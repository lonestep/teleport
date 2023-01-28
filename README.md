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
# License

Copyright (c) lonestep. All rights reserved.
MIT许可版权声明

Licensed under the [MIT](https://github.com/lonestep/teleport/blob/master/LICENSE) License.
