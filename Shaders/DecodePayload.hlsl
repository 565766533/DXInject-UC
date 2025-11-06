StructuredBuffer<uint> encodedPayload : register(t0);
RWStructuredBuffer<uint> decodedPayload : register(u0);

cbuffer DecodeParams : register(b0)
{
    uint payloadSizeInDwords;
    uint encodeKey;
    uint2 padding;
};

[numthreads(256, 1, 1)]
void DecodePayloadCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;

    if (index >= payloadSizeInDwords)
        return;

    uint encoded = encodedPayload[index];

    uint rotateAmount = (index & 31);
    uint rotated;
    if (rotateAmount == 0)
        rotated = encoded;
    else
        rotated = (encoded << rotateAmount) | (encoded >> (32 - rotateAmount));

    uint rollingKey = (encodeKey + index) * 0x45D9F3B;
    rotated ^= rollingKey;

    uint posKey = encodeKey ^ (index * 0x9E3779B9);
    uint decoded = rotated ^ posKey;

    decodedPayload[index] = decoded;
}
