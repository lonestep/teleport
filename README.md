[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/lonestep/teleport/blob/master/LICENSE) 

# Teleport

* The efficient, cross-platform, elegant, native C++ IPC(Inter-Process Communication) implementation that base on shared memory. 

* һ�������ͨѶ�ĸ�Ч����ƽ̨�������ġ����ڹ����ڴ�ı���C++ʵ�֡�

# Features

* Multiple senders and multiple recievers.
* Acknowledgement on failed message(s).
* Use without introducing dependencies.
* Efficient and elegant.
* Support all kinds of STL versions/old IDEs.

* �෢���ߺͽ����ߡ�
* ����ʧ��ȷ�ϡ�
* ���������������
* ��Ч������
* ֧�־ɰ汾����������STL�⡣

# Build
* You don't need to build it into library, use the 5 source files insdead:
  * platform.*
  * teleport.*
  * typedefs.hpp
* If you wanna build unittest, open teleport directory with Visual Studio, CMake 3.8+ is required.
* Or use cmake command on *Nix like system.

* �������ɿ⣬ֱ��������5��Դ�ļ���
  * platform.*
  * teleport.*
  * typedefs.hpp
* ����������ɵ�Ԫ���Գ�����Visual Studioֱ�Ӵ�teleportĿ¼��CMake���ɼ��ɡ���������ҪCMake 3.8���ϣ�
* ������unixϵͳ����cmake���ɡ�
# Getting Started

* Add the following files into your project:
  * platform.*
  * teleport.*
  * typedefs.hpp
* �������ļ��ӵ������Ŀ:
  * platform.*
  * teleport.*
  * typedefs.hpp
---
* Create a OnMessage callback to handle the notification(s), here's an example:
* ����һ��OnMessage�ص�������ص���Ϣ���������ӣ�
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
* �����д���ŵ���Ŀ����Ӧ�ط���
```cpp
    // Open the channel with CH_LISTEN flag in your listening process:
    // ����Ķ��Ľ�����CH_LISTEN��Ǵ�Ƶ��
    T_ID nChannelId = 0;
    //  strTopic: Arbitrary ansic string indicating the channel, no slash "\\", no more than MAX_NAME(128) characters.
    //  ref. to teleport.hpp for more.
    //  strTopic: ����ASCII�ַ�������������б��"\\"�����ܳ���128�ַ���
    //  �ο� teleport.hpp 
    RC rc = ITeleport::Open( strTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nChannelId,
        OnMessage,
        bGlobal);
    if(IS_FAILED(rc))
    {
      // Audit the failure ��¼ʧ��
    }
    
    // Now you can do your own things,  OnMessage will get called on messsage recieved
    // ��������Ը��Լ�������������Ϣ����ص�OnMessage���ᱻ����
    ...
    
    // Close Channel on your proces/thread exit
    // ����Ľ���/�߳��˳����ر�Ƶ��
    rc = ITeleport::Close(nChannelId, T_TRUE);
    
    
```
---
```
    // From the sending process, you open a channel with CH_SEND flag:
    // ���ͽ�������CH_SEND��Ǵ�һ��Ƶ��
    T_ID nChannelId = 0;
    RC rc = ITeleport::Open( strTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nChannelId,
        OnMessage,
        bGlobal);
    if(IS_SUCCESS(rc))
    {
      // You can call ITeleport::Send() multiple times for the same nChannelId
      // ����Ƶ��ID��������ظ�����ITeleport::Send()����Ƶ��������Ϣ
      rc = ITeleport::Send(nChannelId, (T_PVOID)strMsg.c_str(), (T_UINT32)strMsg.length());
    }
    ...
    // Remember to close the channel on process/thread exit
    // �����̡��߳��˳����ǵùر�Ƶ��
    rc = ITeleport::Close(nChannelId, T_TRUE);
```
# License

Copyright (c) lonestep. All rights reserved.
MIT��ɰ�Ȩ����

Licensed under the [MIT](https://github.com/lonestep/teleport/blob/master/LICENSE) License.
