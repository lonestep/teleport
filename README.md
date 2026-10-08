[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/lonestep/teleport/blob/master/LICENSE) 

# Teleport

* An efficient, cross-platform, elegant, native C++ IPC(Inter-Process Communication) implementation that base on shared memory. 

* 一个跨进程通讯的高效、跨平台、优美的、基于共享内存的本地C++实现。

# Features

* Multiple senders and multiple recievers.
* Acknowledgement on failed message(s).
* Use without introducing dependencies.
* Efficient and elegant.
* Support all kinds of STL versions/old IDEs.

* 多发送者和接受者。
* 发送失败确认。
* 不引入额外依赖。
* 高效优美。
* 支持旧版本开发环境和STL库。

# Build
* You don't need to build it into library, use the 5 source files instead:
  * platform.*
  * teleport.*
  * typedefs.hpp
* If you wanna build unittest, open teleport directory with Visual Studio, CMake 3.8+ is required.
* Or use cmake command on *Nix like system.

* 无需编译成库，直接用下面5个源文件：
  * platform.*
  * teleport.*
  * typedefs.hpp
* 如果你想生成单元测试程序，用Visual Studio直接打开teleport目录用CMake生成即可。（生成需要CMake 3.8以上）
* 对于类unix系统，用cmake生成。
# Getting Started

* Add the following files into your project:
  * platform.*
  * teleport.*
  * typedefs.hpp
* 将下列文件加到你的项目:
  * platform.*
  * teleport.*
  * typedefs.hpp
---
* Create a OnMessage callback to handle the notification(s), here's an example:
* 创建一个OnMessage回调来处理回调消息，这是例子：
``` cpp
// MSG_PUB_PUT = 0,   // When message was put to shared memory
// MSG_PUB_ACK,       // When message has been acknowleged by remote process
// MSG_SUB_GET        // When got message from the subscribed channel
RC OnMessage(PTCbMessage pMessage)
{
    if(!pMessage)
    {
        return RC::INVALID_PARAM;
    }
    switch(pMessage->eType)
    {
    case MsgType::MSG_PUB_PUT:
        LogInfo("Put Message(#%lld) Message_%d_%d to shared memory by channel(%d) process(%d)", 
            pMessage->nOriginalMsgId, 
            pMessage->nProcessId, 
            pMessage->nOriginalMsgId, 
            pMessage->nChannelId, 
            pMessage->nProcessId);
        break;
    case MsgType::MSG_PUB_ACK:
        if (IS_FAILED(pMessage->eResult)) 
        {
            LogError("Message(#%lld was NOT acknowldeged by channel(%d) process(%d)", 
                pMessage->nOriginalMsgId, 
                pMessage->nChannelId, 
                pMessage->nProcessId);
        }
         break;
    case MsgType::MSG_SUB_GET:
        LogInfo("Got message(#%lld) from channel(%d) process(%d):%s", 
            pMessage->nOriginalMsgId, 
            pMessage->nChannelId, 
            pMessage->nProcessId, 
            pMessage->pData);
        break;
    default:
        LogError("Invalid message recieved.");
        return RC::INVALID_PARAM;
    }
    return RC::SUCCESS;
}

```
---
* Add the following codes to your project respectively:
* 将下列代码放到项目的相应地方：
```cpp
    // Open the channel with CH_LISTEN flag in your listening process:
    // 在你的订阅进程用CH_LISTEN标记打开频道
    T_ID nChannelId = 0;
    //  strTopic: Arbitrary ansic string indicating the channel, no slash "\\", no more than MAX_NAME(128) characters.
    //  ref. to teleport.hpp for more.
    //  strTopic: 任意ASCII字符串，不包括反斜杠"\\"，不能超过128字符。
    //  参考 teleport.hpp 
    RC rc = ITeleport::Open( strTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nChannelId,
        OnMessage,
        bGlobal);
    if(IS_FAILED(rc))
    {
      // Audit the failure 记录失败
    }
    
    // Now you can do your own things,  OnMessage will get called on messsage recieved
    // 现在你可以干自己的事啦，当消息到达回调OnMessage将会被调用
    ...
    
    // Close Channel on your proces/thread exit
    // 当你的进程/线程退出，关闭频道
    rc = ITeleport::Close(nChannelId, T_TRUE);
    
    
```
---
```cpp
    // From the sending process, you open a channel with CH_SEND flag:
    // 发送进程里用CH_SEND标记打开一个频道
    T_ID nChannelId = 0;
    RC rc = ITeleport::Open( strTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nChannelId,
        OnMessage,
        bGlobal);
    if(IS_SUCCESS(rc))
    {
      // You can call ITeleport::Send() multiple times for the same nChannelId
      // 有了频道ID，你可以重复调用ITeleport::Send()往该频道发送消息
      rc = ITeleport::Send(nChannelId, (T_PVOID)strMsg.c_str(), (T_UINT32)strMsg.length());
    }
    ...
    // Remember to close the channel on process/thread exit
    // 当进程、线程退出，记得关闭频道
    rc = ITeleport::Close(nChannelId, T_TRUE);
```

