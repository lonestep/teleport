[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/lonestep/teleport/blob/master/LICENSE)

# Teleport

High-performance, zero-daemon, shared-memory Inter-Process Communication (IPC) library in native C++.

基于共享内存的高性能、无守护进程、原生 C++ 进程间通信（IPC）基础库。

---

## 1. Supported Platforms & Compilers (支持的平台与编译器)

Teleport 基于纯原生 C++ 实现，无任何第三方库依赖，支持主流桌面端、服务器端操作系统与容器环境。

| 操作系统 / OS | 最低版本要求 | 验证环境 | 体系架构 |
| :--- | :--- | :--- | :---: |
| **Linux** | Linux Kernel 3.10+ | Ubuntu 24.04 LTS, Ubuntu 22.04 LTS, Debian 12, RHEL / CentOS 9/10 | x86_64, aarch64 |
| **Windows** | Windows 7 / Server 2008 R2 | Windows 11, Windows 10, Windows Server 2022 / 2019 | x86_64, x86 |
| **Container** | OCI 兼容容器运行时 | Docker, Podman (Ubuntu 24.04 LTS Container, Windows VM on KVM) | x86_64 |

### 编译器兼容性 (Compiler Compatibility)

* **GCC**: GCC 7.3 及更高版本（完整支持 GCC 11 / 12 / 13 / 14）
* **Clang**: Clang 9.0 及更高版本（完整支持 Clang 15 / 16 / 17 / 18）
* **MSVC**: Microsoft Visual Studio 2017 / 2019 / 2022 (MSVC v141 / v142 / v143)
* **MinGW-w64**: x86_64-w64-mingw32-g++ 8.0+

---

## 2. Features (核心特性)

* **连续环形日志缓冲区 (Continuous Circular Log Buffer)**：采用类似 Aeron 架构的单一连续物理内存环形日志，支持 1 字节至 4MB 变长消息，解除固定 4KB 槽位限制；通过尾部对齐环绕填充（`LOG_RECORD_FLAG_PADDING`）与 64 字节缓存行对齐消除伪共享（False Sharing）。
* **三级背压与 QoS 流控策略 (Three-Tier Backpressure Policies)**：
  * `POLICY_BLOCK`（默认）：严格零丢失可靠投递，通过原子确认游标侦测最慢消费者，缓冲区满时发送端进入自适应背压；
  * `POLICY_DROP_OLDEST`：最新优先，写游标主动覆盖最旧未读，落后消费者自动重置游标并触发丢包统计；
  * `POLICY_ISOLATE_SLOW_CONSUMER`：动态监测下游积压量，超过滞后阈值（如 8MB）自动标记为隔离状态，防止单一慢节点阻碍主干链路。
* **混合自适应等待策略 (Hybrid Adaptive Wait Strategy)**：三阶段平滑降级（CPU pause 自旋 -> 线程 yield 让步 -> 内核事件/Futex 阻塞），兼顾亚微秒级延迟与空闲时 0% CPU 占用。
* **原生跨进程同步 RPC 框架 (Synchronous Cross-Process RPC)**：内置基于 64 位关联 ID（`CorrelationId`）与调用方私有专用回复通道的请求-响应机制（`RegisterRpcService` / `Call`）。
* **零拷贝与内存屏障保障 (Zero-Copy & Memory Barriers)**：支持共享内存就地分配与提交（`AcquireBuffer` / `CommitBuffer`），消除临时堆分配与二次拷贝。
* **多发送者与多接收者并发支持 (Multi-Producer Multi-Consumer)**：支持多进程并发发布与多进程全量订阅广播，内置原子 CRC32 数据完整性校验。
* **无外部依赖与无守护进程 (Daemon-Free Architecture)**：全量代码仅由 5 个源文件组成（`platform.*`, `teleport.*`, `typedefs.hpp`），无需独立运行服务进程（Daemon-free），无需 Boost，兼容 C++11 及更高版本。

---

## 3. Technical Comparison Matrix (技术对比矩阵)

### 3.1 核心架构与设计选型对比 (Core Architecture Comparison)

