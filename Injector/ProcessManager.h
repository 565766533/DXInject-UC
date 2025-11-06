#pragma once
#include <windows.h>
#include <string>

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    bool createSuspendedTarget(const std::wstring& targetPath);
    bool writeSharedGPUData(HANDLE bufferHandle, uint32_t payloadSize, uint32_t encodeKey);
    bool injectInitializationCode();
    bool resumeTarget();
    void terminate();

    DWORD getTargetPID() const { return processInfo.dwProcessId; }

private:
    PROCESS_INFORMATION processInfo;
    HANDLE sharedMemory;
    bool isCreated;

    bool createSharedMemoryMapping(HANDLE bufferHandle, uint32_t payloadSize, uint32_t encodeKey);
};