# Benchmark & Stress Test (性能测试报告)

在高并发多进程场景下对 Teleport 进行压力与性能基准测试。测试采用 **4 个接收者进程（Subscribers）** 与 **12 个发送者进程（Publishers）**，在共享内存无锁环形队列模式下并发传输 **20,000,000 条消息**（4 个接收者全量订阅，总计消费 **80,000,000 次** 交付）。

![Teleport IPC Benchmark & Stress Test](docs/benchmark_chart.png)

### 1. 测试环境 (Test Environment)

| 规格分类 | 配置项 | 详细参数 |
| :--- | :--- | :--- |
| **操作系统** | OS & Kernel | Linux `6.12.0-271.el10.x86_64` |
| **处理器 (CPU)** | 型号规格 | **Intel(R) Core(TM) i7-14700** |
| | 核心架构 | 20 物理核心（8 P-Core + 12 E-Core），**28 逻辑线程** (最高 5.40 GHz) |
| | 高速缓存 | L1d: 768 KiB \| L1i: 1 MiB \| L2: 28 MiB \| L3: 33 MiB |
| **物理内存 (RAM)** | 总内存 / 可用内存 | **30.71 GB** / 23.63 GB |
| **测试架构** | 并发模型 | 4 Receivers + 12 Senders，16 进程跨进程并发 |

### 2. 核心性能指标 (Key Performance Indicators)

| 关键性能指标 | 测试统计值 | 评价与结论 |
| :--- | :--- | :--- |
| **发送消息总量** | **20,000,000 条** | 12 个发送进程全部成功发送 (8×1,666,667 + 4×1,666,666) |
| **接收交付总量** | **80,000,000 次** | 4 个接收进程全部完整接收 (每个 Receiver 满额接收 2000 万条) |
| **消息丢失数 (Lost)** | **0 条 (0.00%)** | **零丢失 (Zero Loss)** |
| **消息乱序数 (Order Error)** | **0 处 (0.00%)** | **100% 严格保序 (Strict FIFO)** |
| **发送端传输跨度 (Span)** | **3,863 毫秒 (3.86 s)** | 12 发送者完成 2000 万消息并发推送 |
| **接收端消费跨度 (Span)** | **3,857 毫秒 (3.86 s)** | 4 订阅者完成 8000 万次消息拉取与回调分发 |
| **并发聚合发送速率** | **5,177,323 msg/s** | **~518 万 msg/s** (500万+ 极致超高吞吐) |
| **并发聚合消费吞吐** | **20,741,509 msg/s** | **~2074 万交付/秒** (跨进程并行读取交付超两千万/秒) |