| 核心设计维度 / Dimension | Teleport | Aeron (IPC Mode) | Iceoryx / Iceoryx2 | ZeroMQ / NNG |
| :--- | :--- | :--- | :--- | :--- |
| **拓扑与通道模型** | **无中心点对多广播 / 专用RPC链路** | 单向流通道 (Stream Channel) | 发布/订阅与请求/响应服务 | 节点间套接字拓扑 (REQ/REP, PUB/SUB) |
| **外部守护进程依赖** | **零守护进程 (Daemon-Free, 点对点直连)** | 必须独立运行 **Media Driver** 守护进程 | 必须独立运行 **RouDi** 管理中枢守护进程 | 进程内线程引擎，无外部独立守护进程 |
| **内存组织形态** | **单一片上连续环形日志 (Continuous Ring)** | 三段式轮换 LogBuffer (Term Rotation) | 基于内存池的固定大小 Chunk 块管理 | 内核与用户态多层缓冲帧拷贝 |
| **消息长度支持** | **1B ~ 4MB 动态变长 (自动填充行对齐)** | 变长分片 (支持大包重组) | 固定分块 Chunk (超大包需跨块或多段分配) | 任意长度变长帧 |
| **等待与通知策略** | **混合自适应 (Pause -> Yield -> Event)** | 纯轮询 Busy Spin / 线程睡眠衰减策略 | 线程轮询 / POSIX 条件变量通知 | 内核事件驱动 (epoll / kqueue / IOCP) |
| **流控与慢消费者** | **三级 QoS (BLOCK / DROP_OLDEST / ISOLATE)** | 慢消费者阻塞单流所有发布者 | 队列深度限制 (KeepLast / DropOldest) | 高低水位线 (HWM) 丢弃或阻塞 |
| **崩溃恢复与保活** | **原子位图检测僵尸进程并释放游标** | 驱动心跳保活检测并清理租约 | 运行时监控客户端崩溃并回收 Chunk | 套接字断开重连机制 |
| **工程侵入与依赖** | **5 个原生源文件直编，零第三方依赖** | 复杂构建依赖，跨进程驱动部署配置繁重 | C++14/17 强类型框架绑定，部署流程复杂 | 动态库/静态库引入，依赖 C++ 运行时 |

---

### 3.2 技术规格横向对比 (Detailed Comparison Table)

| 技术维度 / Dimension | Teleport | Aeron (IPC Mode) | Iceoryx / Iceoryx2 | ZeroMQ (IPC) | Boost.Interprocess |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **单通道点对点吞吐** | **4.51M msg/s** | ~3.50M msg/s | ~2.80M msg/s | ~0.65M msg/s | ~0.90M msg/s |
| **多接收者聚合消费吞吐** | **15.07M 交付/s** | ~8.00M 交付/s | ~6.50M 交付/s | ~0.80M 交付/s | 需加锁串行化 |
| **单向端到端延迟** | **Min 100ns / P50 300ns** | ~350ns | ~400ns | 15~40 µs | 1~5 µs |
| **内存组织结构** | **1B ~ 4MB 连续环形日志** | 连续 LogBuffer | 固定大小 Chunk 分块 | 套接字帧内存拷贝 | 内存池或分段分配 |
| **QoS 背压流控** | **Block / DropOldest / Isolate** | 慢消费者阻塞发送者 | KeepLast / DropOldest | 高低水位线 (HWM) | 无 |
| **原生跨进程同步 RPC** | **内置 (CorrelationId+专用通道)** | 无 (需自行构建协议) | 需独立配置服务通道 | REQ/REP 套接字模式 | 无 |
| **等待策略** | **自适应 (Pause -> Yield -> Futex)** | 需配置 IdleStrategy | 轮询或信号量阻塞 | epoll 多路复用 | 互斥锁休眠或纯轮询 |
| **空闲 CPU 占用率** | **0.0%** | 轮询打满核心 / 休眠抖动 | 轮询打满核心 / 延迟抖动 | 0% | 0% |
| **跨平台支持** | **Windows & Linux 原生支持** | 跨平台 (驱动配置繁琐) | Linux 为主 (Windows 有限) | 跨平台 | 跨平台 |
| **独立守护进程依赖** | **无 (零守护进程)** | 需独立 Media Driver 进程 | 需独立 RouDi 管理进程 | 无 | 无 |
| **代码集成形态** | **直接引入 5 个源文件** | 依赖外部驱动与复杂构建链 | 依赖 C++ 框架与复杂配置 | 依赖动态库/静态库 | 依赖 Boost 模板库 |

