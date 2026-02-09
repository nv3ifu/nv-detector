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
constexpr auto kJumpSlot = R_X86_64_JUMP_SLOT;
struct MemoryProtection {
  size_t start;    
  size_t end;      
  int protection;  
};
struct PltHook::Impl {
  const Elf64_Sym* dynsym;
  const char* dynstr;
  size_t dynstr_size;
  void* plt_addr_base;
  const Elf64_Rela* rela_plt;
  size_t rela_plt_cnt;
  std::vector<MemoryProtection> memory_protections;
  static size_t page_size;
  static std::string error_message;
  explicit Impl(struct link_map* lmap);
  void InitializeFromLinkMap(struct link_map* lmap);
  void LoadMemoryProtections();
  auto GetMemoryProtection(void* addr) const -> int;
  static void SetError(const char* fmt, ...);
  static auto FindDynamicEntry(const Elf64_Dyn* dyn, Elf64_Sxword tag)
      -> const Elf64_Dyn*;
};
size_t PltHook::Impl::page_size = 0;
std::string PltHook::Impl::error_message;
PltHook::Impl::Impl(struct link_map* lmap) {
  if (page_size == 0) {
    page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));  
  }
  InitializeFromLinkMap(lmap);
  LoadMemoryProtections();
}
auto PltHook::Impl::InitializeFromLinkMap(struct link_map* lmap) -> void {
  plt_addr_base = reinterpret_cast<void*>(lmap->l_addr);
  const auto* dyn = FindDynamicEntry(lmap->l_ld, DT_SYMTAB);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_SYMTAB");
  }
  dynsym = reinterpret_cast<const Elf64_Sym*>(dyn->d_un.d_ptr);
  dyn = FindDynamicEntry(lmap->l_ld, DT_STRTAB);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_STRTAB");
  }
  dynstr = reinterpret_cast<const char*>(dyn->d_un.d_ptr);
  dyn = FindDynamicEntry(lmap->l_ld, DT_STRSZ);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_STRSZ");
  }
  dynstr_size = dyn->d_un.d_val;
  dyn = FindDynamicEntry(lmap->l_ld, DT_JMPREL);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_JMPREL");
  }
  rela_plt = reinterpret_cast<const Elf64_Rela*>(dyn->d_un.d_ptr);
  dyn = FindDynamicEntry(lmap->l_ld, DT_PLTRELSZ);
  if (dyn == nullptr) {
    throw std::runtime_error("Failed to find DT_PLTRELSZ");
  }
  rela_plt_cnt = dyn->d_un.d_val / sizeof(Elf64_Rela);
}
auto PltHook::Impl::LoadMemoryProtections() -> void {
  FILE* map_fp = fopen("/proc/self/maps", "r");
  if (map_fp == nullptr) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to open /proc/self/maps");
  }
  constexpr size_t kBufSize = PATH_MAX;
  std::array<char, kBufSize> buf{};
  while (fgets(buf.data(), kBufSize, map_fp) != nullptr) {
    size_t start;
    size_t end;
    constexpr size_t kPermsSize = 5;
    std::array<char, kPermsSize> perms{};
    if (sscanf(buf.data(), "%lx-%lx %4s", &start, &end, perms.data()) != 3) {
      continue;
    }
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
    memory_protections.emplace_back(start, end, prot);
  }
  fclose(map_fp);
}
auto PltHook::Impl::GetMemoryProtection(void* addr) const -> int {
  auto addr_val = reinterpret_cast<size_t>(addr);
  for (const auto& prot : memory_protections) {
    if (prot.start <= addr_val && addr_val < prot.end) {
      return prot.protection;
    }
  }
  return 0;
}
auto PltHook::Impl::SetError(const char* fmt, ...) -> void {
  constexpr size_t kBufSize = 512;
  std::array<char, kBufSize> buf{};  
  va_list arg_ptr;
  va_start(arg_ptr, fmt);
  vsnprintf(buf.data(), kBufSize, fmt, arg_ptr);  
  va_end(arg_ptr);
  error_message = buf.data();
}
auto PltHook::Impl::FindDynamicEntry(const Elf64_Dyn* dyn, Elf64_Sxword tag)
    -> const Elf64_Dyn* {
  while (dyn->d_tag != DT_NULL) {
    if (dyn->d_tag == tag) {
      return dyn;
    }
    dyn++;
  }
  return nullptr;
}
auto PltHook::Create(const char* filename) -> std::unique_ptr<PltHook> {
  if (filename == nullptr) {
    printf("Creating PltHook for main executable\n");
    struct link_map* lmap = nullptr;
    void* handle = dlopen(nullptr, RTLD_LAZY);
    if (handle == nullptr) {
      printf("dlopen error: %s\n", dlerror());
      Impl::SetError("dlopen error: %s", dlerror());
      throw std::runtime_error(Impl::error_message);
    }
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lmap) != 0) {
      dlclose(handle);
      Impl::SetError("dlinfo error");
      throw std::runtime_error(Impl::error_message);
    }
    dlclose(handle);
    while (lmap->l_prev != nullptr) {
      lmap = lmap->l_prev;
    }
    return std::unique_ptr<PltHook>(new PltHook(lmap));
  }
  printf("Creating PltHook for %s\n", filename);
  void* handle = dlopen(filename, RTLD_LAZY | RTLD_NOLOAD);
  if (handle == nullptr) {
    printf("dlopen error: %s\n", dlerror());
    Impl::SetError("dlopen error: %s", dlerror());
    throw std::runtime_error(Impl::error_message);
  }
  struct link_map* lmap = nullptr;
  if (dlinfo(handle, RTLD_DI_LINKMAP, &lmap) != 0) {
    dlclose(handle);
    Impl::SetError("dlinfo error");
    throw std::runtime_error(Impl::error_message);
  }
  dlclose(handle);
  return std::unique_ptr<PltHook>(new PltHook(lmap));
}
PltHook::PltHook(struct link_map* lmap)
    : pimpl_(std::make_unique<Impl>(lmap)) {}
