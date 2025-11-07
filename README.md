# DXInject-UC

DXInject-UC 是一个基于 DirectX 11 的 GPU 载荷传输与解码样例，演示如何借助显卡共享缓冲区、计算着色器和跨进程同步原语，实现“显卡协助的进程空洞化”（GPU-assisted process hollowing）。项目由 `Injector`（负责编码/上传 Shellcode）与 `Target`（负责从显卡解码并执行 Shellcode）两个可执行文件构成，配合共享内存与事件进行通信。

> ⚠️ **仅限研究用途**：本仓库用于学习 GPU 资源共享、同步与安全研究，请勿在未授权环境中运行。

## 功能亮点

- **显存载荷传输**：`Injector` 通过 `ID3D11Buffer` 创建共享缓冲区，并将编码后的载荷上传至 GPU（`Injector/GPUPayloadTransport.cpp`）。
- **计算着色器解码**：`Target` 使用 `Shaders/DecodePayload.hlsl` 计算着色器在 GPU 侧快速解码，再回读到 CPU。
- **跨进程同步**：双方通过具名事件和共享内存（`Common/SharedStructures.h`）交换 DXGI 共享句柄、载荷大小以及编码密钥。
- **动态 Shellcode 修补与执行**：`Target` 在执行前动态解析 `MessageBoxA` / `ExitThread` 地址并写入解码后的 Shellcode（`Target/Executor.cpp`）。

## 目录结构

| 目录/文件 | 说明 |
| --- | --- |
| `DXInject.sln` | Visual Studio 解决方案，包含 Injector 与 Target 工程 |
| `Common/SharedStructures.h` | 跨进程共用的同步对象名称、共享结构体定义 |
| `Injector/` | 注入端：GPU 载荷编码、事件创建、目标进程管理 |
| `Target/` | 目标端：窗口壳、GPU 解码器与 Shellcode 执行器 |
| `Shaders/DecodePayload.*` | 负责解码的 HLSL 计算着色器与已编译 `.cso` |

## 技术栈

- C++17 / Win32 API / Visual Studio 2019+
- Direct3D 11（设备、资源共享、计算着色器）
- DXGI 共享句柄、Keyed Mutex 同步
- HLSL 计算着色器 (`cs_5_0`)

## 构建要求

1. Windows 10/11 x64，启用 GPU 驱动并支持 Direct3D 11。
2. Visual Studio 2019 或 2022（带 Desktop development with C++ workload）。
3. Windows 10 SDK（含 `fxc.exe` 或安装最新版 `dxc.exe` 用于编译着色器）。
4. 将 `Target.exe` 与 `Injector.exe` 置于同一目录，或调整 `Injector/Main.cpp` 中的路径逻辑。

## 快速开始

1. **获取源码**：`git clone` 仓库并同步子模块（如有）。
2. **编译着色器**（推荐放在 `Shaders` 目录执行）：
   ```bash
   fxc /nologo /T cs_5_0 /E DecodePayloadCS /Fo DecodePayload.cso DecodePayload.hlsl
   ```
   或
   ```bash
   dxc -T cs_6_0 -E DecodePayloadCS -Fo DecodePayload.cso DecodePayload.hlsl
   ```
3. **打开解决方案**：使用 Visual Studio 打开 `DXInject.sln`，选择 `x64` 配置并分别构建 `Injector`、`Target`。
4. **部署**：确保 `Target.exe`、`Injector.exe` 以及 `Shaders/DecodePayload.cso` 位于 VS 生成的输出目录（默认 `x64/Debug` 或 `x64/Release`）。
5. **运行 Demo**：
   - 手动启动 `Target.exe`，或让 `Injector` 创建目标进程（默认从当前目录查找 `Target.exe`）。
   - 执行 `Injector.exe`，观察控制台输出及目标窗口弹出的消息框。

## 工作流程概览

```
Shellcode
   │（编码 + 加密，CPU）
   ▼
Injector.exe ──共享句柄/元数据──► Shared Memory + Events
   │                                     ▲
   │（共享 ID3D11Buffer）                │
   └─────────────────────────────────────┘
Target.exe ──GPU Decode (Compute Shader)─► CPU Payload ──► Executor (CreateThread)
```

1. `Injector` 将 Shellcode 打包为 `uint32_t` 数组并施加位置相关的异或与旋转混淆。
2. 通过 `ID3D11Buffer` + `IDXGIResource::GetSharedHandle` 获取可跨进程共享的句柄，并写入共享内存。
3. `Target` 读取句柄、payload 大小与密钥，使自身设备打开同一块显存，随后绑定 `DecodePayloadCS` 计算着色器执行逆向运算。
4. 解码数据被复制到 staging buffer，再映射回系统内存交由 `Executor` 分配 RWX 内存并运行。
5. 双方通过 `Local\GPUInject_*` 事件实现 Payload 准备、解码完成与执行完成的握手。

## 核心模块

- **GPUPayloadTransport**（`Injector/GPUPayloadTransport.*`）：负责初始化 D3D11 设备、编码 Shellcode 并创建带共享句柄的结构化缓冲区。
- **ProcessManager**（`Injector/ProcessManager.*`）：负责创建挂起的目标进程、写入共享内存并恢复主线程。
- **GPULoader**（`Target/GPULoader.*`）：在目标进程中打开共享缓冲区，配置 SRV/UAV、常量缓冲，调度计算着色器并将结果复制回 CPU。
- **Executor**（`Target/Executor.*`）：将解码结果修补 API 地址后写入 RWX 内存，并创建线程执行。
- **Shellcode**（`Injector/Shellcode.h`）：示例载荷，展示地址修补方式以及成功后的弹窗字符串。

## 同步原语与数据结构

| 名称 | 类型 | 作用 |
| --- | --- | --- |
| `Local\GPUInject_PayloadReady` | 事件 | `Injector` 上传完成后置位，提示 `Target` 可以读取共享数据 |
| `Local\GPUInject_DecodeComplete` | 事件 | `Target` 解码完毕后通知 `Injector` |
| `Local\GPUInject_ExecutionComplete` | 事件 | Shellcode 执行结束通知 `Injector` |
| `SharedGPUData` | 共享内存结构 | 保存共享缓冲区句柄、载荷大小和编码密钥 |

## 常见问题 / 限制

- **仅支持 Windows + D3D11**：若 GPU/驱动不满足 D3D11 要求，`D3D11CreateDevice` 会失败。
- **未实现完整错误处理**：代码以 PoC 为主，真实场景需补充异常路径、日志与安全检查。
- **Shellcode 示例较简单**：目前只演示 `MessageBoxA`，需要自行替换更复杂的载荷。
- **未对显卡同步进行健壮封装**：高并发或不同权限级别的进程可能需要 Keyed Mutex 更严谨的 acquire/release。

## 研究/法律声明

本项目仅用于学习 GPU 资源共享、跨进程同步与安全研究技巧。请遵守当地法律法规，在授权的、可控的环境中运行和测试；作者与维护者不对任何滥用行为负责。
