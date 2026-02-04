#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "plthook.h"

// 定义重定位类型常量，这里使用x86_64架构的JUMP_SLOT类型
// 这是PLT表中用于函数调用的重定位类型
constexpr auto kJumpSlot = R_X86_64_JUMP_SLOT;

// 内存保护信息结构体，用于存储内存区域的保护属性
struct MemoryProtection {
  size_t start;    // 内存区域起始地址
  size_t end;      // 内存区域结束地址
  int protection;  // 保护属性(PROT_READ, PROT_WRITE, PROT_EXEC的组合)
};

// PltHook类的实现细节
struct PltHook::Impl {
  // 动态符号表指针，包含了程序中所有动态链接符号的信息
  // 每个符号包含名称、大小、类型、绑定信息等
  const Elf64_Sym* dynsym;

  // 动态字符串表指针，存储了符号名称等字符串数据
  // 符号表中的st_name字段是字符串表的索引
  const char* dynstr;

  // 动态字符串表的大小，用于边界检查
  size_t dynstr_size;

  // PLT（过程链接表）的基地址，用于计算函数地址的偏移
  // 这是加载到内存中的共享库的基地址
  void* plt_addr_base;

  // 重定位表指针，包含了需要重定位的条目信息
  // 对于PLT，这是.rela.plt段，包含了函数调用的重定位信息
  const Elf64_Rela* rela_plt;

  // 重定位表中条目的数量，用于遍历所有重定位项
  size_t rela_plt_cnt;

  // 内存页保护属性的vector，记录了各个内存区域的读写执行权限
  // 用于在修改PLT表项时临时更改内存保护属性
  std::vector<MemoryProtection> memory_protections;

  // 系统内存页大小（静态成员），通常为4KB
  // 用于内存保护操作时对齐地址
  static size_t page_size;

  // 错误信息存储（静态成员）
  // 用于记录操作过程中的错误信息
  static std::string error_message;

  // 构造函数，接收动态链接器的映射信息
  // link_map包含了共享库的加载地址和动态段信息
  explicit Impl(struct link_map* lmap);

  // 从link_map初始化PLT hook所需的各种表和地址信息
  // 解析动态段中的符号表、字符串表和重定位表
  void InitializeFromLinkMap(struct link_map* lmap);

  // 加载进程内存映射的保护属性信息
  // 通过读取/proc/self/maps获取内存区域的保护属性
  void LoadMemoryProtections();

  // 获取指定地址的内存保护属性
  // 用于在修改PLT表项前检查内存保护
  auto GetMemoryProtection(void* addr) const -> int;

  // 设置错误信息的辅助函数
  // 支持格式化字符串，类似printf
  static void SetError(const char* fmt, ...);

  // 在动态段表中查找指定类型的表项
  // 用于查找符号表、字符串表等关键表的位置
  static auto FindDynamicEntry(const Elf64_Dyn* dyn, Elf64_Sxword tag)
      -> const Elf64_Dyn*;
};

// 初始化静态成员变量
size_t PltHook::Impl::page_size = 0;
std::string PltHook::Impl::error_message;

// Impl构造函数实现
PltHook::Impl::Impl(struct link_map* lmap) {
  // 首次使用时初始化页大小
  if (page_size == 0) {
    page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));  // 获取系统页大小
  }
  // 初始化PLT hook所需的表和地址信息
  InitializeFromLinkMap(lmap);
  // 加载内存保护信息
  LoadMemoryProtections();
}

// 从link_map初始化所需的表和地址信息
auto PltHook::Impl::InitializeFromLinkMap(struct link_map* lmap) -> void {
  // 设置基地址，这是共享库加载到内存中的基地址
  plt_addr_base = reinterpret_cast<void*>(lmap->l_addr);

  // 获取动态段表中的符号表
  const auto* dyn = FindDynamicEntry(lmap->l_ld, DT_SYMTAB);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_SYMTAB");
  }
  dynsym = reinterpret_cast<const Elf64_Sym*>(dyn->d_un.d_ptr);

  // 获取动态段表中的字符串表
  dyn = FindDynamicEntry(lmap->l_ld, DT_STRTAB);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_STRTAB");
  }
  dynstr = reinterpret_cast<const char*>(dyn->d_un.d_ptr);

  // 获取动态段表中的字符串表大小
  dyn = FindDynamicEntry(lmap->l_ld, DT_STRSZ);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_STRSZ");
  }
  dynstr_size = dyn->d_un.d_val;

  // 获取动态段表中的重定位表（PLT专用）
  dyn = FindDynamicEntry(lmap->l_ld, DT_JMPREL);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_JMPREL");
  }
  rela_plt = reinterpret_cast<const Elf64_Rela*>(dyn->d_un.d_ptr);

  // 获取动态段表中的重定位表大小
  dyn = FindDynamicEntry(lmap->l_ld, DT_PLTRELSZ);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_PLTRELSZ");
  }
  // 计算重定位表中的条目数量
  rela_plt_cnt = dyn->d_un.d_val / sizeof(Elf64_Rela);
}