---

## 4. Configuration Options (配置参数说明)

### 4.1 通道打开标志 (Channel Open Flags - OpenFlag)

用于 `ITeleport::Open` 函数，指定通道访问模式与创建行为：

| 配置标志 | 取值 | 说明 |
| :--- | :---: | :--- |
| `CH_LISTEN` | `0x01` | 订阅监听模式：启动后台拉取线程并通过 `OnMessage` 回调分发。 |
| `CH_SEND` | `0x02` | 发送模式：允许调用 `Send` 或 `AcquireBuffer` / `CommitBuffer`。 |
| `CH_CREATE_IF_NOEXIST` | `0x04` | 自动创建：若底层共享内存对象不存在则自动初始化创建。 |
| `CH_LISTEN_SEND` | `0x03` | 全双工模式：同时具备监听与发送能力（通道不存在时不自动创建）。 |
| `CH_ALL` | `0x07` | 全功能模式：具备监听、发送能力，且在通道不存在时自动创建。 |

### 4.2 流控与背压策略 (Channel Policies - ChannelPolicy)

通过 `ITeleport::SetChannelPolicy(nChannelId, ePolicy, nLagThreshold)` 动态调整：

| 策略枚举 | 枚举值 | 默认 | 行为描述与适用场景 |
| :--- | :---: | :---: | :--- |
| `POLICY_BLOCK` | `0` | **是** | **严格可靠投递**：发送端写入前检查所有消费者的读取进度；若环形空间不足，发送端自适应背压等待。适用于订单交易、控制信令等数据不可丢失场景。 |
| `POLICY_DROP_OLDEST` | `1` | 否 | **覆盖旧数据**：环形日志满时发送端直接覆盖最旧记录；落后的消费者自动重置游标至最新提交点，累加 `DropCount` 并触发 `MSG_DROPPED` 回调。适用于高频行情快照、传感器采样等场景。 |
| `POLICY_ISOLATE_SLOW_CONSUMER` | `2` | 否 | **慢消费者隔离**：监测消费者的落后滞后量（Lag），超过 `nLagThreshold` 字节时标记为隔离状态，不再阻塞发送端推流。适用于广播拓扑中隔离异常卡顿节点。 |

### 4.3 核心系统容量与限制常量

定义在 `typedefs.hpp` 中：

| 常量名称 | 默认值 | 作用说明 |
| :--- | :---: | :--- |
| `DEFAULT_SHM_SIZE` | `18 MB` | 单通道默认映射的共享内存大小（含 16MB 环形日志与元数据头部）。 |
| `MAX_SHM_SIZE` | `256 MB` | 单通道允许扩展的共享内存上限。 |
| `MAX_LOG_MESSAGE_SIZE` | `4 MB` | 单条变长消息的最大有效载荷（Payload）限制。 |
| `MAX_SUBSCRIBERS_PER_CHANNEL` | `2048` | 单通道允许并发注册的最大消费者数。 |
| `nLagThreshold` | `8 MB` | `POLICY_ISOLATE_SLOW_CONSUMER` 默认滞后隔离阈值。 |
| `MAX_NAME` | `128` 字节 | 通道主题名最大字符长度。 |
| `MAX_RPC_TOPIC_LEN` | `64` 字节 | 跨进程 RPC 服务主题名最大字符长度。 |

---

## 5. Build (编译与构建)

Teleport 支持两种引入方式：
1. **源码直接嵌入**：直接将 5 个核心源文件（`platform.hpp`, `platform.cpp`, `teleport.hpp`, `teleport.cpp`, `typedefs.hpp`）加入已有工程；
2. **CMake 构建**：通过标准 CMake 构建独立可执行文件或静态库。