PltHook::~PltHook() = default;
auto PltHook::EnumerateSymbols(unsigned int& pos, const char*& name_out,
                               void**& addr_out) const -> PltHook::ErrorCode {
  while (pos < pimpl_->rela_plt_cnt) {
    const auto* plt = &pimpl_->rela_plt[pos];
    if (ELF64_R_TYPE(plt->r_info) == kJumpSlot) {
      size_t idx = ELF64_R_SYM(plt->r_info);
      name_out = pimpl_->dynstr + pimpl_->dynsym[idx].st_name;
      addr_out = reinterpret_cast<void**>(
          reinterpret_cast<char*>(pimpl_->plt_addr_base) + plt->r_offset);
      pos++;
      return PltHook::ErrorCode::kSuccess;
    }
    pos++;
  }
  name_out = nullptr;
  addr_out = nullptr;
  return PltHook::ErrorCode::kEofReached;
}
auto PltHook::ReplaceFunction(const char* funcname, void* newfunc,
                              void** oldfunc) -> PltHook::ErrorCode {
  void* original = dlsym(RTLD_DEFAULT, funcname);
  if (original == nullptr) {
    PltHook::Impl::SetError("No such function: %s", funcname);
    return PltHook::ErrorCode::kFunctionNotFound;
  }
  unsigned int pos = 0;
  const char* name;
  void** addr;
  while (EnumerateSymbols(pos, name, addr) == PltHook::ErrorCode::kSuccess) {
    if (strncmp(name, funcname, strlen(funcname)) == 0 &&
        (name[strlen(funcname)] == '\0' || name[strlen(funcname)] == '@')) {
      int prot = pimpl_->GetMemoryProtection(addr);
      void* page_addr = reinterpret_cast<void*>(
          reinterpret_cast<size_t>(addr) & ~(PltHook::Impl::page_size - 1));
      if (prot == 0) {
        PltHook::Impl::SetError("Could not get memory protection at %p",
                                page_addr);
        return PltHook::ErrorCode::kInternalError;
      }
      if ((prot & PROT_WRITE) == 0) {
        if (mprotect(page_addr, PltHook::Impl::page_size, prot | PROT_WRITE) !=
            0) {
          PltHook::Impl::SetError(
              "Could not change memory protection at %p: %s", page_addr,
              strerror(errno));
          return PltHook::ErrorCode::kInternalError;
        }
      }
      if (oldfunc != nullptr) {
        *oldfunc = original;  
      }
      *addr = newfunc;
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
  PltHook::Impl::SetError("No such function: %s", funcname);
  return PltHook::ErrorCode::kFunctionNotFound;
}
auto PltHook::GetLastError() -> const std::string& {
  return Impl::error_message;
}
