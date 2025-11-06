#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <vector>

class GPUPayloadTransport {
public:
    GPUPayloadTransport();
    ~GPUPayloadTransport();

    bool initialize();
    bool encodeAndUploadPayload(const uint8_t* payload, uint32_t size, uint32_t encodeKey);
    HANDLE getSharedBufferHandle() const { return sharedBufferHandle; }
    uint32_t getPayloadSize() const { return payloadSize; }
    void cleanup();

private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11Buffer* encodedBuffer;
    HANDLE sharedBufferHandle;
    uint32_t payloadSize;
    uint32_t encodeKey;

    std::vector<uint32_t> encodePayload(const uint8_t* payload, uint32_t size, uint32_t key);
};
