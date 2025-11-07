#include "ProcessManager.h"
#include "SharedStructures.h"
#include <iostream>

ProcessManager::ProcessManager()
    : sharedMemory(nullptr)
    , isCreated(false)
{
    ZeroMemory(&processInfo, sizeof(processInfo));
}

ProcessManager::~ProcessManager() {
    terminate();
}

bool ProcessManager::createSuspendedTarget(const std::wstring& targetPath) {
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);

    std::wstring cmdLine = targetPath;

    BOOL success = CreateProcessW(
        nullptr,
        &cmdLine[0],
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );

    if (!success) {
        std::cerr << "failed to create target process\n";
        return false;
    }

    isCreated = true;
    return true;
}

bool ProcessManager::createSharedMemoryMapping(HANDLE bufferHandle, uint32_t payloadSize, uint32_t encodeKey) {
    // 使用具名共享内存承载 DXGI 共享句柄和元数据，便于目标进程读取。
    sharedMemory = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(GPUInject::SharedGPUData),
        GPUInject::SHARED_MEMORY_NAME
    );

    if (!sharedMemory) {
        std::cerr << "failed to create shared memory\n";
        return false;
    }

    void* mappedView = MapViewOfFile(
        sharedMemory,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(GPUInject::SharedGPUData)
    );

    if (!mappedView) {
        std::cerr << "failed to map view of file\n";
        return false;
    }

    GPUInject::SharedGPUData* sharedData = static_cast<GPUInject::SharedGPUData*>(mappedView);
    sharedData->encodedBufferHandle = bufferHandle;
    sharedData->payloadSize = payloadSize;
    sharedData->encodeKey = encodeKey;
    sharedData->reserved = 0;

    UnmapViewOfFile(mappedView);
    return true;
}

bool ProcessManager::writeSharedGPUData(HANDLE bufferHandle, uint32_t payloadSize, uint32_t encodeKey) {
    return createSharedMemoryMapping(bufferHandle, payloadSize, encodeKey);
}

bool ProcessManager::injectInitializationCode() {
    return true;
}

bool ProcessManager::resumeTarget() {
    if (!isCreated) {
        return false;
    }

    // 目标在挂起状态完成共享数据写入后再恢复，避免竞态。
    DWORD result = ResumeThread(processInfo.hThread);
    if (result == (DWORD)-1) {
        std::cerr << "failed to resume thread\n";
        return false;
    }

    return true;
}

void ProcessManager::terminate() {
    if (sharedMemory) {
        CloseHandle(sharedMemory);
        sharedMemory = nullptr;
    }

    if (isCreated) {
        if (processInfo.hThread) {
            CloseHandle(processInfo.hThread);
        }
        if (processInfo.hProcess) {
            CloseHandle(processInfo.hProcess);
        }
        isCreated = false;
    }
}
