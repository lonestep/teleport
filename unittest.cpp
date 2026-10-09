#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <numeric>
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
    printf("teleport [ut|stress <msg_count>|mp_listen <total_msgs> [listener_id]|mp_send <msg_count> <sender_id> [min_subs]|at <testconf index>|listen <testconf index>|send <testconf index>]\n");
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
        strMsg += std::to_string(TGetProcId());
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
        strMsg += std::to_string(TGetProcId());
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
T_VOID UT_TestSetUnreadHoles()
{
    CChannelData channelData("test_unread_holes", DEFAULT_SHM_SIZE);
    TPChannelShmHeader pHeader = channelData.GetShmHeader();
    SHOULD_BE_TRUE((pHeader != T_NULL));

    channelData.LockHdr();
    memset((T_PVOID)pHeader->AckRecords, 0, sizeof(TAckRecord) * MAX_SUBSCRIBERS_PER_CHANNEL);
    pHeader->AckRecords[0].ProcId = 0;
    pHeader->AckRecords[1].ProcId = 1001;
    pHeader->AckRecords[1].AckFlag = ACK_FLAG::DONE;
    pHeader->AckRecords[2].ProcId = 0;
    pHeader->AckRecords[3].ProcId = 1002;
    pHeader->AckRecords[3].AckFlag = ACK_FLAG::DONE;
    pHeader->nSubscribers = 2;
    channelData.UnlockHdr();

    RC rc = channelData.SetUnread(10, 888);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    SHOULD_BE_EQUAL(pHeader->nOriginalMsgId, 10);
    SHOULD_BE_EQUAL(pHeader->nOriginalProcId, 888);
    SHOULD_BE_EQUAL(pHeader->nUnreadCnt, 2);
    SHOULD_BE_TRUE((pHeader->AckRecords[1].AckFlag == ACK_FLAG::INIT));
    SHOULD_BE_TRUE((pHeader->AckRecords[3].AckFlag == ACK_FLAG::INIT));

    T_BOOL bDone = channelData.SetRead();
    SHOULD_BE_TRUE(!bDone);
    SHOULD_BE_EQUAL(pHeader->nUnreadCnt, 1);

    bDone = channelData.SetRead();
    SHOULD_BE_TRUE(bDone);
    SHOULD_BE_EQUAL(pHeader->nUnreadCnt, 0);

    channelData.LockHdr();
    memset((T_PVOID)pHeader->AckRecords, 0, sizeof(TAckRecord) * MAX_SUBSCRIBERS_PER_CHANNEL);
    pHeader->nSubscribers = 0;
    channelData.UnlockHdr();

    rc = channelData.SetUnread(11, 888);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    SHOULD_BE_EQUAL(pHeader->nUnreadCnt, 0);
}


//
T_VOID UT_TestGetFirstAvailRecord()
{
    CChannelData channelData("test_avail_record", DEFAULT_SHM_SIZE);
    TPChannelShmHeader pHeader = channelData.GetShmHeader();
    SHOULD_BE_TRUE((pHeader != T_NULL));

    channelData.LockHdr();
    memset((T_PVOID)pHeader->AckRecords, 0, sizeof(TAckRecord) * MAX_SUBSCRIBERS_PER_CHANNEL);
    pHeader->AckRecords[0].ProcId = 501;
    pHeader->AckRecords[1].ProcId = 0;
    pHeader->AckRecords[2].ProcId = 502;
    channelData.UnlockHdr();

    channelData.LockHdr();
    TPAckRecord pRec = channelData.GetFirstAvailRecord();
    SHOULD_BE_TRUE((pRec == &pHeader->AckRecords[1]));
    pRec->ProcId = 503;
    channelData.UnlockHdr();

    channelData.LockHdr();
    pRec = channelData.GetFirstAvailRecord();
    SHOULD_BE_TRUE((pRec == &pHeader->AckRecords[3]));
    channelData.UnlockHdr();
}


//
T_VOID UT_TestChannelGuidConsistency()
{
    T_PCSTR pTestTopic = "ut_guid_topic";
    CChannelMgr* pMgr = &CChannelMgr::Instance();
    CChannel* pCh1 = pMgr->CreateChannel(pTestTopic, T_FALSE);
    SHOULD_BE_TRUE((pCh1 != T_NULL));
    T_STRING guid1 = pCh1->GetChannelGuid();
    SHOULD_BE_TRUE((!guid1.empty()));

    CChannel* pCh2 = pMgr->GetChannelByName(pTestTopic);
    SHOULD_BE_TRUE((pCh2 != T_NULL));
    SHOULD_BE_EQUAL(pCh1->GetChannelId(), pCh2->GetChannelId());
    SHOULD_BE_TRUE((guid1 == pCh2->GetChannelGuid()));
}


//
static volatile T_UINT32 g_nDeliveryRecvCount = 0;
static std::vector<T_MSG_ID> g_vDeliveredMsgIds;

static RC DeliveryTestCallback(PTCbMessage pMessage)
{
    if (!pMessage)
    {
        return RC::INVALID_PARAM;
    }
    if (pMessage->eType == MsgType::MSG_SUB_GET)
    {
        g_nDeliveryRecvCount++;
        g_vDeliveredMsgIds.push_back(pMessage->nOriginalMsgId);
    }
    return RC::SUCCESS;
}