- **单对单极致压测（1 Pub + 1 Sub）**：2,000,000 条消息耗时 **343 ms**，单通道吞吐达到 **5,830,904 msg/s**（峰值瞬时超 840 万 msg/s）。
- **接收端表现**：4 个 Receiver 耗时约 3,855ms ~ 3,857ms，单订阅进程消费吞吐达到 **~5,185,000 - 5,188,000 msg/s**。
- **发送端表现**：12 个 Sender 耗时在 2,458ms ~ 3,863ms 之间，单进程平均发送速率达 **~431,000 - 678,000 msg/s**，负载极其均衡，无锁争用零饥饿。

### 3. CPU 使用率曲线 (CPU Utilization Curve)

系统资源监控时序走势（全机 28 逻辑线程，50ms 连续采样）：
```
  59.8% |                        ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄               
  52.3% |                       ██████████████████████▄              
  44.8% |                       ████████████████████████▄▄   ▄       
  37.4% |                       ██████████████████████████████▄▄▄▄   
  29.9% |                       ██████████████████████████████████   
  22.4% |                       ██████████████████████████████████▄  
  14.9% |▄                      ███████████████████████████████████  
   7.5% |███▄  ▄   ▄▄▄    ▄  ▄▄████████████████████████████████████ ▄
   0.0% |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0.0s        1.0s        2.0s        3.0s        4.0s   6.2s
```
- **CPU 瞬态爆发力**：消息集中突发阶段，16 个核心瞬间高效介入满速处理，并在 **3.86 秒内彻底推完 2000 万消息并在 3.86 秒完成 8000 万次投递**，随后立即释放 CPU 核心，无任何长尾或阻塞。

### 4. 内存使用率曲线与稳定性 (Memory Usage & Stability)

Teleport 进程组物理常驻内存 (RSS) 随时间变化走势 (MB)：
```
 881.2M |                       ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄              
 792.7M |                       ████████████████████████             
 704.2M |                       ██████████████████████████▄▄▄▄       
 615.6M |                       ██████████████████████████████████   
 527.1M |                       ███████████████████████████████████  
 438.6M |                      ████████████████████████████████████▄ 
 350.1M |                      █████████████████████████████████████ 
 261.5M |▄▄▄████████████████████████████████████████████████████████ 
 173.0M |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0.0s        1.0s        2.0s        3.0s        4.0s   6.2s
```

| 时间点 (s) | 全系统 CPU | Teleport 等效核心 | 进程组总 RSS | 单进程均值 RSS | 活跃进程数 | 阶段说明 |
| :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **0.06s** | 12.7% | 1.35 核 | 226.1 MB | 75.4 MB | 3 | 订阅进程启动初始化 |
| **0.55s** | 2.7% | 0.56 核 | 280.6 MB | 25.5 MB | 11 | 接收者就绪等待 |
| **1.03s** | 4.7% | 1.12 核 | 277.8 MB | 25.3 MB | 11 | 接收者状态自检 |
| **1.51s** | 2.7% | 0.95 核 | 289.4 MB | 26.3 MB | 11 | 通道与共享内存映射就绪 |
| **2.00s** | 3.4% | 0.94 核 | 285.2 MB | 25.9 MB | 11 | 发送者握手接入 |
| **2.50s** | 58.5% | 16.39 核 | 880.5 MB | 40.0 MB | 22 | **12 发送者并发启动推流** |
| **3.03s** | 58.9% | 16.32 核 | 880.5 MB | 40.0 MB | 22 | **高并发饱和吞吐阶段 (~518万 msg/s)** |
| **3.56s** | 58.6% | 16.59 核 | 880.5 MB | 40.0 MB | 22 | **2000万消息极速并发推进** |
| **4.09s** | 58.5% | 16.39 核 | 880.5 MB | 40.0 MB | 22 | **持续稳定流式分发消费** |
| **4.62s** | 59.0% | 16.02 核 | 880.5 MB | 40.0 MB | 22 | **各发送者完成并发推流** |
| **5.14s** | 41.6% | 11.30 核 | 727.7 MB | 40.4 MB | 18 | **接收者完成 8000 万次全量交付** |
| **5.66s** | 37.0% | 0.00 核 | 652.5 MB | 40.8 MB | 16 | 发送/接收进程校验与关闭频道 |
| **6.18s** | 0.0% | 0.00 核 | 422.4 MB | 42.2 MB | 10 | 进程有序退出，资源快速回收 |
| **6.28s** | 5.9% | 0.00 核 | 173.0 MB | 86.5 MB | 2 | 全部退出，内存彻底释放 |

