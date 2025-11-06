#pragma once
#include <windows.h>
#include <cstdint>

namespace GPUInject {

constexpr uint32_t MAX_PAYLOAD_SIZE = 65536;
constexpr uint32_t DECODE_THREAD_GROUP_SIZE = 256;

constexpr wchar_t PAYLOAD_READY_EVENT[] = L"Local\\GPUInject_PayloadReady";
constexpr wchar_t DECODE_COMPLETE_EVENT[] = L"Local\\GPUInject_DecodeComplete";
constexpr wchar_t EXECUTION_COMPLETE_EVENT[] = L"Local\\GPUInject_ExecutionComplete";
constexpr wchar_t SHARED_MEMORY_NAME[] = L"Local\\GPUInject_SharedMemory";

struct SharedGPUData {
    HANDLE encodedBufferHandle;
    uint32_t payloadSize;
    uint32_t encodeKey;
    uint32_t reserved;
};

struct PayloadChunk {
    uint32_t data;
};

}