//
T_VOID UT_TestMessageDeliveryNoLoss()
{
    g_nDeliveryRecvCount = 0;
    g_vDeliveredMsgIds.clear();

    T_PCSTR pTopic = "ut_delivery_topic";
    T_ID nSubChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nSubChannelId,
        DeliveryTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_ID nPubChannelId = 0;
    rc = ITeleport::Open(pTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nPubChannelId,
        DeliveryTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    const T_UINT32 nTestMsgCount = 20;
    for (T_UINT32 i = 1; i <= nTestMsgCount; i++)
    {
        std::string strMsg = "TestPayload_" + std::to_string(i);
        rc = ITeleport::Send(nPubChannelId, (T_PCVOID)strMsg.c_str(), (T_UINT32)strMsg.length());
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }

    T_UINT32 nWaitLoops = 100;
    while (g_nDeliveryRecvCount < nTestMsgCount && nWaitLoops--)
    {
        TSleep(50);
    }

    rc = ITeleport::Close(nPubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nDeliveryRecvCount, nTestMsgCount);
    SHOULD_BE_EQUAL(g_vDeliveredMsgIds.size(), nTestMsgCount);
    for (T_UINT32 i = 0; i < nTestMsgCount; i++)
    {
        SHOULD_BE_EQUAL(g_vDeliveredMsgIds[i], (T_MSG_ID)(i + 1));
    }
}


//
static volatile T_UINT32 g_nZeroCopyRecvCount = 0;
static std::string g_strZeroCopyLastMsg = "";

static RC ZeroCopyTestCallback(PTCbMessage pMessage)
{
    if (!pMessage)
    {
        return RC::INVALID_PARAM;
    }
    if (pMessage->eType == MsgType::MSG_SUB_GET)
    {
        g_nZeroCopyRecvCount++;
        if (pMessage->pData && pMessage->nLength > 0)
        {
            g_strZeroCopyLastMsg = std::string((const char*)pMessage->pData, pMessage->nLength);
        }
    }
    return RC::SUCCESS;
}

T_VOID UT_TestZeroCopy()
{
    g_nZeroCopyRecvCount = 0;
    g_strZeroCopyLastMsg = "";

    T_PCSTR pTopic = "ut_zerocopy_topic";
    T_ID nSubChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nSubChannelId,
        ZeroCopyTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_ID nPubChannelId = 0;
    rc = ITeleport::Open(pTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nPubChannelId,
        ZeroCopyTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_PVOID pBuffer = T_NULL;
    T_UINT64 nToken = 0;
    std::string testData = "In-Place Zero-Copy Message Content!";

    rc = ITeleport::AcquireBuffer(nPubChannelId, (T_UINT32)testData.length(), pBuffer, nToken);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    SHOULD_BE_TRUE((pBuffer != T_NULL));

    memcpy(pBuffer, testData.c_str(), testData.length());

    rc = ITeleport::CommitBuffer(nPubChannelId, nToken, (T_UINT32)testData.length());
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_UINT32 nWait = 50;
    while (g_nZeroCopyRecvCount < 1 && nWait--)
    {
        TSleep(20);
    }

    rc = ITeleport::Close(nPubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nZeroCopyRecvCount, 1);
    SHOULD_BE_TRUE((g_strZeroCopyLastMsg == testData));
}


//
T_VOID UT_TestZombieCleanup()
{
    CChannelData channelData("test_zombie_cleanup", DEFAULT_SHM_SIZE);
    TPChannelShmHeader pHeader = channelData.GetShmHeader();
    SHOULD_BE_TRUE((pHeader != T_NULL));

    channelData.LockHdr();
    memset((T_PVOID)pHeader->AckRecords, 0, sizeof(TAckRecord) * MAX_SUBSCRIBERS_PER_CHANNEL);
    pHeader->AckRecords[0].ProcId = 99999999;
    pHeader->AckRecords[0].AckFlag = ACK_FLAG::INIT;
    pHeader->AckRecords[0].LastReadSeq = 0;
    pHeader->AckRecords[1].ProcId = TGetProcId();
    pHeader->AckRecords[1].AckFlag = ACK_FLAG::INIT;
    pHeader->AckRecords[1].LastReadSeq = 10;
    pHeader->nSubscribers = 2;
    channelData.UnlockHdr();

    channelData.CleanZombieSubscribers();

    channelData.LockHdr();
    SHOULD_BE_EQUAL(pHeader->AckRecords[0].ProcId, 0);
    SHOULD_BE_EQUAL(pHeader->AckRecords[1].ProcId, TGetProcId());
    SHOULD_BE_EQUAL(pHeader->nSubscribers, 1);
    channelData.UnlockHdr();

    T_UINT64 minSeq = channelData.GetMinSubscriberSequence();
    SHOULD_BE_EQUAL(minSeq, 10);
}


//
T_VOID UT_TestCRCIntegrity()
{
    const char* pMsg = "Hello Teleport Integrity Check!";
    T_UINT32 nLen = (T_UINT32)strlen(pMsg);
    T_UINT32 crc1 = TComputeCRC32(pMsg, nLen);
    T_UINT32 crc2 = TComputeCRC32(pMsg, nLen);
    SHOULD_BE_EQUAL(crc1, crc2);
    SHOULD_BE_TRUE((crc1 != 0));

    const char* pCorrupted = "Hello Teleport Integrity Xheck!";
    T_UINT32 crcCorrupt = TComputeCRC32(pCorrupted, nLen);
    SHOULD_BE_TRUE((crc1 != crcCorrupt));
}


//
static volatile T_UINT32 g_nCorruptRecvCount = 0;
static std::vector<T_MSG_ID> g_vCorruptRecvIds;

static RC CorruptTestCallback(PTCbMessage pMessage)
{
    if (pMessage && pMessage->eType == MsgType::MSG_SUB_GET)
    {
        g_nCorruptRecvCount++;
        g_vCorruptRecvIds.push_back(pMessage->nOriginalMsgId);
    }
    return RC::SUCCESS;
}

T_VOID UT_TestCorruptedMessageDiscard()
{
    g_nCorruptRecvCount = 0;
    g_vCorruptRecvIds.clear();

    T_PCSTR pTopic = "ut_corrupt_topic";
    T_ID nSubChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nSubChannelId,
        CorruptTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_ID nPubChannelId = 0;
    rc = ITeleport::Open(pTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nPubChannelId,
        CorruptTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    std::string msg1 = "Valid Message 1";
    rc = ITeleport::Send(nPubChannelId, (T_PCVOID)msg1.c_str(), (T_UINT32)msg1.length());
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    CChannel* pCh = CChannelMgr::Instance().GetChannelById(nSubChannelId);
    SHOULD_BE_TRUE((pCh != T_NULL));
    T_PVOID pBuf = T_NULL;
    T_UINT64 token = 0;
    std::string msg2 = "Corrupted Message 2";
    rc = ITeleport::AcquireBuffer(nPubChannelId, (T_UINT32)msg2.length(), pBuf, token, LOG_RECORD_FLAG_CORRUPT_CRC);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    memcpy(pBuf, msg2.c_str(), msg2.length());
    rc = ITeleport::CommitBuffer(nPubChannelId, token, (T_UINT32)msg2.length());
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    std::string msg3 = "Valid Message 3";
    rc = ITeleport::Send(nPubChannelId, (T_PCVOID)msg3.c_str(), (T_UINT32)msg3.length());
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_UINT32 nWait = 50;
    while (g_nCorruptRecvCount < 2 && nWait--)
    {
        TSleep(20);
    }

    rc = ITeleport::Close(nPubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nCorruptRecvCount, 2);
    if (g_vCorruptRecvIds.size() >= 2)
    {
        SHOULD_BE_EQUAL(g_vCorruptRecvIds[0], 1);
        SHOULD_BE_EQUAL(g_vCorruptRecvIds[1], 3);
    }
}


//
// Optimization 1 Unit Test: Variable-length messages and zero-copy wrap-around
//
static volatile T_UINT32 g_nVarMsgCount = 0;
static volatile T_UINT32 g_nVarErrorCount = 0;

static RC VarMsgCallback(PTCbMessage pMessage)
{
    if (pMessage && pMessage->eType == MsgType::MSG_SUB_GET)
    {
        g_nVarMsgCount++;
        const unsigned char* pData = (const unsigned char*)pMessage->pData;
        for (T_UINT32 i = 0; i < pMessage->nLength; i += 1024)
        {
            if (pData[i] != (unsigned char)(i % 251))
            {
                g_nVarErrorCount++;
                break;
            }
        }
    }
    return RC::SUCCESS;
}

T_VOID UT_TestVariableLengthMessages()
{
    g_nVarMsgCount = 0;
    g_nVarErrorCount = 0;

    T_PCSTR pTopic = "ut_var_length_topic";
    T_ID nSubId = 0, nPubId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nSubId, VarMsgCallback, T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Open(pTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nPubId, VarMsgCallback, T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    std::vector<T_UINT32> testSizes = { 64, 512, 4096, 16384, 65536, 131072, 262144 };
    for (size_t s = 0; s < testSizes.size(); s++)
    {
        T_UINT32 nSize = testSizes[s];
        std::vector<unsigned char> buf(nSize);
        for (T_UINT32 i = 0; i < nSize; i++) buf[i] = (unsigned char)(i % 251);

        rc = ITeleport::Send(nPubId, buf.data(), nSize);
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }

    // Also test zero-copy AcquireBuffer and CommitBuffer for a large 128KB payload
    T_UINT32 zeroCopySize = 131072;
    T_PVOID pBuf = T_NULL;
    T_UINT64 token = 0;
    rc = ITeleport::AcquireBuffer(nPubId, zeroCopySize, pBuf, token);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    SHOULD_BE_TRUE(pBuf != T_NULL);
    for (T_UINT32 i = 0; i < zeroCopySize; i++) ((unsigned char*)pBuf)[i] = (unsigned char)(i % 251);
    rc = ITeleport::CommitBuffer(nPubId, token, zeroCopySize);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    testSizes.push_back(zeroCopySize);

    T_UINT32 nWait = 100;
    while (g_nVarMsgCount < testSizes.size() && nWait--)
    {
        TSleep(20);
    }

    rc = ITeleport::Close(nPubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nVarMsgCount, (T_UINT32)testSizes.size());
    SHOULD_BE_EQUAL(g_nVarErrorCount, 0);
    printf("UT_TestVariableLengthMessages: All %u variable-sized and zero-copy messages verified!\n", g_nVarMsgCount);
}


//
// Optimization 2 Unit Test: Backpressure policies (POLICY_BLOCK, POLICY_DROP_OLDEST, POLICY_ISOLATE_SLOW_CONSUMER)
//
static RC DummyPolicyCallback(PTCbMessage pMsg) { return RC::SUCCESS; }

T_VOID UT_TestChannelPolicies()
{
    T_PCSTR pTopic = "ut_policy_test_topic";
    T_ID nChId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nChId, DummyPolicyCallback, T_FALSE, ChannelPolicy::POLICY_BLOCK);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    CChannel* pCh = CChannelMgr::Instance().GetChannelById(nChId);
    SHOULD_BE_TRUE(pCh != T_NULL);
    CChannelData* pData = pCh->GetChannelData();
    SHOULD_BE_TRUE(pData != T_NULL);

    SHOULD_BE_EQUAL((int)pData->GetChannelPolicy(), (int)ChannelPolicy::POLICY_BLOCK);

    pData->SetChannelPolicy(ChannelPolicy::POLICY_DROP_OLDEST);
    SHOULD_BE_EQUAL((int)pData->GetChannelPolicy(), (int)ChannelPolicy::POLICY_DROP_OLDEST);

    pData->SetChannelPolicy(ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER, 50);
    SHOULD_BE_EQUAL((int)pData->GetChannelPolicy(), (int)ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER);
    SHOULD_BE_EQUAL(pData->GetShmHeader()->nLagThreshold, 50);

    // Test slow subscriber isolation logic
    TPChannelShmHeader pHdr = pData->GetShmHeader();
    pData->LockHdr();
    pHdr->AckRecords[0].ProcId = 88888;
    pHdr->AckRecords[0].AckFlag = ACK_FLAG::INIT;
    pHdr->AckRecords[0].LastReadSeq = 0;
    pHdr->AckRecords[0].LastReadOffset = 0;
    pHdr->AckRecords[0].DropCount = 0;
    pHdr->nSubscribers = 1;
    pHdr->PubHeader.CommitMsgSeq = 200;
    pHdr->PubHeader.WriteCursor = 1024 * 1024;
    pData->UnlockHdr();

    T_UINT64 minOffset = pData->GetMinSubscriberOffset();
    pData->LockHdr();
    SHOULD_BE_EQUAL(pHdr->AckRecords[0].Status, (T_UINT32)SUB_STATUS_ISOLATED);
    pData->UnlockHdr();

    rc = ITeleport::Close(nChId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    printf("UT_TestChannelPolicies: All 3 QoS backpressure policies verified!\n");
}


//
// Optimization 4 Unit Test: Synchronous cross-process RPC Request-Response
//
static RC EchoRpcHandler(T_PCVOID pReq, T_UINT32 nReqLen, T_PVOID pResp, T_UINT32& nRespLen)
{
    std::string reqStr((const char*)pReq, nReqLen);
    std::string respStr = "ECHO:" + reqStr;
    if (respStr.length() > nRespLen) return RC::EXCEED_LIMIT;
    memcpy(pResp, respStr.data(), respStr.length());
    nRespLen = (T_UINT32)respStr.length();
    return RC::SUCCESS;
}

static RC MathAddRpcHandler(T_PCVOID pReq, T_UINT32 nReqLen, T_PVOID pResp, T_UINT32& nRespLen)
{
    std::string reqStr((const char*)pReq, nReqLen);
    int a = 0, b = 0;
    if (sscanf(reqStr.c_str(), "%d+%d", &a, &b) == 2)
    {
        std::string respStr = std::to_string(a + b);
        if (respStr.length() > nRespLen) return RC::EXCEED_LIMIT;
        memcpy(pResp, respStr.data(), respStr.length());
        nRespLen = (T_UINT32)respStr.length();
        return RC::SUCCESS;
    }
    return RC::INVALID_PARAM;
}

T_VOID UT_TestRpcCall()
{
    RC rc = ITeleport::RegisterRpcService("echo_rpc_service", EchoRpcHandler);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    rc = ITeleport::RegisterRpcService("math_rpc_service", MathAddRpcHandler);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    // Call echo service
    std::string req = "HelloTeleportRpc!";
    char respBuf[256] = {0};
    T_UINT32 respLen = sizeof(respBuf);
    rc = ITeleport::Call("echo_rpc_service", req.c_str(), (T_UINT32)req.length(), respBuf, respLen, 3000);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    std::string respStr(respBuf, respLen);
    SHOULD_BE_EQUAL(respStr, "ECHO:HelloTeleportRpc!");

    // Call math service multiple times
    for (int i = 1; i <= 5; i++)
    {
        std::string mathReq = std::to_string(i * 10) + "+" + std::to_string(i * 20);
        respLen = sizeof(respBuf);
        memset(respBuf, 0, sizeof(respBuf));
        rc = ITeleport::Call("math_rpc_service", mathReq.c_str(), (T_UINT32)mathReq.length(), respBuf, respLen, 3000);
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
        std::string mathResp(respBuf, respLen);
        SHOULD_BE_EQUAL(mathResp, std::to_string(i * 30));
    }

    rc = ITeleport::UnregisterRpcService("echo_rpc_service");
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    rc = ITeleport::UnregisterRpcService("math_rpc_service");
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    printf("UT_TestRpcCall: Synchronous cross-process RPC calls passed successfully!\n");
}


// ============================================================================
// RIGOROUS AUTOMATED TESTS FOR ALL 4 OPTIMIZATIONS
// ============================================================================

// ----------------------------------------------------------------------------
// Rigor Test 1: Continuous Circular Log Buffer & Variable-Length Edge Cases
// ----------------------------------------------------------------------------
static volatile T_UINT32 g_nRigorBoundaryRecv = 0;
static volatile T_UINT32 g_nRigorBoundaryCorrupt = 0;
static std::vector<T_UINT32> g_vRigorExpectedLengths;

static RC RigorBoundaryCallback(PTCbMessage pMsg)
{
    if (pMsg && pMsg->eType == MsgType::MSG_SUB_GET)
    {
        T_UINT32 idx = g_nRigorBoundaryRecv++;
        if (idx < g_vRigorExpectedLengths.size())
        {
            if (pMsg->nLength != g_vRigorExpectedLengths[idx])
            {
                g_nRigorBoundaryCorrupt++;
            }
        }
        if (pMsg->nLength > 0 && pMsg->pData)
        {
            const unsigned char* pBytes = (const unsigned char*)pMsg->pData;
            for (T_UINT32 i = 0; i < pMsg->nLength; i += 512)
            {
                if (pBytes[i] != (unsigned char)((i ^ 0xA5) & 0xFF))
                {
                    g_nRigorBoundaryCorrupt++;
                    break;
                }
            }
        }
    }
    return RC::SUCCESS;
}

T_VOID UT_Rigor_VariableLength_BoundaryWrapping()
{
    g_nRigorBoundaryRecv = 0;
    g_nRigorBoundaryCorrupt = 0;
    g_vRigorExpectedLengths.clear();

    T_PCSTR pTopic = "ut_rigor_var_boundary";
    T_ID nSubId = 0, nPubId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nSubId, RigorBoundaryCallback, T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Open(pTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nPubId, T_NULL, T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    // 1. Edge Case: Message exceeding maximum limit (MAX_LOG_MESSAGE_SIZE + 1024)
    std::vector<unsigned char> oversized(MAX_LOG_MESSAGE_SIZE + 1024, 0xEE);
    rc = ITeleport::Send(nPubId, oversized.data(), (T_UINT32)oversized.size());
    SHOULD_BE_EQUAL(rc, RC::EXCEED_LIMIT);

    // 2. Edge Case: Empty 0-byte payload
    g_vRigorExpectedLengths.push_back(0);
    rc = ITeleport::Send(nPubId, T_NULL, 0);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    // 3. Edge Cases: Exact cache line boundaries (1B, 15B, 63B, 64B, 65B, 127B, 128B)
    std::vector<T_UINT32> edgeSizes = { 1, 15, 63, 64, 65, 127, 128, 511, 512, 1024, 4096, 65536, 262144 };
    for (T_UINT32 sz : edgeSizes)
    {
        g_vRigorExpectedLengths.push_back(sz);
        std::vector<unsigned char> payload(sz);
        for (T_UINT32 i = 0; i < sz; i++) payload[i] = (unsigned char)((i ^ 0xA5) & 0xFF);
        rc = ITeleport::Send(nPubId, payload.data(), sz);
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }

    // 4. Force Continuous Boundary Wrap-Around:
    // Write 300 messages of 64KB each (= 19.2MB).
    // Given the 16MB ring buffer, this is guaranteed to wrap around offset 0,
    // triggering LOG_RECORD_FLAG_PADDING and resetting write cursor to offset 0 cleanly.
    T_UINT32 wrapBlockSize = 65536;
    std::vector<unsigned char> wrapPayload(wrapBlockSize);
    for (T_UINT32 i = 0; i < wrapBlockSize; i++) wrapPayload[i] = (unsigned char)((i ^ 0xA5) & 0xFF);

    for (int w = 0; w < 300; w++)
    {
        g_vRigorExpectedLengths.push_back(wrapBlockSize);
        rc = ITeleport::Send(nPubId, wrapPayload.data(), wrapBlockSize);
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }

    T_UINT32 nExpectedTotal = (T_UINT32)g_vRigorExpectedLengths.size();
    T_UINT32 nWait = 200;
    while (g_nRigorBoundaryRecv < nExpectedTotal && nWait--)
    {
        TSleep(20);
    }

    rc = ITeleport::Close(nPubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nRigorBoundaryRecv, nExpectedTotal);
    SHOULD_BE_EQUAL(g_nRigorBoundaryCorrupt, 0);
    printf("UT_Rigor_VariableLength_BoundaryWrapping: %u messages (including wrap-around and limits) passed with 0 corruption!\n", nExpectedTotal);
}


// ----------------------------------------------------------------------------
// Rigor Test 2: Concurrent Multi-Thread Variable-Length Messages
// ----------------------------------------------------------------------------
static volatile T_UINT32 g_nRigorConcurrentRecv = 0;
static volatile T_UINT32 g_nRigorConcurrentErrors = 0;
static std::map<T_UINT32, T_UINT32> g_mRigorThreadLastSeq;
static std::mutex g_mRigorConcurrentMtx;

static RC RigorConcurrentCallback(PTCbMessage pMsg)
{
    if (pMsg && pMsg->eType == MsgType::MSG_SUB_GET)
    {
        if (pMsg->nLength >= 8 && pMsg->pData)
        {
            T_UINT32 threadId = *(T_UINT32*)pMsg->pData;
            T_UINT32 seq = *((T_UINT32*)pMsg->pData + 1);

            std::lock_guard<std::mutex> lk(g_mRigorConcurrentMtx);
            if (g_mRigorThreadLastSeq.find(threadId) == g_mRigorThreadLastSeq.end())
            {
                g_mRigorThreadLastSeq[threadId] = 0;
            }
            if (seq != g_mRigorThreadLastSeq[threadId] + 1)
            {
                g_nRigorConcurrentErrors++;
            }
            g_mRigorThreadLastSeq[threadId] = seq;

            // Verify payload pattern
            const unsigned char* pBytes = (const unsigned char*)pMsg->pData;
            for (T_UINT32 i = 8; i < pMsg->nLength; i += 256)
            {
                if (pBytes[i] != (unsigned char)((i + threadId) & 0xFF))
                {
                    g_nRigorConcurrentErrors++;
                    break;
                }
            }
        }
        else
        {
            g_nRigorConcurrentErrors++;
        }
        g_nRigorConcurrentRecv++;
    }
    return RC::SUCCESS;
}

T_VOID UT_Rigor_VariableLength_ConcurrentMultiThread()
{
    g_nRigorConcurrentRecv = 0;
    g_nRigorConcurrentErrors = 0;
    g_mRigorThreadLastSeq.clear();

    T_PCSTR pTopic = "ut_rigor_var_concurrent";
    T_ID nSubId = 0, nPubId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nSubId, RigorConcurrentCallback, T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Open(pTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nPubId, T_NULL, T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    const int nThreads = 4;
    const int nMsgsPerThread = 50;
    std::vector<std::thread> senders;

    for (int t = 1; t <= nThreads; t++)
    {
        senders.emplace_back([nPubId, t, nMsgsPerThread]() {
            for (int s = 1; s <= nMsgsPerThread; s++)
            {
                T_UINT32 sz = 32 + ((s * 37 + t * 97) % 8192); // variable size 32B ~ 8KB
                std::vector<unsigned char> buf(sz);
                *(T_UINT32*)buf.data() = (T_UINT32)t;
                *((T_UINT32*)buf.data() + 1) = (T_UINT32)s;
                for (T_UINT32 i = 8; i < sz; i++)
                {
                    buf[i] = (unsigned char)((i + t) & 0xFF);
                }
                RC sendRc = ITeleport::Send(nPubId, buf.data(), sz);
                if (IS_FAILED(sendRc))
                {
                    g_nRigorConcurrentErrors++;
                }
            }
        });
    }

    for (auto& th : senders)
    {
        th.join();
    }

    T_UINT32 nTotalExpected = nThreads * nMsgsPerThread;
    T_UINT32 nWait = 200;
    while (g_nRigorConcurrentRecv < nTotalExpected && nWait--)
    {
        TSleep(20);
    }

    rc = ITeleport::Close(nPubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nRigorConcurrentRecv, nTotalExpected);
    SHOULD_BE_EQUAL(g_nRigorConcurrentErrors, 0);
    printf("UT_Rigor_VariableLength_ConcurrentMultiThread: %u concurrent variable messages verified with 0 error!\n", nTotalExpected);
}


// ----------------------------------------------------------------------------
// Rigor Test 3: QoS POLICY_BLOCK Backpressure & 100% Reliable Delivery
// ----------------------------------------------------------------------------
static volatile T_UINT32 g_nRigorBlockRecv = 0;

static RC RigorBlockCallback(PTCbMessage pMsg)
{
    if (pMsg && pMsg->eType == MsgType::MSG_SUB_GET)
    {
        g_nRigorBlockRecv++;
    }
    return RC::SUCCESS;
}

T_VOID UT_Rigor_Policy_Block_Backpressure()
{
    g_nRigorBlockRecv = 0;
    T_PCSTR pTopic = "ut_rigor_policy_block";
    T_ID nSubId = 0, nPubId = 0;

    // Explicitly set POLICY_BLOCK
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nSubId, RigorBlockCallback, T_FALSE, ChannelPolicy::POLICY_BLOCK);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Open(pTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nPubId, T_NULL, T_FALSE, ChannelPolicy::POLICY_BLOCK);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    rc = ITeleport::SetChannelPolicy(nPubId, ChannelPolicy::POLICY_BLOCK);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    ChannelPolicy pol = ChannelPolicy::POLICY_DROP_OLDEST;
    T_UINT32 lag = 0;
    rc = ITeleport::GetChannelPolicy(nPubId, pol, lag);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    SHOULD_BE_EQUAL((int)pol, (int)ChannelPolicy::POLICY_BLOCK);

    // Send 200 messages (each 4KB)
    const T_UINT32 nTotal = 200;
    std::vector<unsigned char> data(4096, 0x5A);
    for (T_UINT32 i = 0; i < nTotal; i++)
    {
        rc = ITeleport::Send(nPubId, data.data(), (T_UINT32)data.size());
        SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    }

    T_UINT32 nWait = 200;
    while (g_nRigorBlockRecv < nTotal && nWait--)
    {
        TSleep(20);
    }

    rc = ITeleport::Close(nPubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(g_nRigorBlockRecv, nTotal);
    printf("UT_Rigor_Policy_Block_Backpressure: POLICY_BLOCK verified 100%% reliable delivery (%u/%u)!\n", g_nRigorBlockRecv, nTotal);
}


// ----------------------------------------------------------------------------
// Rigor Test 4: QoS POLICY_DROP_OLDEST Buffer Overrun & DropCount Recovery
// ----------------------------------------------------------------------------
static volatile T_UINT32 g_nRigorDropCountReceived = 0;
static volatile T_UINT32 g_nRigorDropMsgCount = 0;

static RC RigorDropCallback(PTCbMessage pMsg)
{
    if (pMsg)
    {
        if (pMsg->eType == MsgType::MSG_DROPPED)
        {
            g_nRigorDropCountReceived += pMsg->nLength;
        }
        else if (pMsg->eType == MsgType::MSG_SUB_GET)
        {
            g_nRigorDropMsgCount++;
        }
    }
    return RC::SUCCESS;
}

T_VOID UT_Rigor_Policy_DropOldest_Overwrite()
{
    g_nRigorDropCountReceived = 0;
    g_nRigorDropMsgCount = 0;

    T_PCSTR pTopic = "ut_rigor_policy_drop";
    T_ID nChId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nChId, RigorDropCallback, T_FALSE, ChannelPolicy::POLICY_DROP_OLDEST);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    CChannel* pCh = CChannelMgr::Instance().GetChannelById(nChId);
    SHOULD_BE_TRUE(pCh != T_NULL);
    CChannelData* pData = pCh->GetChannelData();
    SHOULD_BE_TRUE(pData != T_NULL);

    SHOULD_BE_EQUAL((int)pData->GetChannelPolicy(), (int)ChannelPolicy::POLICY_DROP_OLDEST);

    // Write records into channel
    T_MSG_ID msgId = 0;
    T_UINT32 dummy = 12345;
    for (int i = 0; i < 5; i++)
    {
        pData->WriteRingMsg(&dummy, sizeof(dummy), msgId, 1001);
    }

    // Simulate severe subscriber lag exceeding buffer capacity
    TPChannelShmHeader pHdr = pData->GetShmHeader();
    pData->LockHdr();
    pHdr->PubHeader.WriteCursor = 32 * 1024 * 1024; // 32MB ahead
    pHdr->AckRecords[0].LastReadOffset = 0;
    pHdr->AckRecords[0].LastReadSeq = 0;
    pData->UnlockHdr();

    // Call ReadRingMsg: detects overrun, updates DropCount, marks SUB_STATUS_DROPPED
    T_PVOID pOutData = T_NULL;
    T_UINT32 nOutSize = 0;
    T_MSG_ID nOutId = 0;
    T_ID nOutSender = 0;
    T_UINT32 nOutFlags = 0;
    T_UINT64 nOutCorr = 0;
    rc = pData->ReadRingMsg(&pHdr->AckRecords[0], pOutData, nOutSize, nOutId, nOutSender, nOutFlags, nOutCorr);

    pData->LockHdr();
    SHOULD_BE_EQUAL(pHdr->AckRecords[0].Status, (T_UINT32)SUB_STATUS_DROPPED);
    pData->UnlockHdr();

    rc = ITeleport::Close(nChId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    printf("UT_Rigor_Policy_DropOldest_Overwrite: POLICY_DROP_OLDEST overrun detection & recovery passed!\n");
}


// ----------------------------------------------------------------------------
// Rigor Test 5: QoS POLICY_ISOLATE_SLOW_CONSUMER Dynamic Lag Isolation & Recovery
// ----------------------------------------------------------------------------
T_VOID UT_Rigor_Policy_IsolateSlowConsumer()
{
    T_PCSTR pTopic = "ut_rigor_policy_isolate";
    T_ID nChId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nChId, DummyPolicyCallback, T_FALSE, ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    CChannel* pCh = CChannelMgr::Instance().GetChannelById(nChId);
    SHOULD_BE_TRUE(pCh != T_NULL);
    CChannelData* pData = pCh->GetChannelData();
    SHOULD_BE_TRUE(pData != T_NULL);

    // Set lag threshold to 32 KB
    pData->SetChannelPolicy(ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER, 32 * 1024);
    SHOULD_BE_EQUAL((int)pData->GetChannelPolicy(), (int)ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER);
    SHOULD_BE_EQUAL(pData->GetShmHeader()->nLagThreshold, 32 * 1024);

    // Setup 2 subscribers in shared memory:
    // Sub 0: Fast subscriber (LastReadOffset = 64KB)
    // Sub 1: Slow subscriber (LastReadOffset = 0KB, lag = 64KB > 32KB threshold)
    TPChannelShmHeader pHdr = pData->GetShmHeader();
    pData->LockHdr();
    pHdr->nSubscribers = 2;
    pHdr->PubHeader.WriteCursor = 64 * 1024;
    pHdr->PubHeader.WriteMsgSeq = 100;
    pHdr->PubHeader.CommitMsgSeq = 100;

    pHdr->AckRecords[0].ProcId = 11111;
    pHdr->AckRecords[0].Status = SUB_STATUS_ACTIVE;
    pHdr->AckRecords[0].LastReadOffset = 64 * 1024;
    pHdr->AckRecords[0].LastReadSeq = 100;

    pHdr->AckRecords[1].ProcId = 22222;
    pHdr->AckRecords[1].Status = SUB_STATUS_ACTIVE;
    pHdr->AckRecords[1].LastReadOffset = 0;
    pHdr->AckRecords[1].LastReadSeq = 0;
    pData->UnlockHdr();

    // Call GetMinSubscriberOffset:
    // Sub 1 has lag 64KB > threshold 32KB -> MUST transition to SUB_STATUS_ISOLATED!
    T_UINT64 minOffset = pData->GetMinSubscriberOffset();

    pData->LockHdr();
    SHOULD_BE_EQUAL(pHdr->AckRecords[1].Status, (T_UINT32)SUB_STATUS_ISOLATED);
    SHOULD_BE_EQUAL(pHdr->AckRecords[0].Status, (T_UINT32)SUB_STATUS_ACTIVE);
    pData->UnlockHdr();

    // The minOffset must now reflect Sub 0's offset, ignoring the isolated Sub 1!
    SHOULD_BE_EQUAL(minOffset, (T_UINT64)(64 * 1024));

    // Now test recovery: Sub 1 catches up
    pData->LockHdr();
    pHdr->AckRecords[1].LastReadOffset = 64 * 1024;
    pData->UnlockHdr();

    minOffset = pData->GetMinSubscriberOffset();
    pData->LockHdr();
    SHOULD_BE_EQUAL(pHdr->AckRecords[1].Status, (T_UINT32)SUB_STATUS_ACTIVE);
    pData->UnlockHdr();

    rc = ITeleport::Close(nChId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    printf("UT_Rigor_Policy_IsolateSlowConsumer: Dynamic lag isolation & recovery passed!\n");
}


// ----------------------------------------------------------------------------
// Rigor Test 6: Hybrid Adaptive Wait Pruning & High-Contention Spinlock
// ----------------------------------------------------------------------------
T_VOID UT_Rigor_AdaptiveWait_PruningAndTiming()
{
    // 1. Primitive sanity tests
    for (int i = 0; i < 1000; i++)
    {
        T_CPU_PAUSE();
    }
    for (int i = 0; i < 20; i++)
    {
        T_THREAD_YIELD();
    }

    // 2. T_ADAPTIVE_WAIT spin phase test (true at attempt 20)
    int count = 0;
    bool res = T_ADAPTIVE_WAIT([&count]() {
        return (++count >= 20);
    }, 100, 10);
    SHOULD_BE_TRUE(res);
    SHOULD_BE_EQUAL(count, 20);

    // 3. T_ADAPTIVE_WAIT yield phase test (true at attempt 105, spinCount = 100)
    count = 0;
    res = T_ADAPTIVE_WAIT([&count]() {
        return (++count >= 105);
    }, 100, 20);
    SHOULD_BE_TRUE(res);
    SHOULD_BE_EQUAL(count, 105);

    // 4. T_ADAPTIVE_WAIT never true
    count = 0;
    res = T_ADAPTIVE_WAIT([&count]() {
        count++;
        return false;
    }, 50, 10);
    SHOULD_BE_TRUE(!res);
    SHOULD_BE_EQUAL(count, 61); // 50 spins + 10 yields + 1 final check

    // 5. T_ADAPTIVE_WAIT_TIMEOUT test with background delayed signal
    std::atomic<bool> flag{ false };
    std::thread delayedSignaler([&flag]() {
        TSleep(30);
        flag = true;
    });

    res = T_ADAPTIVE_WAIT_TIMEOUT([&flag]() {
        return flag.load();
    }, 500, 50, 10);
    SHOULD_BE_TRUE(res);
    delayedSignaler.join();

    // 6. High contention multithreaded spinlock test
    std::atomic<int> spinlockVal{ 0 };
    std::atomic<int> sharedCounter{ 0 };
    const int nWorkers = 4;
    const int nOpsPerWorker = 5000;
    std::vector<std::thread> workers;

    for (int w = 0; w < nWorkers; w++)
    {
        workers.emplace_back([&]() {
            for (int op = 0; op < nOpsPerWorker; op++)
            {
                while (spinlockVal.exchange(1, std::memory_order_acquire) != 0)
                {
                    T_CPU_PAUSE();
                }
                sharedCounter.fetch_add(1, std::memory_order_relaxed);
                spinlockVal.store(0, std::memory_order_release);
            }
        });
    }

    for (auto& w : workers)
    {
        w.join();
    }
    SHOULD_BE_EQUAL(sharedCounter.load(), nWorkers * nOpsPerWorker);

    printf("UT_Rigor_AdaptiveWait_PruningAndTiming: All 6 adaptive wait and contention tests passed!\n");
}


// ----------------------------------------------------------------------------
// Rigor Test 7: Synchronous RPC Concurrent Multi-Thread Correlation Isolation
// ----------------------------------------------------------------------------
static RC RigorCalcRpcHandler(T_PCVOID pReq, T_UINT32 nReqLen, T_PVOID pResp, T_UINT32& nRespLen)
{
    std::string req((const char*)pReq, nReqLen);
    int a = 0, b = 0;
    if (sscanf(req.c_str(), "%d*%d", &a, &b) == 2)
    {
        std::string resp = std::to_string(a * b + 11);
        if (resp.length() > nRespLen) return RC::EXCEED_LIMIT;
        memcpy(pResp, resp.data(), resp.length());
        nRespLen = (T_UINT32)resp.length();
        return RC::SUCCESS;
    }
    return RC::INVALID_PARAM;
}

T_VOID UT_Rigor_Rpc_MultiThreadedConcurrency()
{
    T_PCSTR pServiceName = "rigor_calc_service";
    RC rc = ITeleport::RegisterRpcService(pServiceName, RigorCalcRpcHandler);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    const int nClients = 4;
    const int nCallsPerClient = 25;
    std::atomic<int> nSuccessCalls{ 0 };
    std::atomic<int> nCrossTalkErrors{ 0 };
    std::vector<std::thread> clients;

    for (int c = 1; c <= nClients; c++)
    {
        clients.emplace_back([pServiceName, c, nCallsPerClient, &nSuccessCalls, &nCrossTalkErrors]() {
            for (int k = 1; k <= nCallsPerClient; k++)
            {
                int a = c * 10 + k;
                int b = k * 3;
                std::string req = std::to_string(a) + "*" + std::to_string(b);
                std::string expectedResp = std::to_string(a * b + 11);

                char respBuf[128] = { 0 };
                T_UINT32 respLen = sizeof(respBuf);
                RC callRc = ITeleport::Call(pServiceName, req.c_str(), (T_UINT32)req.length(), respBuf, respLen, 3000);
                if (IS_SUCCESS(callRc))
                {
                    std::string actualResp(respBuf, respLen);
                    if (actualResp != expectedResp)
                    {
                        nCrossTalkErrors++;
                    }
                    else
                    {
                        nSuccessCalls++;
                    }
                }
                else
                {
                    nCrossTalkErrors++;
                }
            }
        });
    }

    for (auto& cl : clients)
    {
        cl.join();
    }

    rc = ITeleport::UnregisterRpcService(pServiceName);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    SHOULD_BE_EQUAL(nSuccessCalls.load(), nClients * nCallsPerClient);
    SHOULD_BE_EQUAL(nCrossTalkErrors.load(), 0);
    printf("UT_Rigor_Rpc_MultiThreadedConcurrency: %d concurrent RPC calls with correlation ID matching verified!\n", nSuccessCalls.load());
}


// ----------------------------------------------------------------------------
// Rigor Test 8: Large RPC Payload & Client Truncation Verification
// ----------------------------------------------------------------------------
static RC RigorPayloadRpcHandler(T_PCVOID pReq, T_UINT32 nReqLen, T_PVOID pResp, T_UINT32& nRespLen)
{
    T_UINT32 outSize = nReqLen * 2;
    if (outSize > nRespLen)
    {
        return RC::EXCEED_LIMIT;
    }
    unsigned char* pOut = (unsigned char*)pResp;
    for (T_UINT32 i = 0; i < outSize; i++)
    {
        pOut[i] = (unsigned char)((i ^ 0x3C) & 0xFF);
    }
    nRespLen = outSize;
    return RC::SUCCESS;
}

T_VOID UT_Rigor_Rpc_LargePayloadAndTruncation()
{
    T_PCSTR pServiceName = "rigor_payload_service";
    RC rc = ITeleport::RegisterRpcService(pServiceName, RigorPayloadRpcHandler);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    // 1. Large 32KB request producing 64KB response
    T_UINT32 reqSize = 32768;
    std::vector<unsigned char> reqPayload(reqSize, 0xAA);
    std::vector<unsigned char> respBuffer(131072, 0);
    T_UINT32 respLen = (T_UINT32)respBuffer.size();

    rc = ITeleport::Call(pServiceName, reqPayload.data(), reqSize, respBuffer.data(), respLen, 3000);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    SHOULD_BE_EQUAL(respLen, reqSize * 2);

    // Verify content integrity
    for (T_UINT32 i = 0; i < respLen; i++)
    {
        if (respBuffer[i] != (unsigned char)((i ^ 0x3C) & 0xFF))
        {
            SHOULD_BE_TRUE(false);
            break;
        }
    }

    // 2. Truncation test: Client only provides 32 bytes for a 64KB response
    char smallBuf[32] = { 0 };
    T_UINT32 smallLen = sizeof(smallBuf);
    rc = ITeleport::Call(pServiceName, reqPayload.data(), reqSize, smallBuf, smallLen, 3000);
    SHOULD_BE_EQUAL(rc, RC::EXCEED_LIMIT);
    SHOULD_BE_EQUAL(smallLen, reqSize * 2); // Informs caller of actual response size
    for (T_UINT32 i = 0; i < sizeof(smallBuf); i++)
    {
        SHOULD_BE_EQUAL((unsigned char)smallBuf[i], (unsigned char)((i ^ 0x3C) & 0xFF));
    }

    rc = ITeleport::UnregisterRpcService(pServiceName);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    printf("UT_Rigor_Rpc_LargePayloadAndTruncation: Large payload (64KB) and truncation checks passed!\n");
}


// ----------------------------------------------------------------------------
// Rigor Test 9: RPC Timeout, Error Status Propagation & Cleanup
// ----------------------------------------------------------------------------
static RC RigorTimeoutRpcHandler(T_PCVOID pReq, T_UINT32 nReqLen, T_PVOID pResp, T_UINT32& nRespLen)
{
    std::string req((const char*)pReq, nReqLen);
    if (req == "SLOW")
    {
        TSleep(250);
        return RC::SUCCESS;
    }
    else if (req == "ERROR_PARAM")
    {
        return RC::INVALID_PARAM;
    }
    std::string resp = "OK:" + req;
    if (resp.length() > nRespLen) return RC::EXCEED_LIMIT;
    memcpy(pResp, resp.data(), resp.length());
    nRespLen = (T_UINT32)resp.length();
    return RC::SUCCESS;
}

T_VOID UT_Rigor_Rpc_TimeoutAndErrorHandling()
{
    T_PCSTR pServiceName = "rigor_timeout_service";
    RC rc = ITeleport::RegisterRpcService(pServiceName, RigorTimeoutRpcHandler);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    // 1. Test timeout: handler sleeps 250ms, client timeout = 50ms
    char respBuf[64] = { 0 };
    T_UINT32 respLen = sizeof(respBuf);
    std::string slowReq = "SLOW";
    rc = ITeleport::Call(pServiceName, slowReq.c_str(), (T_UINT32)slowReq.length(), respBuf, respLen, 50);
    SHOULD_BE_EQUAL(rc, RC::TIMEOUT);

    // 2. Test handler custom error code propagation
    std::string errReq = "ERROR_PARAM";
    respLen = sizeof(respBuf);
    rc = ITeleport::Call(pServiceName, errReq.c_str(), (T_UINT32)errReq.length(), respBuf, respLen, 3000);
    SHOULD_BE_EQUAL(rc, RC::INVALID_PARAM);

    // 3. Test clean unregister
    rc = ITeleport::UnregisterRpcService(pServiceName);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    // 4. Test calling unregistered/closed service
    std::string normReq = "PING";
    respLen = sizeof(respBuf);
    rc = ITeleport::Call(pServiceName, normReq.c_str(), (T_UINT32)normReq.length(), respBuf, respLen, 50);
    SHOULD_BE_TRUE(rc == RC::CLOSED || rc == RC::TIMEOUT || rc == RC::NOT_FOUND);

    printf("UT_Rigor_Rpc_TimeoutAndErrorHandling: RPC timeout and error propagation verified!\n");
}



//
static volatile T_UINT32 g_nStressRecvCount = 0;
static volatile T_UINT32 g_nStressOrderErrorCount = 0;

static RC StressTestCallback(PTCbMessage pMessage)
{
    if (!pMessage)
    {
        return RC::INVALID_PARAM;
    }
    if (pMessage->eType == MsgType::MSG_SUB_GET)
    {
        if (pMessage->pData && pMessage->nLength >= sizeof(T_UINT32))
        {
            T_UINT32 nSeq = *(T_UINT32*)pMessage->pData;
            if (nSeq != g_nStressRecvCount + 1)
            {
                g_nStressOrderErrorCount++;
            }
        }
        g_nStressRecvCount++;
    }
    return RC::SUCCESS;
}


//
static inline T_UINT64 GetTimeMs()
{
#ifdef Windows
    return (T_UINT64)::GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (T_UINT64)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}


//
T_VOID UT_TestStress(T_UINT32 nTotalMessages = 2000000)
{
    g_nStressRecvCount = 0;
    g_nStressOrderErrorCount = 0;

    T_PCSTR pTopic = "ut_stress_topic";
    T_ID nSubChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nSubChannelId,
        StressTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_ID nPubChannelId = 0;
    rc = ITeleport::Open(pTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nPubChannelId,
        StressTestCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    printf("Starting stress test: %u messages...\n", nTotalMessages);
    fflush(stdout);

    T_UINT64 tStartTime = GetTimeMs();
    T_UINT32 nLastReport = 0;

    for (T_UINT32 i = 1; i <= nTotalMessages; i++)
    {
        T_UINT32 nThrottleYield = 0;
        while ((i - 1) - g_nStressRecvCount >= 2000)
        {
            if (++nThrottleYield % 50 == 0)
            {
                TSleep(1);
            }
            else
            {
                TSleep(0);
            }
        }

        T_UINT32 nSeq = i;
        rc = ITeleport::Send(nPubChannelId, (T_PCVOID)&nSeq, sizeof(nSeq));
        if (IS_FAILED(rc))
        {
            printf("Send failed at message %u: rc=%d\n", i, (int)rc);
            break;
        }

        if (i - nLastReport >= 200000 || i == nTotalMessages)
        {
            T_UINT64 nElapsed = GetTimeMs() - tStartTime;
            if (nElapsed == 0) nElapsed = 1;
            printf("Progress: Sent %u / %u | Received: %u | Elapsed: %llu ms (%.0f msg/s)\n",
                i, nTotalMessages, g_nStressRecvCount, (unsigned long long)nElapsed, (double)g_nStressRecvCount * 1000.0 / nElapsed);
            fflush(stdout);
            nLastReport = i;
        }
    }

    T_UINT32 nWaitLoops = 1000;
    while (g_nStressRecvCount < nTotalMessages && nWaitLoops--)
    {
        TSleep(50);
        if (g_nStressRecvCount - nLastReport >= 200000 || g_nStressRecvCount == nTotalMessages)
        {
            T_UINT64 nElapsed = GetTimeMs() - tStartTime;
            if (nElapsed == 0) nElapsed = 1;
            printf("Waiting: Received %u / %u | Elapsed: %llu ms (%.0f msg/s)\n",
                g_nStressRecvCount, nTotalMessages, (unsigned long long)nElapsed, (double)g_nStressRecvCount * 1000.0 / nElapsed);
            fflush(stdout);
            nLastReport = g_nStressRecvCount;
        }
    }

    rc = ITeleport::Close(nPubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    rc = ITeleport::Close(nSubChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_UINT64 nTotalElapsed = GetTimeMs() - tStartTime;
    if (nTotalElapsed == 0) nTotalElapsed = 1;
    T_UINT32 nLostCount = nTotalMessages - g_nStressRecvCount;

    printf("\n================ Stress Test Result ================\n");
    printf("Total Messages Sent:     %u\n", nTotalMessages);
    printf("Total Messages Received: %u\n", g_nStressRecvCount);
    printf("Lost Messages:           %u\n", nLostCount);
    printf("Order Errors:            %u\n", g_nStressOrderErrorCount);
    printf("Elapsed Time:            %llu ms (%.2f s)\n", (unsigned long long)nTotalElapsed, (double)nTotalElapsed / 1000.0);
    printf("Throughput:              %.0f msg/s\n", (double)g_nStressRecvCount * 1000.0 / nTotalElapsed);
    printf("====================================================\n\n");

    SHOULD_BE_EQUAL(g_nStressRecvCount, nTotalMessages);
    SHOULD_BE_EQUAL(nLostCount, 0);
    SHOULD_BE_EQUAL(g_nStressOrderErrorCount, 0);
}

static std::vector<double> g_vOneWayLatenciesUs;
static std::mutex g_mtxLatency;
static RC LatencyTestCallback(PTCbMessage pMessage)
{
    if (pMessage && pMessage->eType == MsgType::MSG_SUB_GET && pMessage->nLength >= sizeof(uint64_t))
    {
        uint64_t sendNs = *(uint64_t*)pMessage->pData;
        auto nowNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        if (nowNs >= sendNs)
        {
            double us = (double)(nowNs - sendNs) / 1000.0;
            std::lock_guard<std::mutex> lk(g_mtxLatency);
            g_vOneWayLatenciesUs.push_back(us);
        }
    }
    return RC::SUCCESS;
}

static RC DummyRpcHandler(T_PCVOID pReqData, T_UINT32 nReqLen, T_PVOID pRespBuf, T_UINT32& nRespLen)
{
    if (nReqLen > 0 && pReqData)
    {
        memcpy(pRespBuf, pReqData, (nReqLen < nRespLen) ? nReqLen : nRespLen);
        nRespLen = (nReqLen < nRespLen) ? nReqLen : nRespLen;
    }
    return RC::SUCCESS;
}

static void PrintLatencyPercentiles(const char* title, std::vector<double>& latencies)
{
    if (latencies.empty()) return;
    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();
    double minV = latencies.front();
    double maxV = latencies.back();
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double meanV = sum / n;
    double p50 = latencies[(size_t)(n * 0.50)];
    double p90 = latencies[(size_t)(n * 0.90)];
    double p99 = latencies[(size_t)(n * 0.99)];
    double p999 = latencies[(size_t)(n * 0.999)];

    printf("\n================ %s (Samples: %zu) ================\n", title, n);
    printf("  Min Latency:    %8.3f us (%7.1f ns)\n", minV, minV * 1000.0);
    printf("  Mean Latency:   %8.3f us (%7.1f ns)\n", meanV, meanV * 1000.0);
    printf("  P50  (Median):  %8.3f us (%7.1f ns)\n", p50, p50 * 1000.0);
    printf("  P90:            %8.3f us (%7.1f ns)\n", p90, p90 * 1000.0);
    printf("  P99:            %8.3f us (%7.1f ns)\n", p99, p99 * 1000.0);
    printf("  P99.9:          %8.3f us (%7.1f ns)\n", p999, p999 * 1000.0);
    printf("  Max Latency:    %8.3f us (%7.1f ns)\n", maxV, maxV * 1000.0);
    printf("======================================================================\n");
}

void UT_BenchmarkLatency(T_UINT32 nSamples = 100000)
{
    printf("Running End-to-End Latency Benchmark (%u samples)...\n", nSamples);
    g_vOneWayLatenciesUs.clear();
    g_vOneWayLatenciesUs.reserve(nSamples);

    // 1. One-way Pub/Sub Latency
    T_PCSTR pTopic = "ut_latency_topic";
    T_ID nSubId = 0, nPubId = 0;
    RC rc = ITeleport::Open(pTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nSubId, LatencyTestCallback, T_FALSE);
    if (IS_FAILED(rc)) { printf("Open sub failed\n"); return; }
    rc = ITeleport::Open(pTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nPubId, LatencyTestCallback, T_FALSE);
    if (IS_FAILED(rc)) { printf("Open pub failed\n"); return; }

    // Warm-up
    for (int w = 0; w < 1000; w++)
    {
        uint64_t t = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        ITeleport::Send(nPubId, &t, sizeof(t));
    }
    TSleep(50);
    {
        std::lock_guard<std::mutex> lk(g_mtxLatency);
        g_vOneWayLatenciesUs.clear();
    }

    for (T_UINT32 i = 0; i < nSamples; i++)
    {
        uint64_t t = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        ITeleport::Send(nPubId, &t, sizeof(t));
        if ((i & 0x1ff) == 0) TSleep(0);
    }

    int waitLimit = 200;
    while (true)
    {
        {
            std::lock_guard<std::mutex> lk(g_mtxLatency);
            if (g_vOneWayLatenciesUs.size() >= nSamples || waitLimit <= 0) break;
        }
        TSleep(10);
        waitLimit--;
    }
    ITeleport::Close(nPubId, T_TRUE);
    ITeleport::Close(nSubId, T_TRUE);

    PrintLatencyPercentiles("One-Way End-to-End Latency (Pub -> Sub)", g_vOneWayLatenciesUs);

    // 2. Round-Trip RPC Latency
    T_PCSTR pRpcTopic = "ut_latency_rpc";
    ITeleport::RegisterRpcService(pRpcTopic, DummyRpcHandler, T_FALSE);
    std::vector<double> rttLatencies;
    rttLatencies.reserve(nSamples);

    char reqBuf[64] = "ping";
    char respBuf[64] = {0};
    T_UINT32 respLen = sizeof(respBuf);

    // Warmup
    for (int w = 0; w < 500; w++)
    {
        respLen = sizeof(respBuf);
        ITeleport::Call(pRpcTopic, reqBuf, 4, respBuf, respLen, 1000, T_FALSE);
    }

    for (T_UINT32 i = 0; i < nSamples; i++)
    {
        respLen = sizeof(respBuf);
        auto tStart = std::chrono::high_resolution_clock::now();
        rc = ITeleport::Call(pRpcTopic, reqBuf, 4, respBuf, respLen, 1000, T_FALSE);
        auto tEnd = std::chrono::high_resolution_clock::now();
        if (IS_SUCCESS(rc))
        {
            double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count() / 1000.0;
            rttLatencies.push_back(us);
        }
    }
    ITeleport::UnregisterRpcService(pRpcTopic);

    PrintLatencyPercentiles("Synchronous RPC Round-Trip (RTT) Latency", rttLatencies);
}



#pragma pack(push, 1)
typedef struct _TStressPayload
{
    T_UINT32 nSenderId;
    T_UINT32 nSeq;
}TStressPayload;
#pragma pack(pop)

static volatile T_UINT32 g_nMPTotalRecv = 0;
static volatile T_UINT32 g_nMPOrderErrors = 0;
static std::map<T_UINT32, T_UINT32> g_mSenderLastSeq;

static RC MPListenCallback(PTCbMessage pMessage)
{
    if (!pMessage)
    {
        return RC::INVALID_PARAM;
    }
    if (pMessage->eType == MsgType::MSG_SUB_GET)
    {
        if (pMessage->pData && pMessage->nLength >= sizeof(TStressPayload))
        {
            TStressPayload* pPayload = (TStressPayload*)pMessage->pData;
            T_UINT32 nSenderId = pPayload->nSenderId;
            T_UINT32 nSeq = pPayload->nSeq;
            if (g_mSenderLastSeq.find(nSenderId) == g_mSenderLastSeq.end())
            {
                g_mSenderLastSeq[nSenderId] = 0;
            }
            if (nSeq != g_mSenderLastSeq[nSenderId] + 1)
            {
                g_nMPOrderErrors++;
                if (g_nMPOrderErrors <= 20)
                {
                    printf("ORDER_ERR: Sender %u: expected %u, got %u\n", nSenderId, g_mSenderLastSeq[nSenderId] + 1, nSeq);
                }
            }
            g_mSenderLastSeq[nSenderId] = nSeq;
        }
        g_nMPTotalRecv++;
    }
    return RC::SUCCESS;
}

//
T_VOID RunMPListen(T_PCSTR pTopic, T_UINT32 nTotalMsg, T_UINT32 nListenerId = 0)
{
    g_nMPTotalRecv = 0;
    g_nMPOrderErrors = 0;
    g_mSenderLastSeq.clear();

    T_ID nChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_LISTEN | CH_CREATE_IF_NOEXIST,
        nChannelId,
        MPListenCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    printf("MP Listener %u ready on topic '%s', expecting %u messages...\n", nListenerId, pTopic, nTotalMsg);
    fflush(stdout);

    T_UINT64 tStartTime = GetTimeMs();
    T_BOOL bStarted = T_FALSE;
    T_UINT32 nLastReport = 0;
    T_UINT32 nWaitLoops = 40000; // up to 2000s

    while (g_nMPTotalRecv < nTotalMsg && nWaitLoops--)
    {
        TSleep(50);
        if (g_nMPTotalRecv > 0 && !bStarted)
        {
            bStarted = T_TRUE;
            tStartTime = GetTimeMs();
        }
        if (g_nMPTotalRecv - nLastReport >= 200000 || (g_nMPTotalRecv > 0 && g_nMPTotalRecv == nTotalMsg))
        {
            T_UINT64 nElapsed = GetTimeMs() - tStartTime;
            if (nElapsed == 0) nElapsed = 1;
            printf("MP Listener %u: Received %u / %u | Elapsed: %llu ms (%.0f msg/s)\n",
                nListenerId, g_nMPTotalRecv, nTotalMsg, (unsigned long long)nElapsed, (double)g_nMPTotalRecv * 1000.0 / nElapsed);
            fflush(stdout);
            nLastReport = g_nMPTotalRecv;
        }
    }

    rc = ITeleport::Close(nChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_UINT64 nTotalElapsed = GetTimeMs() - tStartTime;
    if (nTotalElapsed == 0) nTotalElapsed = 1;
    T_UINT32 nLostCount = nTotalMsg - g_nMPTotalRecv;

    printf("\n================ Multi-Process Stress Result (Listener %u) ================\n", nListenerId);
    printf("Total Messages Expected: %u\n", nTotalMsg);
    printf("Total Messages Received: %u\n", g_nMPTotalRecv);
    printf("Total Messages Lost:     %u\n", nLostCount);
    printf("Order Errors:            %u\n", g_nMPOrderErrors);
    printf("Active Sender Processes: %zu\n", g_mSenderLastSeq.size());
    for (auto it : g_mSenderLastSeq)
    {
        printf("  - Sender ID %u: %u messages received\n", it.first, it.second);
    }
    printf("Elapsed Time:            %llu ms (%.2f s)\n", (unsigned long long)nTotalElapsed, (double)nTotalElapsed / 1000.0);
    printf("Throughput:              %.0f msg/s\n", (double)g_nMPTotalRecv * 1000.0 / nTotalElapsed);
    printf("========================================================================\n\n");
    fflush(stdout);
}

//
static RC MPSendCallback(PTCbMessage pMessage)
{
    return RC::SUCCESS;
}

//
T_VOID RunMPSend(T_PCSTR pTopic, T_UINT32 nMsgCount, T_UINT32 nSenderId, T_UINT32 nExpectedSubs = 1)
{
    T_ID nChannelId = 0;
    RC rc = ITeleport::Open(pTopic,
        CH_SEND | CH_CREATE_IF_NOEXIST,
        nChannelId,
        MPSendCallback,
        T_FALSE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    while (pChannel && (T_USHORT)pChannel->GetSubscriberCount() < nExpectedSubs)
    {
        TSleep(20);
    }

    printf("MP Sender %u started: sending %u messages...\n", nSenderId, nMsgCount);
    fflush(stdout);

    T_UINT64 tStartTime = GetTimeMs();
    T_UINT32 nLastReport = 0;

    for (T_UINT32 i = 1; i <= nMsgCount; i++)
    {
        TStressPayload payload;
        payload.nSenderId = nSenderId;
        payload.nSeq = i;

        rc = ITeleport::Send(nChannelId, (T_PCVOID)&payload, sizeof(payload));
        if (IS_FAILED(rc))
        {
            printf("Sender %u: send failed at seq %u: rc=%d\n", nSenderId, i, (int)rc);
            break;
        }

        if (i - nLastReport >= 50000 || i == nMsgCount)
        {
            T_UINT64 nElapsed = GetTimeMs() - tStartTime;
            if (nElapsed == 0) nElapsed = 1;
            printf("Sender %u: Sent %u / %u | Elapsed: %llu ms (%.0f msg/s)\n",
                nSenderId, i, nMsgCount, (unsigned long long)nElapsed, (double)i * 1000.0 / nElapsed);
            fflush(stdout);
            nLastReport = i;
        }
    }

    rc = ITeleport::Close(nChannelId, T_TRUE);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    T_UINT64 nTotalElapsed = GetTimeMs() - tStartTime;
    if (nTotalElapsed == 0) nTotalElapsed = 1;
    printf("Sender %u complete: %u msgs in %llu ms (%.0f msg/s)\n",
        nSenderId, nMsgCount, (unsigned long long)nTotalElapsed, (double)nMsgCount * 1000.0 / nTotalElapsed);
    fflush(stdout);
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
    if (0 == _stricmp(pCmd, "mp_listen"))
    {
        if (argc < 3) { Usage(); return -1; }
        T_UINT32 nTotalMsg = (T_UINT32)atoi(argv[2]);
        T_UINT32 nListenerId = (argc >= 4) ? (T_UINT32)atoi(argv[3]) : 0;
        RunMPListen("ut_mp_stress_topic", nTotalMsg, nListenerId);
        return 0;
    }
    else if (0 == _stricmp(pCmd, "mp_send"))
    {
        if (argc < 4) { Usage(); return -1; }
        T_UINT32 nMsgCount = (T_UINT32)atoi(argv[2]);
        T_UINT32 nSenderId = (T_UINT32)atoi(argv[3]);
        T_UINT32 nExpectedSubs = (argc >= 5) ? (T_UINT32)atoi(argv[4]) : 1;
        RunMPSend("ut_mp_stress_topic", nMsgCount, nSenderId, nExpectedSubs);
        return 0;
    }

    if (argc == 2)
    {
        if (0 == _stricmp(pCmd, "ut"))
        {
            UT_TestBFCrypto();
            UT_TestThread();
            UT_TestSetUnreadHoles();
            UT_TestGetFirstAvailRecord();
            UT_TestChannelGuidConsistency();
            UT_TestMessageDeliveryNoLoss();
            UT_TestZeroCopy();
            UT_TestZombieCleanup();
            UT_TestCRCIntegrity();
            UT_TestCorruptedMessageDiscard();
            UT_TestVariableLengthMessages();
            UT_TestChannelPolicies();
            UT_TestRpcCall();

            // Rigorous automated test cases for all 4 optimizations
            UT_Rigor_VariableLength_BoundaryWrapping();
            UT_Rigor_VariableLength_ConcurrentMultiThread();
            UT_Rigor_Policy_Block_Backpressure();
            UT_Rigor_Policy_DropOldest_Overwrite();
            UT_Rigor_Policy_IsolateSlowConsumer();
            UT_Rigor_AdaptiveWait_PruningAndTiming();
            UT_Rigor_Rpc_MultiThreadedConcurrency();
            UT_Rigor_Rpc_LargePayloadAndTruncation();
            UT_Rigor_Rpc_TimeoutAndErrorHandling();
            return 0;
        }
        else if (0 == _stricmp(pCmd, "stress"))
        {
            UT_TestStress(2000000);
            return 0;
        }
        else if (0 == _stricmp(pCmd, "latency"))
        {
            UT_BenchmarkLatency(50000);
            return 0;
        }
    }
    else if (argc == 3)
    {
        if (0 == _stricmp(pCmd, "stress"))
        {
            T_UINT32 nTotal = (T_UINT32)atoi(argv[2]);
            if (nTotal == 0) nTotal = 2000000;
            UT_TestStress(nTotal);
            return 0;
        }
        else if (0 == _stricmp(pCmd, "latency"))
        {
            T_UINT32 nTotal = (T_UINT32)atoi(argv[2]);
            if (nTotal == 0) nTotal = 50000;
            UT_BenchmarkLatency(nTotal);
            return 0;
        }
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
