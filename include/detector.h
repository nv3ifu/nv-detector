#pragma once
extern "C" {
enum DetectorOption {
  kDetectorOptionMemory = 1,      
  kDetectorOptionLock = 2,        
  kDetectorOptionMemoryLock = 3,  
};
enum OutputOption {
  kOutputOptionConsole = 1,      
  kOutputOptionFile = 2,         
  kOutputOptionConsoleFile = 3,  
};
void DetectorInit(const char* work_dir, DetectorOption detect_option,
                  OutputOption output_option);
void DetectorStart(void);
void DetectorDetect(void);
void DetectorRegister(const char* lib_name);
void DetectorRegisterMain(void);
}  
