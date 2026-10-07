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
```
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

在高并发多进程场景下对 Teleport 进行压力与性能基准测试。测试采用 **4 个接收者进程（Subscribers）** 与 **12 个发送者进程（Publishers）**，在共享内存无锁环形队列模式下并发传输 **2,000,000 条消息**（4 个接收者全量订阅，总计消费 **8,000,000 次** 交付）。

### 1. 测试环境 (Test Environment)

| 规格分类 | 配置项 | 详细参数 |
| :--- | :--- | :--- |
| **操作系统** | OS & Kernel | Linux `6.12.0-271.el10.x86_64` |
| **处理器 (CPU)** | 型号规格 | **Intel(R) Core(TM) i7-14700** |
| | 核心架构 | 20 物理核心（8 P-Core + 12 E-Core），**28 逻辑线程** (最高 5.40 GHz) |
| | 高速缓存 | L1d: 768 KiB \| L1i: 1 MiB \| L2: 28 MiB \| L3: 33 MiB |
| **物理内存 (RAM)** | 总内存 / 可用内存 | **30.71 GB** / 21.10 GB |
| **测试架构** | 并发模型 | 4 Receivers + 12 Senders，16 进程跨进程并发 |

### 2. 核心性能指标 (Key Performance Indicators)

| 关键性能指标 | 测试统计值 | 评价与结论 |
| :--- | :--- | :--- |
| **发送消息总量** | **2,000,000 条** | 12 个发送进程全部成功发送 |
| **接收交付总量** | **8,000,000 条** | 4 个接收进程全部完整接收 |
| **消息丢失数 (Lost)** | **0 条 (0.00%)** | **零丢失 (Zero Loss)** |
| **消息乱序数 (Order Error)** | **0 处 (0.00%)** | **100% 严格保序 (Strict FIFO)** |
| **多进程传输跨度 (Span)** | **23.75 秒** | 持续高负荷传输阶段 |
| **并发聚合发送速率** | **84,196 msg/s** | 12 进程 CAS 并发争用吞吐 |
| **并发聚合消费吞吐** | **337,496 msg/s** | 4 进程并行拉取与校验交付 |

- **接收端表现**：4 个 Receiver 耗时均在 23.68s ~ 23.70s，各接收 2,000,000 条，单接收端速率为 **~84,400 msg/s**。
- **发送端表现**：12 个 Sender 耗时均在 23.14s ~ 23.75s，单发送端平均速率为 **~7,020 - 7,203 msg/s**，各进程负载均匀，无死锁或饥饿。

### 3. CPU 使用率曲线 (CPU Utilization Curve)

系统资源监控以 250ms 采样率连续采集（全机 28 逻辑线程）：
```
  23.0% |                                             ▄              
  20.4% |                                             █              
  17.7% |                                             █              
  15.1% |         ▄ ▄▄▄▄▄▄▄   ▄▄█▄█▄▄ ▄▄▄▄▄▄▄ ▄▄▄▄▄▄▄▄█ ▄   ▄▄▄ ▄▄▄▄▄
  12.4% |        ████████████████████████████████████████████████████
   9.8% |        ████████████████████████████████████████████████████
   7.1% |▄    ▄  ████████████████████████████████████████████████████
   4.5% |██▄▄▄█  ████████████████████████████████████████████████████
   1.8% |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0s         5s         10s        15s        20s        25s
```
- **CPU 平均占用**：全系统 CPU 平均使用率仅 **13.5%**（峰值 23.0%）。
- **等效核心数**：Teleport 16 进程组平均仅占用 **3.54 个物理核心**（峰值 4.35 核心），得益于混合自旋退避算法，避免了盲目死循环消耗 CPU。

### 4. 内存使用率曲线与稳定性 (Memory Usage & Stability)

Teleport 进程组物理常驻内存 (RSS) 随时间变化走势 (MB)：
```
 850.8M |           ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
 765.9M |         ███████████████████████████████████████████████████
 680.9M |        ████████████████████████████████████████████████████
 596.0M |        ████████████████████████████████████████████████████
 511.1M |        ████████████████████████████████████████████████████
 426.2M |        ████████████████████████████████████████████████████
 341.2M |     ███████████████████████████████████████████████████████
 256.3M |████████████████████████████████████████████████████████████
 171.4M |████████████████████████████████████████████████████████████
        +------------------------------------------------------------
          0s         5s         10s        15s        20s        25s
```

| 时间 (s) | 系统CPU (%) | Teleport等效核心 | 进程组总RSS (MB) | 单进程均值RSS (MB) | 系统总内存占用 (GB) |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **0.26s** | 6.4% | 0.54 核 | 276.2 MB | 25.1 MB | 9.70 GB |
| **2.29s** | 13.1% | 3.19 核 | 722.8 MB | 32.9 MB | 9.96 GB |
| **4.33s** | 14.2% | 4.03 核 | 845.5 MB | 38.4 MB | 9.95 GB |
| **8.41s** | 13.8% | 3.94 核 | 848.4 MB | 38.6 MB | 9.95 GB |
| **12.49s** | 13.5% | 3.81 核 | 848.4 MB | 38.6 MB | 9.96 GB |
| **16.56s** | 16.0% | 4.03 核 | 848.4 MB | 38.6 MB | 9.95 GB |
| **20.65s** | 23.0% | 4.21 核 | 848.8 MB | 38.6 MB | 10.01 GB |
| **24.73s** | 13.5% | 3.78 核 | 849.7 MB | 38.6 MB | 9.99 GB |
| **26.01s** | 6.8% | 0.00 核 | 171.4 MB | 85.7 MB | 9.66 GB (进程退出完全释放) |

- **零内存泄漏 (Zero Leak)**：在持续 20 秒、8,000,000 次高频读写冲击下，进程组内存自稳定期（845.5 MB）到结束（849.7 MB）抖动小于 4.2 MB（涨幅不足 0.5%），纯静态内存布局实现 **运行时 0 动态堆分配**。

# License

Copyright (c) lonestep. All rights reserved.
MIT许可版权声明

Licensed under the [MIT](https://github.com/lonestep/teleport/blob/master/LICENSE) License.
