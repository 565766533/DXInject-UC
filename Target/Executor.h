#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

class Executor {
public:
    Executor();
    ~Executor();

    bool executeShellcode(std::vector<uint8_t>& shellcode);

private:
    void* allocatedMemory;
    SIZE_T allocatedSize;

    void patchShellcodeAddresses(std::vector<uint8_t>& shellcode);
};
