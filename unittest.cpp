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
    rc = ITeleport::AcquireBuffer(nPubChannelId, (T_UINT32)msg2.length(), pBuf, token);
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);
    memcpy(pBuf, msg2.c_str(), msg2.length());
    rc = ITeleport::CommitBuffer(nPubChannelId, token, (T_UINT32)msg2.length());
    SHOULD_BE_EQUAL(rc, RC::SUCCESS);

    TPRingSlot pSlot2 = pCh->GetChannelData()->GetSlot((T_UINT32)(token & RING_SLOT_MASK));
    pSlot2->nChecksum ^= 0x12345678;

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
            return 0;
        }
        else if (0 == _stricmp(pCmd, "stress"))
        {
            UT_TestStress(2000000);
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