// 加载内存保护信息
auto PltHook::Impl::LoadMemoryProtections() -> void {
  // 打开/proc/self/maps文件，获取进程内存映射信息
  FILE* map_fp = fopen("/proc/self/maps", "r");
  if (map_fp == nullptr) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to open /proc/self/maps");
  }
  constexpr size_t kBufSize = PATH_MAX;
  std::array<char, kBufSize> buf{};
  // 逐行读取内存映射信息
  while (fgets(buf.data(), kBufSize, map_fp) != nullptr) {
    size_t start;
    size_t end;
    constexpr size_t kPermsSize = 5;
    std::array<char, kPermsSize> perms{};
    // 解析内存区域的起始地址、结束地址和权限
    if (sscanf(buf.data(), "%lx-%lx %4s", &start, &end, perms.data()) != 3) {
      continue;
    }

    // 解析权限字符串为保护标志
    int prot = 0;
    if (perms[0] == 'r') {
      prot |= PROT_READ;
    }
    if (perms[1] == 'w') {
      prot |= PROT_WRITE;
    }
    if (perms[2] == 'x') {
      prot |= PROT_EXEC;
    }

    // 保存内存区域的保护信息
    memory_protections.emplace_back(start, end, prot);
  }
  fclose(map_fp);
}

// 获取指定地址的内存保护属性
auto PltHook::Impl::GetMemoryProtection(void* addr) const -> int {
  auto addr_val = reinterpret_cast<size_t>(addr);
  // 遍历内存保护信息，查找包含指定地址的区域
  for (const auto& prot : memory_protections) {
    if (prot.start <= addr_val && addr_val < prot.end) {
      return prot.protection;
    }
  }
  // 未找到对应的内存区域
  return 0;
}

// 设置错误信息
auto PltHook::Impl::SetError(const char* fmt, ...) -> void {
  constexpr size_t kBufSize = 512;
  std::array<char, kBufSize> buf{};  // 零初始化，全是 \0

  va_list arg_ptr;
  va_start(arg_ptr, fmt);
  vsnprintf(buf.data(), kBufSize, fmt, arg_ptr);  // 保证在位置 0~len 写 \0
  va_end(arg_ptr);

  // 读到 \0 停止，不会越界（因为 buf 末尾肯定是 \0）
  error_message = buf.data();
}

// 在动态段表中查找指定类型的表项
auto PltHook::Impl::FindDynamicEntry(const Elf64_Dyn* dyn, Elf64_Sxword tag)
    -> const Elf64_Dyn* {
  // 遍历动态段表，直到找到指定类型的表项或到达表尾
  while (dyn->d_tag != DT_NULL) {
    if (dyn->d_tag == tag) {
      return dyn;
    }
    dyn++;
  }
  // 未找到指定类型的表项
  return nullptr;
}

// PltHook实现 - 创建PltHook实例
auto PltHook::Create(const char* filename) -> std::unique_ptr<PltHook> {
  // 如果filename为NULL，则获取主程序的link_map
  if (filename == nullptr) {
    printf("Creating PltHook for main executable\n");

    // 获取主程序的link_map
    struct link_map* lmap = nullptr;
    void* handle = dlopen(nullptr, RTLD_LAZY);
    if (handle == nullptr) {
      printf("dlopen error: %s\n", dlerror());
      Impl::SetError("dlopen error: %s", dlerror());
      throw std::runtime_error(Impl::error_message);
    }

    // 使用dlinfo获取link_map信息
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lmap) != 0) {
      dlclose(handle);
      Impl::SetError("dlinfo error");
      throw std::runtime_error(Impl::error_message);
    }
    dlclose(handle);

    // 找到link_map链表的第一个元素（主程序）
    while (lmap->l_prev != nullptr) {
      lmap = lmap->l_prev;
    }

    // 创建PltHook实例
    return std::unique_ptr<PltHook>(new PltHook(lmap));
  }

  // 处理指定的动态库
  printf("Creating PltHook for %s\n", filename);
  // 打开动态库，但不加载新的副本（RTLD_NOLOAD）
  void* handle = dlopen(filename, RTLD_LAZY | RTLD_NOLOAD);
  if (handle == nullptr) {
    printf("dlopen error: %s\n", dlerror());
    Impl::SetError("dlopen error: %s", dlerror());
    throw std::runtime_error(Impl::error_message);
  }

  // 获取动态链接库的link_map信息
  struct link_map* lmap = nullptr;
  if (dlinfo(handle, RTLD_DI_LINKMAP, &lmap) != 0) {
    dlclose(handle);
    Impl::SetError("dlinfo error");
    throw std::runtime_error(Impl::error_message);
  }
  dlclose(handle);

  // 创建PltHook实例，并返回智能指针
  return std::unique_ptr<PltHook>(new PltHook(lmap));
}

