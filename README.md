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
| **物理内存 (RAM)** | 总内存 / 可用内存 | **30.71 GB** / 20.64 GB |
| **测试架构** | 并发模型 | 4 Receivers + 12 Senders，16 进程跨进程并发 |

### 2. 核心性能指标 (Key Performance Indicators)

| 关键性能指标 | 测试统计值 | 评价与结论 |
| :--- | :--- | :--- |
| **发送消息总量** | **2,000,000 条** | 12 个发送进程全部成功发送 |
| **接收交付总量** | **8,000,000 条** | 4 个接收进程全部完整接收 |
| **消息丢失数 (Lost)** | **0 条 (0.00%)** | **零丢失 (Zero Loss)** |
| **消息乱序数 (Order Error)** | **0 处 (0.00%)** | **100% 严格保序 (Strict FIFO)** |
| **发送端传输跨度 (Span)** | **445 毫秒 (0.45 s)** | 12 发送者完成 200 万消息并发推送 |
| **接收端消费跨度 (Span)** | **502 毫秒 (0.50 s)** | 4 订阅者完成 800 万次消息拉取与回调分发 |
| **并发聚合发送速率** | **4,494,382 msg/s** | **~450 万 msg/s** (突破百万级设计指标) |
| **并发聚合消费吞吐** | **15,936,255 msg/s** | **~1593 万交付/秒** (跨进程并行读取交付) |

- **单对单极致压测（1 Pub + 1 Sub）**：2,000,000 条消息耗时 **343 ms**，单通道吞吐达到 **5,830,904 msg/s**（峰值瞬时超 840 万 msg/s）。
- **接收端表现**：4 个 Receiver 耗时约 500ms ~ 502ms，单订阅进程消费吞吐约为 **~3,990,000 msg/s**。
- **发送端表现**：12 个 Sender 耗时在 338ms ~ 445ms 之间，单进程平均发送速率达 **~374,000 - 493,000 msg/s**，负载极其均衡，无锁争用零饥饿。

### 3. CPU 使用率曲线 (CPU Utilization Curve)

系统资源监控时序走势（全机 28 逻辑线程）：
```
  46.6% |         █ 
  41.2% |        ▄█ 
  35.7% |        ██ 
  30.3% |        ██ 
  24.9% |        ██ 
  19.4% |        ██ 
  14.0% |        ██ 
   8.5% |█    ▄  ██ 
   3.1% |███████████
        +------------------------------------------------------------
          0.0s  0.5s  1.0s  1.5s  2.0s  2.5s
```
- **CPU 瞬态响应**：并发读写瞬间 CPU 算力迅速响应，短时间内处理完 800 万次分发后立即释放核心，能效比极高。

### 4. 内存使用率曲线与稳定性 (Memory Usage & Stability)

Teleport 进程组物理常驻内存 (RSS) 随时间变化走势 (MB)：
```
 902.1M |        █  
 813.9M |        █  
 725.6M |        █▄ 
 637.4M |        ██ 
 549.2M |        ██ 
 461.0M |        ██ 
 372.8M |     ▄████ 
 284.5M |▄█████████ 
 196.3M |███████████
        +------------------------------------------------------------
          0.0s  0.5s  1.0s  1.5s  2.0s  2.5s
```

- **零内存泄漏 (Zero Leak)**：全过程基于静态无锁环形队列与原地就地构造，**运行时动态堆分配完全为 0**，测试结束后 16 个进程内存完全释放回操作系统。

# License

Copyright (c) lonestep. All rights reserved.
MIT许可版权声明

Licensed under the [MIT](https://github.com/lonestep/teleport/blob/master/LICENSE) License.