### 5.1 各环境编译命令

#### Linux (Ubuntu 24.04 LTS / Debian / RHEL)
```bash
g++ -std=c++11 -O3 -Wall -Wextra -I. \
    platform.cpp teleport.cpp your_main.cpp -o your_app \
    -lpthread -lrt
```

#### Windows (MinGW-w64)
```bash
x86_64-w64-mingw32-g++ -DWindows=1 -O3 -Wall -Wextra -static -I. \
    platform.cpp teleport.cpp your_main.cpp -o your_app.exe \
    -lws2_32 -lshlwapi -lole32
```

#### Windows (MSVC / Visual Studio)
使用 Visual Studio 2019/2022 打开目录，CMakeLists.txt 将自动配置。或使用 MSVC 命令行：
```cmd
cl /O2 /EHsc /I. platform.cpp teleport.cpp your_main.cpp /Fe:your_app.exe ws2_32.lib shlwapi.lib ole32.lib
```

#### CMake 标准构建
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

---

## 6. Getting Started (开发示例)

### 6.1 基础发布与订阅 (Pub/Sub)

#### 订阅端 (Subscriber)
```cpp
#include "teleport.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace TLP;

RC OnMessage(PTCbMessage pMsg)
{
    if (!pMsg) return RC::INVALID_PARAM;
    if (pMsg->eType == MsgType::MSG_SUB_GET)
    {
        std::cout << "Received msg #" << pMsg->nOriginalMsgId 
                  << " from PID " << pMsg->nProcessId 
                  << ": " << std::string((char*)pMsg->pData, pMsg->nLength) 
                  << std::endl;
    }
    return RC::SUCCESS;
}

int main()
{
    T_ID channelId = 0;
    RC rc = ITeleport::Open("MarketTopic", CH_LISTEN | CH_CREATE_IF_NOEXIST, channelId, OnMessage);
    if (IS_FAILED(rc)) return 1;

    std::this_thread::sleep_for(std::chrono::seconds(10));
    ITeleport::Close(channelId, T_TRUE);
    return 0;
}
```

#### 发布端 (Publisher)
```cpp
#include "teleport.hpp"
#include <string>

using namespace TLP;

int main()
{
    T_ID channelId = 0;
    RC rc = ITeleport::Open("MarketTopic", CH_SEND | CH_CREATE_IF_NOEXIST, channelId, T_NULL);
    if (IS_FAILED(rc)) return 1;

    std::string data = "TICK: BTCUSDT Price: 95300.50 Volume: 12.8";
    rc = ITeleport::Send(channelId, data.data(), (T_UINT32)data.size());

    ITeleport::Close(channelId, T_TRUE);
    return 0;
}
```

### 6.2 零拷贝就地构造 (Zero-Copy Ring Buffer API)

在高频数据路径中，发送端可直接获取共享内存地址就地构造数据，避免临时堆分配与二次拷贝：

```cpp
T_UINT32 payloadSize = 65536; // 64 KB
T_PVOID pShmBuffer = nullptr;
T_UINT64 token = 0;

// 1. 申请连续内存切片
RC rc = ITeleport::AcquireBuffer(channelId, payloadSize, pShmBuffer, token);
if (IS_SUCCESS(rc))
{
    // 2. 原地填充数据
    SerializeData(pShmBuffer, payloadSize);

    // 3. 提交数据（写入内存屏障并更新写游标）
    ITeleport::CommitBuffer(channelId, token, payloadSize);
}
```

### 6.3 跨进程同步 RPC 调用 (Synchronous RPC)

