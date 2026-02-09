#include <cstdio>
#include <mutex>
#include <thread>
#include "detector.h"
std::mutex mutex_a;
std::mutex mutex_b;
constexpr int kThreadDelayMs = 100;
auto ThreadFunc1() -> void {
  printf("[Thread 1] Trying to lock mutex_a...\n");
  mutex_a.lock();
  printf("[Thread 1] Locked mutex_a\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(kThreadDelayMs));
  printf("[Thread 1] Trying to lock mutex_b...\n");
  mutex_b.lock();
  printf("[Thread 1] Locked mutex_b\n");
  mutex_b.unlock();
  mutex_a.unlock();
  printf("[Thread 1] Released both locks\n");
}
auto ThreadFunc2() -> void {
  printf("[Thread 2] Trying to lock mutex_b...\n");
  mutex_b.lock();
  printf("[Thread 2] Locked mutex_b\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(kThreadDelayMs));
  printf("[Thread 2] Trying to lock mutex_a...\n");
  mutex_a.lock();
  printf("[Thread 2] Locked mutex_a\n");
  mutex_a.unlock();
  mutex_b.unlock();
  printf("[Thread 2] Released both locks\n");
}
auto main() -> int {
  printf("========================================\n");
  printf("Deadlock Detection Test\n");
  printf("========================================\n");
  printf("\n>>> Initializing detector...\n");
  DetectorInit("./logs",                   
               kDetectorOptionLock,        
               kOutputOptionConsoleFile);  
  printf(">>> Registering main program...\n");
  DetectorRegisterMain();
  printf(">>> Starting detector...\n");
  DetectorStart();
  printf("\n========================================\n");
  printf("Creating two threads with opposite lock order...\n");
  printf("This should trigger deadlock detection.\n");
  printf("========================================\n\n");
  std::thread t1(ThreadFunc1);
  std::thread t2(ThreadFunc2);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  printf("\n>>> Detecting deadlocks...\n");
  DetectorDetect();
  printf("\n========================================\n");
  printf("Test finished (threads may be deadlocked)\n");
  printf("========================================\n");
  t1.detach();
  t2.detach();
  return 0;
}
