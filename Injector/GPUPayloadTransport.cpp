#include "GPUPayloadTransport.h"
#include "SharedStructures.h"
#include <iostream>

GPUPayloadTransport::GPUPayloadTransport()
    : device(nullptr)
    , context(nullptr)
    , encodedBuffer(nullptr)
    , sharedBufferHandle(nullptr)
    , payloadSize(0)
    , encodeKey(0)
{
}

GPUPayloadTransport::~GPUPayloadTransport() {
    cleanup();
}

bool GPUPayloadTransport::initialize() {
    D3D_FEATURE_LEVEL featureLevel;
    UINT createFlags = 0;

#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context
    );

    if (FAILED(hr)) {
        std::cerr << "failed to create d3d11 device\n";
        return false;
    }

    return true;
}

std::vector<uint32_t> GPUPayloadTransport::encodePayload(const uint8_t* payload, uint32_t size, uint32_t key) {
    uint32_t dwordCount = (size + 3) / 4;
    std::vector<uint32_t> encoded(dwordCount, 0);

    // 将任意长度字节流打包成 dword，方便 GPU 端按 uint 读取。
    for (uint32_t i = 0; i < size; i++) {
        uint32_t dwordIndex = i / 4;
        uint32_t byteOffset = i % 4;
        encoded[dwordIndex] |= (static_cast<uint32_t>(payload[i]) << (byteOffset * 8));
    }

    // 位置相关的 XOR + 旋转提供轻量混淆，Target 端用 compute shader 逆运算。
    for (uint32_t i = 0; i < dwordCount; i++) {
        uint32_t value = encoded[i];

        uint32_t posKey = key ^ (i * 0x9E3779B9);
        value ^= posKey;

        uint32_t rollingKey = (key + i) * 0x45D9F3B;
        value ^= rollingKey;

        uint32_t rotateAmount = (i & 31);
        if (rotateAmount == 0)
            encoded[i] = value;
        else
            encoded[i] = (value >> rotateAmount) | (value << (32 - rotateAmount));
    }

    return encoded;
}

bool GPUPayloadTransport::encodeAndUploadPayload(const uint8_t* payload, uint32_t size, uint32_t key) {
    if (size > GPUInject::MAX_PAYLOAD_SIZE) {
        std::cerr << "payload too large\n";
        return false;
    }

    this->payloadSize = size;
    this->encodeKey = key;

    std::vector<uint32_t> encoded = encodePayload(payload, size, key);

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = static_cast<UINT>(encoded.size() * sizeof(uint32_t));
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(uint32_t);

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = encoded.data();

    // 创建可共享的结构化缓冲区，填充编码后的数据。
    HRESULT hr = device->CreateBuffer(&bufferDesc, &initData, &encodedBuffer);
    if (FAILED(hr)) {
        std::cerr << "failed to create gpu buffer\n";
        return false;
    }

    context->Flush();

    IDXGIResource* dxgiResource = nullptr;
    hr = encodedBuffer->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiResource);
    if (FAILED(hr)) {
        std::cerr << "failed to query dxgi interface\n";
        return false;
    }

    // 将资源转换成跨进程可传递的共享句柄，写入共享内存供 Target 打开。
    hr = dxgiResource->GetSharedHandle(&sharedBufferHandle);
    dxgiResource->Release();

    if (FAILED(hr)) {
        std::cerr << "failed to get shared handle\n";
        return false;
    }

    // 提前释放 Keyed Mutex，确保目标进程能够 Acquire。
    IDXGIKeyedMutex* keyedMutex = nullptr;
    hr = encodedBuffer->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&keyedMutex);
    if (SUCCEEDED(hr)) {
        keyedMutex->ReleaseSync(0);
        keyedMutex->Release();
    }

    return true;
}

void GPUPayloadTransport::cleanup() {
    if (encodedBuffer) {
        encodedBuffer->Release();
        encodedBuffer = nullptr;
    }
    if (context) {
        context->Release();
        context = nullptr;
    }
    if (device) {
        device->Release();
        device = nullptr;
    }
}