```cpp
// 服务端：注册处理例程
RC PricingHandler(T_PCVOID pReq, T_UINT32 nReqLen, T_PVOID pResp, T_UINT32& nRespLen)
{
    std::string req((char*)pReq, nReqLen);
    std::string resp = "REPLY:" + req;
    memcpy(pResp, resp.data(), resp.size());
    nRespLen = (T_UINT32)resp.size();
    return RC::SUCCESS;
}
ITeleport::RegisterRpcService("PricingEngine", PricingHandler);

// 客户端：发起同步请求
char respBuf[1024];
T_UINT32 respLen = sizeof(respBuf);
RC rc = ITeleport::Call("PricingEngine", "QUERY_DATA", 10, respBuf, respLen, 1000);
if (IS_SUCCESS(rc))
{
    // 处理 respBuf
}
ITeleport::UnregisterRpcService("PricingEngine");
```

---

## 7. Automated Testing Suite (自动化测试验证)

Teleport 提供完整的自动化单元测试集，覆盖并发竞争、大包回绕、背压流控、RPC 调用及错误恢复路径。

执行命令：
```bash
# Linux
./teleport ut

# Windows
teleport.exe ut
```

---

## 8. Multi-Platform Benchmark Report (多环境基准测试报告)

### 8.1 测试架构与工作载荷

* **拓扑模型**：16 进程跨进程并发（4 个 Receiver 订阅进程 + 12 个 Sender 发布进程）
* **消息规模**：发送端并发推流 **20,000,000 条消息**；4 个订阅端全量接收消费，总计交付 **80,000,000 次**
* **硬件环境**：Intel Core i7-14700 (20 核心 / 28 逻辑线程，最高 5.40 GHz)，32GB 内存

---

### 8.2 多平台性能指标对比 (Benchmark Comparison)

| 性能测试指标 | Linux (Ubuntu 24.04 LTS 原生) | Windows 10/11 (MinGW / Win32 原生) | 说明 |
| :--- | :---: | :---: | :--- |
| **发布消息总量** | **20,000,000 条** | **20,000,000 条** | 12 个发送进程满额发送 (8×1.67M + 4×1.66M) |
| **接收交付总量** | **80,000,000 次** | **80,000,000 次** | 4 个接收进程全量消费 (每个接收 20M 条) |
| **总测试耗时 (Wall Time)** | **5.41 秒** | **6.58 秒** | 16 进程从推流至全量消费完毕总跨度 |
| **并发聚合发送速率** | **3,765,060 msg/s** (376.5万/s) | **3,038,129 msg/s** (303.8万/s) | 12 发送进程聚合推流带宽 |
| **并发聚合消费吞吐** | **15,074,430 交付/s** (1507万/s) | **12,193,263 交付/s** (1219万/s) | 4 订阅进程并行解析交付吞吐 |
| **单通道点对点极限** | **4,514,673 msg/s** (451.5万/s) | **4,514,673 msg/s** (451.5万/s) | 1 发送 + 1 接收纯流式传输 |
| **消息丢失率 (Loss Rate)** | **0.00% (0 条)** | **0.00% (0 条)** | 默认 `POLICY_BLOCK` 可靠模式保障 |
| **单通道顺序校验 (FIFO)** | **100% 严格保序** | **100% 严格保序** | 无锁原子位点分配单调递增 |
| **单进程平均常驻内存 (RSS)** | **~18.5 MB** | **~41.5 MB** | 连续物理内存映射，无动态堆扩展 |
| **空闲期 CPU 占用率** | **0.0%** | **0.0%** | 自适应混合等待进入内核事件休眠 |

---

### 8.3 端到端延迟分布评测 (Latency Distribution Benchmark)

在连续环形日志缓冲区与混合自适应等待策略下，采集 **50,000 次独立样本** 统计高精度纳秒级时钟延迟：

#### 单向端到端延迟 (One-Way Latency, Pub -> Sub)

| 运行环境 | Min (最小值) | P50 (中位数) | Mean (平均值) | P90 (90分位) | P99 (99分位) | P99.9 (99.9分位) | Max (最大值) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Ubuntu 24.04 LTS (原生)** | **106 ns** | **1,017 ns** (1.02 µs) | **1,133 ns** (1.13 µs) | **1,743 ns** | **5,068 ns** | **26.8 µs** | **30.0 µs** |
| **Windows 10/11 (Win32)** | **100 ns** | **300 ns** (0.30 µs) | **474 ns** (0.47 µs) | **1,000 ns** | **2,700 ns** | **8.0 µs** | **386.2 µs** |

