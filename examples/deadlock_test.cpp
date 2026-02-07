/**
 * @file deadlock_test.cpp
 * @brief 死锁检测示例程序
 *
 * 该程序演示如何使用 NvDetector SDK 检测多线程程序中的死锁。
 * 通过创建两个线程以相反顺序获取两把锁来制造经典的死锁场景。
 */

#include <cstdio>
#include <mutex>
#include <thread>

#include "detector.h"

// 两把全局互斥锁
std::mutex mutex_a;
std::mutex mutex_b;

// 线程间等待时间（毫秒），确保两个线程都有机会获取第一把锁
constexpr int kThreadDelayMs = 100;

/**
 * @brief 线程1：先锁A再锁B
 */
auto ThreadFunc1() -> void {
  printf("[Thread 1] Trying to lock mutex_a...\n");
  mutex_a.lock();
  printf("[Thread 1] Locked mutex_a\n");

  // 短暂等待，确保线程2有机会锁住mutex_b
  std::this_thread::sleep_for(std::chrono::milliseconds(kThreadDelayMs));

  printf("[Thread 1] Trying to lock mutex_b...\n");
  mutex_b.lock();
  printf("[Thread 1] Locked mutex_b\n");

  // 如果能到这里说明没有死锁
  mutex_b.unlock();
  mutex_a.unlock();
  printf("[Thread 1] Released both locks\n");
}

/**
 * @brief 线程2：先锁B再锁A（与线程1顺序相反，制造死锁）
 */
auto ThreadFunc2() -> void {
  printf("[Thread 2] Trying to lock mutex_b...\n");
  mutex_b.lock();
  printf("[Thread 2] Locked mutex_b\n");

  // 短暂等待，确保线程1有机会锁住mutex_a
  std::this_thread::sleep_for(std::chrono::milliseconds(kThreadDelayMs));

  printf("[Thread 2] Trying to lock mutex_a...\n");
  mutex_a.lock();
  printf("[Thread 2] Locked mutex_a\n");

  // 如果能到这里说明没有死锁
  mutex_a.unlock();
  mutex_b.unlock();
  printf("[Thread 2] Released both locks\n");
}

/**
 * @brief 主函数
 */
auto main() -> int {
  printf("========================================\n");
  printf("Deadlock Detection Test\n");
  printf("========================================\n");

  // ========== 初始化检测器 ==========
  printf("\n>>> Initializing detector...\n");
  DetectorInit("./logs",                   // 日志目录
               kDetectorOptionLock,        // 仅检测死锁
               kOutputOptionConsoleFile);  // 输出到控制台和文件

  // ========== 注册主程序 ==========
  printf(">>> Registering main program...\n");
  DetectorRegisterMain();

  // ========== 启动检测 ==========
  printf(">>> Starting detector...\n");
  DetectorStart();

  printf("\n========================================\n");
  printf("Creating two threads with opposite lock order...\n");
  printf("This should trigger deadlock detection.\n");
  printf("========================================\n\n");

  // ========== 启动两个线程制造死锁 ==========
  std::thread t1(ThreadFunc1);
  std::thread t2(ThreadFunc2);

  // 等待一段时间让死锁发生
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // ========== 检测死锁 ==========
  printf("\n>>> Detecting deadlocks...\n");
  DetectorDetect();

  printf("\n========================================\n");
  printf("Test finished (threads may be deadlocked)\n");
  printf("========================================\n");

  // 分离线程避免 terminate（因为线程可能处于死锁状态无法 join）
  t1.detach();
  t2.detach();

  return 0;
}
