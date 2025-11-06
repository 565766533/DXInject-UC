#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <vector>

class GPULoader {
public:
    GPULoader();
    ~GPULoader();

    bool initialize();
    bool loadSharedGPUData();
    bool decodePayloadOnGPU();
    std::vector<uint8_t> retrieveDecodedPayload();
    void cleanup();

private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11Buffer* encodedBuffer;
    ID3D11Buffer* decodedBuffer;
    ID3D11Buffer* stagingBuffer;
    ID3D11Buffer* constantBuffer;
    ID3D11ComputeShader* decodeShader;
    ID3D11ShaderResourceView* encodedSRV;
    ID3D11UnorderedAccessView* decodedUAV;

    HANDLE sharedBufferHandle;
    uint32_t payloadSize;
    uint32_t encodeKey;

    bool loadComputeShader();
    bool openSharedBuffer();
    bool createDecodeResources();
};