// PltHook构造函数
PltHook::PltHook(struct link_map* lmap)
    : pimpl_(std::make_unique<Impl>(lmap)) {}

// PltHook析构函数（使用默认实现）
PltHook::~PltHook() = default;

// 枚举符号表中的所有符号
auto PltHook::EnumerateSymbols(unsigned int& pos, const char*& name_out,
                               void**& addr_out) const -> PltHook::ErrorCode {
  // 遍历重定位表
  while (pos < pimpl_->rela_plt_cnt) {
    const auto* plt = &pimpl_->rela_plt[pos];
    // 检查是否为JUMP_SLOT类型的重定位（函数调用）
    if (ELF64_R_TYPE(plt->r_info) == kJumpSlot) {
      // 获取符号索引
      size_t idx = ELF64_R_SYM(plt->r_info);
      // 获取符号名称
      name_out = pimpl_->dynstr + pimpl_->dynsym[idx].st_name;
      // 获取符号地址（PLT表项地址）
      addr_out = reinterpret_cast<void**>(
          reinterpret_cast<char*>(pimpl_->plt_addr_base) + plt->r_offset);
      pos++;
      return PltHook::ErrorCode::kSuccess;
    }
    pos++;
  }
  // 遍历结束
  name_out = nullptr;
  addr_out = nullptr;
  return PltHook::ErrorCode::kEofReached;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto PltHook::ReplaceFunction(const char* funcname, void* newfunc,
                              void** oldfunc) -> PltHook::ErrorCode {
  // 先获取原始函数地址，这会强制解析符号
  void* original = dlsym(RTLD_DEFAULT, funcname);
  if (original == nullptr) {
    PltHook::Impl::SetError("No such function: %s", funcname);
    return PltHook::ErrorCode::kFunctionNotFound;
  }

  unsigned int pos = 0;
  const char* name;
  void** addr;

  // 遍历所有符号，查找目标函数
  while (EnumerateSymbols(pos, name, addr) == PltHook::ErrorCode::kSuccess) {
    // 检查函数名是否匹配（考虑版本后缀，如"func@GLIBC_2.2.5"）
    if (strncmp(name, funcname, strlen(funcname)) == 0 &&
        (name[strlen(funcname)] == '\0' || name[strlen(funcname)] == '@')) {
      // 获取内存页的保护属性
      int prot = pimpl_->GetMemoryProtection(addr);
      // 计算包含该地址的页的起始地址
      void* page_addr = reinterpret_cast<void*>(
          reinterpret_cast<size_t>(addr) & ~(PltHook::Impl::page_size - 1));

      if (prot == 0) {
        PltHook::Impl::SetError("Could not get memory protection at %p",
                                page_addr);
        return PltHook::ErrorCode::kInternalError;
      }

      // 如果内存页不可写，则临时修改为可写
      if ((prot & PROT_WRITE) == 0) {
        if (mprotect(page_addr, PltHook::Impl::page_size, prot | PROT_WRITE) !=
            0) {
          PltHook::Impl::SetError(
              "Could not change memory protection at %p: %s", page_addr,
              strerror(errno));
          return PltHook::ErrorCode::kInternalError;
        }
      }

      // 保存原始函数地址并替换为新函数
      if (oldfunc != nullptr) {
        *oldfunc = original;  // 使用已解析的地址
      }
      // 修改GOT表项，指向新函数
      *addr = newfunc;

      // 恢复内存页的保护属性
      if ((prot & PROT_WRITE) == 0) {
        if (mprotect(page_addr, PltHook::Impl::page_size, prot) != 0) {
          PltHook::Impl::SetError(
              "Could not restore memory protection at %p: %s", page_addr,
              strerror(errno));
          return PltHook::ErrorCode::kInternalError;
        }
      }
      return PltHook::ErrorCode::kSuccess;
    }
  }

  // 未找到目标函数
  PltHook::Impl::SetError("No such function: %s", funcname);
  return PltHook::ErrorCode::kFunctionNotFound;
}

// 获取最后一次错误信息
auto PltHook::GetLastError() -> const std::string& {
  return Impl::error_message;
}