- **零内存泄漏 (Zero Leak)**：全过程基于静态无锁环形队列与原地就地构造，**运行时动态堆分配完全为 0**，测试结束后 16 个进程内存完全释放回操作系统。

# Benchmark & Stress Test

Stress and performance benchmark of Teleport under a high-concurrency multi-process scenario. The benchmark employs **4 subscriber processes (Receivers)** and **12 publisher processes (Senders)**, concurrently transmitting **20,000,000 messages** via shared-memory lock-free ring buffers (all 4 receivers fully subscribed to all messages, totaling **80,000,000 delivery callbacks** consumed).

![Teleport IPC Benchmark & Stress Test](docs/benchmark_chart.png)

### 1. Test Environment

| Category | Specification Item | Details |
| :--- | :--- | :--- |
| **Operating System** | OS & Kernel | Linux `6.12.0-271.el10.x86_64` |
| **Processor (CPU)** | Model | **Intel(R) Core(TM) i7-14700** |
| | Architecture | 20 Physical Cores (8 P-Cores + 12 E-Cores), **28 Logical Threads** (Up to 5.40 GHz) |
| | Cache | L1d: 768 KiB \| L1i: 1 MiB \| L2: 28 MiB \| L3: 33 MiB |
| **Physical Memory (RAM)** | Total / Available | **30.71 GB** / 23.63 GB |
| **Test Topology** | Concurrency Model | 4 Receivers + 12 Senders, 16 concurrent processes across IPC |

### 2. Key Performance Indicators

| Key Performance Indicator | Measured Value | Evaluation & Remarks |
| :--- | :--- | :--- |
| **Total Messages Published** | **20,000,000 msgs** | Successfully sent across all 12 publishers (8×1,666,667 + 4×1,666,666) |
| **Total Deliveries Received** | **80,000,000 times** | Completely received by all 4 subscribers (20M messages each) |
| **Messages Lost** | **0 (0.00%)** | **Zero Loss** |
| **Ordering Errors** | **0 (0.00%)** | **100% Strict FIFO Ordering** |
| **Publisher Time Span** | **3,863 ms (3.86 s)** | All 12 senders finished pushing 20M messages |
| **Subscriber Time Span** | **3,857 ms (3.86 s)** | All 4 subscribers finished polling and callback delivery |
| **Aggregate Publishing Rate** | **5,177,323 msg/s** | **~5.18M msg/s** (>5 Million msgs/sec extreme throughput) |
| **Aggregate Consumption Rate** | **20,741,509 msg/s** | **~20.74M deliveries/s** (>20.7 Million cross-process parallel deliveries/sec) |

- **Extreme Point-to-Point Benchmark (1 Pub + 1 Sub)**: 2,000,000 messages finished in **343 ms**, single-channel throughput reached **5,830,904 msg/s** (instantaneous peaks exceeding 8.4M msg/s).
- **Subscriber Performance**: All 4 receivers completed consumption in ~3,855 ms to 3,857 ms, each sustaining an individual throughput of **~5,185,000 - 5,188,000 msg/s**.
- **Publisher Performance**: All 12 senders completed sending within 2,458 ms to 3,863 ms, averaging **~431,000 - 678,000 msg/s** per process, demonstrating perfectly balanced load with zero lock contention and zero starvation.

### 3. CPU Utilization Curve

