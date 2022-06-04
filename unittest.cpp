#include <iostream>
#include "teleport.hpp"

using namespace TLP;
using namespace std;

T_UINT32                 g_nMsgRecieved    = 0;
T_UINT32                 g_nMixMsgRecieved = 0;
T_ID                     g_MixChannelId;
std::map<T_ID, T_MSG_ID> g_Proc2MsgId;


//
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
        if (pMessage->nChannelId == g_MixChannelId)
        {
            g_nMixMsgRecieved++;
        }
        else 
        {
            if (g_Proc2MsgId.find(pMessage->nProcessId) == g_Proc2MsgId.end())
            {
                g_Proc2MsgId[pMessage->nProcessId] = 0;
            }
            g_nMsgRecieved++;

            if (pMessage->nOriginalMsgId != g_Proc2MsgId[pMessage->nProcessId] + 1)
            {
                LogError("Inconsistency detected:LastMsg:%lld MsgId:%lld", 
                    g_Proc2MsgId[pMessage->nProcessId],
                    pMessage->nOriginalMsgId);
            }
            g_Proc2MsgId[pMessage->nProcessId] = pMessage->nOriginalMsgId;
        }
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


//
T_VOID Usage()
{
    printf("teleport [ut|at <testconf index>|listen <testconf index>|send <testconf index>]");
    exit(-1);
}


//
T_VOID RunProcessWithArg(T_PCSTR pPath, T_UINT32 nProcs, T_PCSTR pArg)
{
    RC rc;
    for (T_UINT32 i = 0; i < nProcs; i++)
    {
        rc = TShellRun(pPath, pArg);
        if (IS_FAILED(rc))
        {
            LogVital("TShellRun failed.");
        }
    }
}


//
T_VOID ListenMessageFromTopic(T_PCSTR  pTopic, T_UINT32 nTotalMsg, T_BOOL bGlobal = T_FALSE)
{
    T_ID nChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nChannelId,
        OnMessage,
        bGlobal);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    while (g_nMsgRecieved < nTotalMsg)
    {
        printf("Recieved: %d/%d\n", g_nMsgRecieved, nTotalMsg);
        if ((nTotalMsg - g_nMsgRecieved) < 10)
        {
            TSleep(1000);
            if (g_nMsgRecieved != nTotalMsg)
            {
                LogWarn("Test failed.");
                break;
            }
        }
        TSleep(1000);
    }
    rc = ITeleport::Close(nChannelId, T_TRUE);
    printf("Recieved:%d/%d\n", g_nMsgRecieved, nTotalMsg);
    LogInfo("Test ok.");
}


//
T_VOID SendMessageToTopic(T_PCSTR  pTopic, T_UINT32 nMsg, T_BOOL bGlobal = T_FALSE)
{
    T_ID nChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nChannelId,
        OnMessage,
        bGlobal);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_UINT32 nLoop = nMsg;
    T_STRING strMsg;
    while (nLoop--)
    {
        strMsg = "Message_";
        strMsg += std::to_string(GetCurrentProcessId());
        strMsg += "_";
        strMsg += std::to_string(nMsg - nLoop);
        rc = ITeleport::Send(nChannelId, (T_PVOID)strMsg.c_str(), (T_UINT32)strMsg.length());
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }
    rc = ITeleport::Close(nChannelId, T_TRUE);
}


//
T_VOID SendMessageToTopicAndListen(T_PCSTR  pTopic, 
    T_UINT32 nMsg, 
    T_UINT32 nTotalMsg, 
    T_BOOL bGlobal = T_FALSE)
{
    g_MixChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_SEND | CH_LISTEN | CH_CREATE_IF_NOEXIST,
        g_MixChannelId,
        OnMessage,
        bGlobal);

    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    T_UINT32 nProc = nTotalMsg / nMsg;
    CChannelMgr* pChannelMgr = &CChannelMgr::Instance();
    CChannel* pChannel = pChannelMgr->GetChannelById(g_MixChannelId);

    //Wait for all processes ready
    while ((T_USHORT)pChannel->GetSubscriberCount() < nProc)
    {
        TSleep(500);
    }
    
    T_UINT32 nLoop = nMsg;
    T_STRING strMsg;
    while (nLoop--)
    {
        strMsg = "Message_";
        strMsg += std::to_string(GetCurrentProcessId());
        strMsg += "_";
        strMsg += std::to_string(nMsg - nLoop);
        rc = ITeleport::Send(g_MixChannelId, (T_PVOID)strMsg.c_str(), (T_UINT32)strMsg.length());
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }
    while (g_nMixMsgRecieved < nTotalMsg)
    {
        printf("Recieved: %d/%d\n", g_nMixMsgRecieved, nTotalMsg);
        if ((nTotalMsg - g_nMixMsgRecieved) < 10)
        {
            TSleep(1000);
            if (g_nMixMsgRecieved != nTotalMsg)
            {
                LogWarn("Test failed.");
                break;
            }
        }
        TSleep(1000);
    }
    rc = ITeleport::Close(g_MixChannelId, T_TRUE);
    printf("Recieved:%d/%d\n", g_nMixMsgRecieved, nTotalMsg);
    LogInfo("Test ok.");
}