#### 同步跨进程 RPC 往返时延 (Synchronous RPC Round-Trip Time)

| 运行环境 | Min (最小值) | P50 (中位数) | Mean (平均值) | P90 (90分位) | P99 (99分位) | P99.9 (99.9分位) | Max (最大值) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Ubuntu 24.04 LTS (原生)** | **14.88 µs** | **16.94 µs** | **17.67 µs** | **19.66 µs** | **28.52 µs** | **162.3 µs** | **430.8 µs** |
| **Windows 10/11 (Win32)** | **160.4 µs** | **175.5 µs** | **180.2 µs** | **194.2 µs** | **266.8 µs** | **510.4 µs** | **2.23 ms** |

---

### 8.4 系统资源利用走势 (Ubuntu 24.04 LTS 实时采样)

#### 系统 CPU 利用率时序走势 (28 逻辑线程)
```
  64.5% | ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄ ▄                                          
  56.6% | ██████████████████████████████████████████▄▄               
  48.7% | █████████████████████████████████████████████▄▄▄▄▄         
  40.8% | █████████████████████████████████████████████████████▄     
  33.0% | ██████████████████████████████████████████████████████▄    
  25.1% | █████████████████████████████████████████████████████████  
  17.2% | █████████████████████████████████████████████████████████▄ 
   9.3% |▄██████████████████████████████████████████████████████████ 
   1.4% |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0.0s        1.1s        2.2s        3.3s        4.4s   5.4s
```

#### 进程组常驻内存 (RSS) 时序走势
```
 314.2M | ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄██████████████████████████                
 274.9M | █████████████████████████████████████████████▄             
 235.6M | ██████████████████████████████████████████████████▄▄▄▄     
 196.4M | ██████████████████████████████████████████████████████     
 157.1M | ███████████████████████████████████████████████████████▄▄  
 117.8M | ██████████████████████████████████████████████████████████ 
  78.5M | ██████████████████████████████████████████████████████████ 
  39.3M | ██████████████████████████████████████████████████████████ 
   0.0M |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0.0s        1.1s        2.2s        3.3s        4.4s   5.4s
```

#### 资源采样详细阶段统计表

| 采样时间点 | 系统 CPU (%) | 等效计算核心 | 进程组总 RSS | 单进程均值 RSS | 活跃进程数 | 阶段说明 |
| :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **0.05s** | 7.0% | 0.00 核 | 17.1 MB | 3.4 MB | 5 | 接收端进程拉起与共享内存映射 |
| **0.46s** | 62.0% | 16.37 核 | 314.0 MB | 18.5 MB | 17 | 12 发送进程全部拉起，进入并发推流 |
| **1.27s** | 61.7% | 16.28 核 | 314.0 MB | 18.5 MB | 17 | 高并发饱和写入 (~376万 msg/s) |
| **2.50s** | 58.0% | 15.86 核 | 314.2 MB | 18.5 MB | 17 | 连续环形日志平稳回绕与数据提交 |
| **3.72s** | 57.7% | 16.17 核 | 314.2 MB | 18.5 MB | 17 | 4 订阅端并发拉取交付 (~1507万次/s) |
| **4.53s** | 47.2% | 13.21 核 | 255.1 MB | 18.2 MB | 14 | 部分发送端率先完成推送并退出 |
| **4.94s** | 44.1% | 11.94 核 | 235.5 MB | 18.1 MB | 13 | 12 发送端陆续完成 2000 万条推送 |
| **5.35s** | 20.6% | 5.36 核 | 138.5 MB | 17.3 MB | 8 | 4 订阅端完成全部 8000 万次消费交付 |
| **5.45s** | 1.4% | 0.00 核 | 0.0 MB | 0.0 MB | 1 | 资源完全释放，恢复空闲基线 |

---

## 9. License

Copyright (c) lonestep. All rights reserved.

Licensed under the [MIT](https://github.com/lonestep/teleport/blob/master/LICENSE) License.