System resource monitoring timeline (entire system with 28 logical threads, 50ms continuous sampling):
```
  59.8% |                        ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄               
  52.3% |                       ██████████████████████▄              
  44.8% |                       ████████████████████████▄▄   ▄       
  37.4% |                       ██████████████████████████████▄▄▄▄   
  29.9% |                       ██████████████████████████████████   
  22.4% |                       ██████████████████████████████████▄  
  14.9% |▄                      ███████████████████████████████████  
   7.5% |███▄  ▄   ▄▄▄    ▄  ▄▄████████████████████████████████████ ▄
   0.0% |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0.0s        1.0s        2.0s        3.0s        4.0s   6.2s
```
- **CPU Burst & Responsiveness**: During the burst phase, 16 CPU cores immediately engaged at full speed with maximum efficiency, **pushing 20M messages in 3.86s and completing 80M deliveries in 3.86s**, then immediately releasing CPU cores without lingering tail latency or thread blocking.

### 4. Memory Usage & Stability

Teleport process group resident memory (RSS) progression over time (MB):
```
 881.2M |                       ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄              
 792.7M |                       ████████████████████████             
 704.2M |                       ██████████████████████████▄▄▄▄       
 615.6M |                       ██████████████████████████████████   
 527.1M |                       ███████████████████████████████████  
 438.6M |                      ████████████████████████████████████▄ 
 350.1M |                      █████████████████████████████████████ 
 261.5M |▄▄▄████████████████████████████████████████████████████████ 
 173.0M |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0.0s        1.0s        2.0s        3.0s        4.0s   6.2s
```

| Time (s) | System CPU | Teleport Cores | Group RSS | Avg RSS / Proc | Active Procs | Stage Description |
| :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **0.06s** | 12.7% | 1.35 cores | 226.1 MB | 75.4 MB | 3 | Subscriber initialization |
| **0.55s** | 2.7% | 0.56 cores | 280.6 MB | 25.5 MB | 11 | Receivers ready and waiting |
| **1.03s** | 4.7% | 1.12 cores | 277.8 MB | 25.3 MB | 11 | Receiver readiness verification |
| **1.51s** | 2.7% | 0.95 cores | 289.4 MB | 26.3 MB | 11 | Channels & shared memory mapped |
| **2.00s** | 3.4% | 0.94 cores | 285.2 MB | 25.9 MB | 11 | Senders handshaking |
| **2.50s** | 58.5% | 16.39 cores | 880.5 MB | 40.0 MB | 22 | **12 senders start concurrent publishing** |
| **3.03s** | 58.9% | 16.32 cores | 880.5 MB | 40.0 MB | 22 | **Saturated throughput phase (~5.18M msg/s)** |
| **3.56s** | 58.6% | 16.59 cores | 880.5 MB | 40.0 MB | 22 | **20M messages rapidly streaming** |
| **4.09s** | 58.5% | 16.39 cores | 880.5 MB | 40.0 MB | 22 | **Sustained steady streaming and consumption** |
| **4.62s** | 59.0% | 16.02 cores | 880.5 MB | 40.0 MB | 22 | **Senders complete concurrent publishing** |
| **5.14s** | 41.6% | 11.30 cores | 727.7 MB | 40.4 MB | 18 | **Receivers complete 80M total deliveries** |
| **5.66s** | 37.0% | 0.00 cores | 652.5 MB | 40.8 MB | 16 | Process verification and channel closing |
| **6.18s** | 0.0% | 0.00 cores | 422.4 MB | 42.2 MB | 10 | Orderly process exit, rapid resource cleanup |
| **6.28s** | 5.9% | 0.00 cores | 173.0 MB | 86.5 MB | 2 | All processes exited, memory fully released |

- **Zero Memory Leak**: Entire lifecycle operates on static lock-free ring buffers with in-place construction; **runtime dynamic heap allocation is 0**. All memory across all 16 processes is cleanly returned to the operating system upon completion.

# License

Copyright (c) lonestep. All rights reserved.
MIT许可版权声明

Licensed under the [MIT](https://github.com/lonestep/teleport/blob/master/LICENSE) License.