//
#define TEST_PLAIN_TEXT "Genius Shawn said:Feed face bee bacca!"
T_VOID UT_TestBFCrypto()
{

    T_UCHAR     szPlaintext[]                        = { TEST_PLAIN_TEXT };
    T_UINT32    nLen                                 = sizeof(szPlaintext);
    T_UCHAR     szCiphertext[sizeof(szPlaintext)]    = { 0 };
    T_UINT32    nLoop                                = 1000;

    RC r = CBFCrypto::Instance().Encrypt(szPlaintext, szCiphertext, nLen);
    SHOULD_BE_EQUAL(r, RC::INVALID_CALL);
    r = CBFCrypto::Instance().Decrypt(szPlaintext, szCiphertext, nLen);
    SHOULD_BE_EQUAL(r, RC::INVALID_CALL);

    CBFCrypto::Instance().SetKey(0xfeedfacebeebacca, 0xfacebaccafeedbee);
    
    do 
    {
        CBFCrypto::Instance().Encrypt(szPlaintext, szCiphertext, nLen);
        CBFCrypto::Instance().Decrypt(szCiphertext, szPlaintext, nLen);

    } while (nLoop--);

    T_BOOL b = (strcmp((T_PCCHAR)szPlaintext, TEST_PLAIN_TEXT) == 0);
    SHOULD_BE_TRUE(b);

}


//
RC ThreadProc(T_PVOID pParam)
{
    T_UINT32 nSeconds = *((T_PUINT32)pParam);
    TSleep(nSeconds);
    return RC::SUCCESS;
}


//
T_VOID UT_TestThread()
{
    Thread t;
    T_UINT32 nThreads = 20;
    RC rc = RC::FAILED;
    for (T_UINT32 i = 1; i <= nThreads; i++) 
    {
        rc = t.Create(ThreadProc, &i);
        BREAK_ON_FAILED(rc);
        rc = t.Start();
        BREAK_ON_FAILED(rc);
        rc = t.StopRunning();
        BREAK_ON_FAILED(rc);
        rc = t.Stop();
        BREAK_ON_FAILED(rc);
    }
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
}


//
typedef struct _TestConf 
{
    T_PCSTR  pTopic;
    T_UINT32 nMsgSend;
    T_UINT32 nListenProcs;
    T_UINT32 nSendProcs;
    T_UINT32 nListenAndSendProcs;
    T_BOOL   bGlobal;

}TestConf;


//
TestConf g_TestConfiguration[] =
{
    //Default test configuration
    {
        "some_topic",  // Topic string
        1000,          // Message count per process
        10,            // Listen process count
        10,            // Send process count
        30,            // Listen and send process count
        T_FALSE        // Indicating whether this is a global channel 
    }  

};

//
int main(int argc, char** argv)
{    

    Logger::Instance().SetLevel(LoggerType::LOG_WARNING);
    T_UINT32 nMaxConf = sizeof(g_TestConfiguration) / sizeof(TestConf);

    if (argc <= 1) 
    {
        Usage();
        return -1;
    }
    T_PCSTR pCmd = argv[1];
    if (argc == 2)
    {
        if (0 == _stricmp(pCmd, "ut"))
        {
            UT_TestBFCrypto();
            UT_TestThread();
            return 0;
        }
    }
    else if (argc == 3)
    {
        T_UINT32 nConfIndex = atoi(argv[2]);
        if (nConfIndex >= nMaxConf)
        {
            Usage();
            return -1;
        }
        T_PCSTR  pTopic         = g_TestConfiguration[nConfIndex].pTopic;
        T_UINT32 nMsgSend       = g_TestConfiguration[nConfIndex].nMsgSend;
        T_UINT32 nListenProcs   = g_TestConfiguration[nConfIndex].nListenProcs;
        T_UINT32 nSendProcs     = g_TestConfiguration[nConfIndex].nSendProcs;
        T_UINT32 nSendAndListen = g_TestConfiguration[nConfIndex].nListenAndSendProcs;
        T_BOOL   bGlobal        = g_TestConfiguration[nConfIndex].bGlobal;

        if (0 == _stricmp(pCmd, "at"))
        {
            std::string strListen     = "listen " + std::to_string(nConfIndex);
            std::string strSend       = "send " + std::to_string(nConfIndex);
            std::string strSendListen = "sendlisten " + std::to_string(nConfIndex);
            RunProcessWithArg(argv[0], nListenProcs, strListen.c_str());
            TSleep(1000 + nSendProcs * 100);
            RunProcessWithArg(argv[0], nSendProcs, strSend.c_str());
            RunProcessWithArg(argv[0], nSendAndListen, strSendListen.c_str());
            
            return 0;
        }
        else if (0 == _stricmp(pCmd, "listen"))
        {
            T_UINT32 nTotalMsg = nSendProcs * nMsgSend;
            ListenMessageFromTopic(pTopic, nTotalMsg, bGlobal);
            return 0;
        }
        else if (0 == _stricmp(pCmd, "send"))
        {
            SendMessageToTopic(pTopic, nMsgSend, bGlobal);
            return 0;
        }
        else if (0 == _stricmp(pCmd, "sendlisten"))
        {
            T_UINT32 nTotalMsg = nSendAndListen * nMsgSend;
            std::string strTopic = pTopic + std::string("_sl");
            SendMessageToTopicAndListen(strTopic.c_str(), nMsgSend, nTotalMsg, bGlobal);
            return 0;
        }
    }
    Usage();
    return -1;
}
