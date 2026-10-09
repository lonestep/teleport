/**
*    File:         teleport.cpp
*
*    Desc:
*
*    Author:     lonestep@gmail.com
*    Created:
*/
#include "teleport.hpp"
using namespace TLP;


//
CChannelBase::CChannelBase():
    m_pCallback(T_NULL),
    m_bRunning(T_FALSE),
    m_bStopped(T_FALSE),
    m_hStopEvent(T_INVHDL)
{ 
    m_hStopEvent = TCreateEvent();
}


//
CChannelBase::~CChannelBase()
{ 
    Stop();
    SAFE_CLOSE_HANDLE(m_hStopEvent);
}


//
RC CChannelBase::Start()
{
    if (!m_bRunning)
    {
        RC rc = m_tCallback.Create(&CChannelBase::CallbackRunWrapper, this);
        if (IS_SUCCESS(rc))
        {
            rc = m_tCallback.Start();
        }
        rc = m_tSub.Create(&CChannelBase::SubRunWrapper, this);
        if (IS_SUCCESS(rc))
        {
            rc = m_tSub.Start();
        }
        rc = m_tPub.Create(&CChannelBase::PubRunWrapper, this);
        if(IS_SUCCESS(rc))
        {
            rc = m_tPub.Start();
        }
        
        LogInfo("CChannelBase::Start::Started.");
        m_bRunning = T_TRUE;
        return rc;
    }
    return RC::ALREADY_EXIST;
}


//
RC CChannelBase::Stop()
{
    if (!m_bStopped)
    {
        m_bStopped = T_TRUE;
        TSetEvent(m_hStopEvent);
        if (m_bRunning)
        {
            m_tPub.Stop();
            m_tSub.Stop();
            m_tCallback.Stop();
            m_bRunning = T_FALSE;
            LogInfo("CChannelBase::Stop::Stopped.");
        }
    }
    return RC::SUCCESS;
}


//
RC CChannelBase::PubRunWrapper(T_PVOID pThis)
{ 
    return ((CChannelBase*)pThis)->RunPubThread();
}


//
RC CChannelBase::SubRunWrapper(T_PVOID pThis)
{
    return ((CChannelBase*)pThis)->RunSubThread();
}



//
RC CChannelBase::CallbackRunWrapper(T_PVOID pThis)
{ 
    return ((CChannelBase*)pThis)->RunCallback();
}


//
RC CChannelBase::SetCallBack(TLP_CALLBACK cb)
{
    m_pCallback = cb;
    return RC::SUCCESS;
}


// Global RPC state management
static std::mutex g_mRpcLock;
static std::map<T_STRING, TLP_RPC_HANDLER> g_mRpcHandlers;
static std::map<T_STRING, T_ID> g_mRpcServerChannels;

struct RpcPendingCall
{
    std::mutex mtx;
    std::condition_variable cv;
    bool completed = false;
    RC status = RC::TIMEOUT;
    std::vector<uint8_t> respData;
};

static std::mutex g_mPendingCallsLock;
static std::map<T_UINT64, RpcPendingCall*> g_mPendingCalls;
static std::atomic<uint64_t> g_nRpcSeqCounter{ 1 };

// RPC Server callback: receives requests, invokes handler, sends response to szReplyTopic
static RC RpcServerCallback(PTCbMessage pMsg)
{
    if (!pMsg || !pMsg->pData || pMsg->nLength < sizeof(TRpcEnvelope))
    {
        return RC::INVALID_PARAM;
    }
    TRpcEnvelope* pEnv = (TRpcEnvelope*)pMsg->pData;
    TLP_RPC_HANDLER handler = T_NULL;
    T_BOOL bGlobal = T_FALSE;
    {
        std::lock_guard<std::mutex> lk(g_mRpcLock);
        CChannel* pCh = CChannelMgr::Instance().GetChannelById(pMsg->nChannelId);
        if (pCh)
        {
            bGlobal = pCh->IsGlobal();
            auto it = g_mRpcHandlers.find(pCh->GetChannelName());
            if (it != g_mRpcHandlers.end())
            {
                handler = it->second;
            }
        }
    }
    if (!handler)
    {
        return RC::NOT_FOUND;
    }

    const void* pReqBody = (const void*)((const char*)pMsg->pData + sizeof(TRpcEnvelope));
    T_UINT32 nReqBodyLen = pEnv->nBodyLength;

    std::vector<uint8_t> respBuffer(1024 * 1024);
    T_UINT32 nRespLen = (T_UINT32)respBuffer.size();

    RC rc = handler(pReqBody, nReqBodyLen, respBuffer.data(), nRespLen);

    std::vector<uint8_t> fullResp(sizeof(TRpcResponseEnvelope) + nRespLen);
    TRpcResponseEnvelope* pRespEnv = (TRpcResponseEnvelope*)fullResp.data();
    pRespEnv->nCorrelationId = pEnv->nCorrelationId;
    pRespEnv->nResult = rc;
    pRespEnv->nBodyLength = nRespLen;
    pRespEnv->nReserved = 0;
    if (nRespLen > 0)
    {
        memcpy(fullResp.data() + sizeof(TRpcResponseEnvelope), respBuffer.data(), nRespLen);
    }

    T_ID nReplyChId = 0;
    rc = ITeleport::Open(pEnv->szReplyTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nReplyChId, T_NULL, bGlobal);
    if (IS_SUCCESS(rc))
    {
        rc = ITeleport::Send(nReplyChId, fullResp.data(), (T_UINT32)fullResp.size(), LOG_RECORD_FLAG_RPC_RESP, pEnv->nCorrelationId);
    }
    return rc;
}

// RPC Client reply callback: receives responses, dispatches to pending caller
static RC RpcClientReplyCallback(PTCbMessage pMsg)
{
    if (!pMsg || !pMsg->pData || pMsg->nLength < sizeof(TRpcResponseEnvelope))
    {
        return RC::INVALID_PARAM;
    }
    TRpcResponseEnvelope* pResp = (TRpcResponseEnvelope*)pMsg->pData;
    std::lock_guard<std::mutex> lk(g_mPendingCallsLock);
    auto it = g_mPendingCalls.find(pResp->nCorrelationId);
    if (it != g_mPendingCalls.end())
    {
        RpcPendingCall* pCall = it->second;
        std::lock_guard<std::mutex> clk(pCall->mtx);
        pCall->status = pResp->nResult;
        const uint8_t* pBody = (const uint8_t*)pMsg->pData + sizeof(TRpcResponseEnvelope);
        pCall->respData.assign(pBody, pBody + pResp->nBodyLength);
        pCall->completed = true;
        pCall->cv.notify_one();
    }
    return RC::SUCCESS;
}


//
RC ITeleport::Open(T_PCSTR strChannelName,
    T_UINT32 eOpenFlag,
    T_ID& nChannelId,
    TLP_CALLBACK cbCallback,
    T_BOOL bGlobal,
    ChannelPolicy ePolicy)
{
    CChannelMgr* pChannelMgr = &CChannelMgr::Instance();
    RC rc = pChannelMgr->ValidateChannelParam(strChannelName, eOpenFlag, cbCallback);
    CHK_RC(rc);
    if (bGlobal)
    {
        pChannelMgr->ShiftToGlobal();
    }    
    CChannel* pChannel = pChannelMgr->GetChannelByName(strChannelName);
    if(!pChannel)
    {
        if (!(eOpenFlag & CH_CREATE_IF_NOEXIST))
        {
            return RC::NOT_FOUND;
        }
        pChannel = pChannelMgr->CreateChannel(strChannelName, bGlobal, ePolicy);
        if (!pChannel)
        {
            return RC::FAILED;
        }
    }
    nChannelId = pChannel->GetChannelId();
    if (cbCallback)
    {
        pChannel->SetCallBack(cbCallback);
    }
    if ( ((T_USHORT)eOpenFlag & CH_LISTEN)&&
        !pChannel->IsOpenned() )
    {
        rc = pChannel->Subscribe((OpenFlag)eOpenFlag, bGlobal);
    }
    return rc;
}


//
RC ITeleport::Send(T_ID nChannelId, T_PCVOID pData, T_UINT32 nSizeInByte, T_UINT32 nFlags, T_UINT64 nCorrelationId)
{
    if (nSizeInByte > MAX_LOG_MESSAGE_SIZE)
    {
        return RC::EXCEED_LIMIT;
    }
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel)
    {
        return pChannel->Publish(pData, nSizeInByte, nFlags, nCorrelationId);
    }
    return RC::NOT_FOUND;
}


//
RC ITeleport::AcquireBuffer(T_ID nChannelId, T_UINT32 nSizeInByte, T_PVOID& pBuffer, T_UINT64& nToken, T_UINT32 nFlags, T_UINT64 nCorrelationId)
{
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel)
    {
        return pChannel->AcquireBuffer(nSizeInByte, pBuffer, nToken, nFlags, nCorrelationId);
    }
    return RC::NOT_FOUND;
}


//
RC ITeleport::CommitBuffer(T_ID nChannelId, T_UINT64 nToken, T_UINT32 nSizeInByte)
{
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel)
    {
        return pChannel->CommitBuffer(nToken, nSizeInByte);
    }
    return RC::NOT_FOUND;
}


//
RC ITeleport::SetChannelPolicy(T_ID nChannelId, ChannelPolicy ePolicy, T_UINT32 nLagThreshold)
{
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel && pChannel->GetChannelData())
    {
        pChannel->GetChannelData()->SetChannelPolicy(ePolicy, nLagThreshold);
        return RC::SUCCESS;
    }
    return RC::NOT_FOUND;
}


//
RC ITeleport::GetChannelPolicy(T_ID nChannelId, ChannelPolicy& ePolicy, T_UINT32& nLagThreshold)
{
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel && pChannel->GetChannelData())
    {
        ePolicy = pChannel->GetChannelData()->GetChannelPolicy();
        nLagThreshold = pChannel->GetChannelData()->GetShmHeader()->nLagThreshold;
        return RC::SUCCESS;
    }
    return RC::NOT_FOUND;
}


//
RC ITeleport::Close(T_ID nChannelId, T_BOOL bSendMsgBeforeClose)
{
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel)
    {
        CChannelMgr::Instance().IncDecChannelRecordRef(nChannelId, T_FALSE);
        if(bSendMsgBeforeClose)
        {
            pChannel->WaitAllEventDone();
        }
        if(pChannel->IsOpenned())
        {
            return pChannel->Unsubscribe(0);
        }
        else
        {
            pChannel->Stop();
            return RC::SUCCESS;
        }
    }
    return RC::NOT_FOUND;
}


//
RC ITeleport::RegisterRpcService(T_PCSTR strTopic, TLP_RPC_HANDLER pHandler, T_BOOL bGlobal)
{
    if (!strTopic || !pHandler)
    {
        return RC::INVALID_PARAM;
    }
    std::lock_guard<std::mutex> lk(g_mRpcLock);
    g_mRpcHandlers[strTopic] = pHandler;
    T_ID nChId = 0;
    RC rc = ITeleport::Open(strTopic, CH_LISTEN | CH_CREATE_IF_NOEXIST, nChId, RpcServerCallback, bGlobal);
    if (IS_SUCCESS(rc))
    {
        g_mRpcServerChannels[strTopic] = nChId;
    }
    return rc;
}


//
RC ITeleport::UnregisterRpcService(T_PCSTR strTopic)
{
    if (!strTopic) return RC::INVALID_PARAM;
    std::lock_guard<std::mutex> lk(g_mRpcLock);
    auto it = g_mRpcServerChannels.find(strTopic);
    if (it != g_mRpcServerChannels.end())
    {
        ITeleport::Close(it->second, T_TRUE);
        g_mRpcServerChannels.erase(it);
    }
    g_mRpcHandlers.erase(strTopic);
    return RC::SUCCESS;
}


//
RC ITeleport::Call(T_PCSTR strTopic, T_PCVOID pReqData, T_UINT32 nReqLen, T_PVOID pRespData, T_UINT32& nRespLen, T_UINT32 nTimeoutMs, T_BOOL bGlobal)
{
    if (!strTopic)
    {
        return RC::INVALID_PARAM;
    }

    std::string replyTopic = "_rpc_rep_" + std::to_string(TGetProcId()) + "_" + std::to_string(TGetThreadId());
    T_ID nReplyChId = 0;
    RC rc = ITeleport::Open(replyTopic.c_str(), CH_LISTEN | CH_CREATE_IF_NOEXIST, nReplyChId, RpcClientReplyCallback, bGlobal);
    if (IS_FAILED(rc))
    {
        return rc;
    }

    uint64_t correlationId = ((uint64_t)TGetProcId() << 32) | g_nRpcSeqCounter.fetch_add(1);
    RpcPendingCall call;
    {
        std::lock_guard<std::mutex> lk(g_mPendingCallsLock);
        g_mPendingCalls[correlationId] = &call;
    }

    std::vector<uint8_t> reqBuf(sizeof(TRpcEnvelope) + nReqLen);
    TRpcEnvelope* pEnv = (TRpcEnvelope*)reqBuf.data();
    pEnv->nCorrelationId = correlationId;
    pEnv->nBodyLength = nReqLen;
    pEnv->nReserved = 0;
    memset(pEnv->szReplyTopic, 0, sizeof(pEnv->szReplyTopic));
    strncpy(pEnv->szReplyTopic, replyTopic.c_str(), sizeof(pEnv->szReplyTopic) - 1);
    if (pReqData && nReqLen > 0)
    {
        memcpy(reqBuf.data() + sizeof(TRpcEnvelope), pReqData, nReqLen);
    }

    T_ID nTargetChId = 0;
    rc = ITeleport::Open(strTopic, CH_SEND | CH_CREATE_IF_NOEXIST, nTargetChId, T_NULL, bGlobal);
    if (IS_FAILED(rc))
    {
        std::lock_guard<std::mutex> lk(g_mPendingCallsLock);
        g_mPendingCalls.erase(correlationId);
        return rc;
    }

    rc = ITeleport::Send(nTargetChId, reqBuf.data(), (T_UINT32)reqBuf.size(), LOG_RECORD_FLAG_RPC_REQ, correlationId);
    if (IS_FAILED(rc))
    {
        std::lock_guard<std::mutex> lk(g_mPendingCallsLock);
        g_mPendingCalls.erase(correlationId);
        return rc;
    }

    std::unique_lock<std::mutex> clk(call.mtx);
    bool ok = call.cv.wait_for(clk, std::chrono::milliseconds(nTimeoutMs), [&] { return call.completed; });

    {
        std::lock_guard<std::mutex> lk(g_mPendingCallsLock);
        g_mPendingCalls.erase(correlationId);
    }

    if (!ok)
    {
        return RC::TIMEOUT;
    }

    if (call.status == RC::SUCCESS)
    {
        T_UINT32 copyLen = (nRespLen < (T_UINT32)call.respData.size()) ? nRespLen : (T_UINT32)call.respData.size();
        if (pRespData && copyLen > 0)
        {
            memcpy(pRespData, call.respData.data(), copyLen);
        }
        if (nRespLen < (T_UINT32)call.respData.size())
        {
            nRespLen = (T_UINT32)call.respData.size();
            return RC::EXCEED_LIMIT;
        }
        nRespLen = (T_UINT32)call.respData.size();
    }
    return call.status;
}


//
CChannel* CChannelMgr::GetChannelByName(T_PCSTR pChannelName)
{
    if (!pChannelName) return T_NULL;
    std::lock_guard<std::mutex> lock(m_mtxChannels);
    T_CHANNEL_NAME_MAP::iterator it = m_mName2Channels.find(pChannelName);
    if (it != m_mName2Channels.end())
    {
        return it->second;
    }

    return T_NULL;
}


//
RC CChannelMgr::ValidateChannelParam(T_PCSTR strChannelName, 
    T_UINT32 nFlag, 
    TLP_CALLBACK pCallback)
{
    if (!strChannelName)
    {
        return RC::INVALID_PARAM;
    }
    if ((nFlag & CH_LISTEN) && !pCallback)
    {
        return RC::INVALID_PARAM;
    }
    if((nFlag & ~(CH_ALL)))
    {
        return RC::INVALID_PARAM;
    }
    T_STRING strName = strChannelName;
    if( (- 1 != strName.find('\\')) ||
        (strName.size() > MAX_NAME) )
    {
        return RC::INVALID_PARAM;
    }
    return RC::SUCCESS;
}


//
CChannel* CChannelMgr::GetChannelById(T_ID nChannelId)
{
    if (0 == nChannelId)
    {
        return T_NULL;
    }
    std::lock_guard<std::mutex> lock(m_mtxChannels);
    for (T_CHANNEL_ID_MAP::iterator it = m_mId2Channels.begin();
        it != m_mId2Channels.end();
        it++)
    {
        if (it->second->GetChannelId() == nChannelId)
        {
            return it->second;
        }
    }
    return T_NULL;
}


//
CChannelMgr& CChannelMgr::Instance()
{
    static CChannelMgr _channel_mgr_inst;
    return _channel_mgr_inst;
}


//
T_ID CChannelMgr::MakeChannelId(T_PCSTR strChannelName, T_BOOL bGlobal)
{
    T_STRING strHash = strChannelName;
    strHash += bGlobal ? GLOBAL_SUFFIX_1 : GLOBAL_SUFFIX_0;
    return BKDRHash(strHash.c_str());
}


//
T_STRING CChannelMgr::MakeObjectName()
{
    T_STRING strNamedObjName;
    if (m_bGlobal)
    {
        strNamedObjName = GLOBAL_STR;
    }
    strNamedObjName += NAMED_OBJ_PREFIX;
    strNamedObjName += "Channels";
    return strNamedObjName;
}


//
CChannel* CChannelMgr::CreateChannel(T_PCSTR strChannelName, T_BOOL bGlobal, ChannelPolicy ePolicy)
{
    ScopedLock<NamedMutex> Lock(*m_pChannelShmMutex);
    T_ID nChannelId         = MakeChannelId(strChannelName, bGlobal);
    TPChannelRecord pRecord = FindChannelRecordById(nChannelId);
    T_STRING strGUID        = pRecord ? pRecord->Guid : "";
    CChannel* pChannel      = new CChannel(strChannelName, nChannelId, m_hStopEvent, strGUID, bGlobal, ePolicy);
    if(pChannel)
    {
        {
            std::lock_guard<std::mutex> lock(m_mtxChannels);
            m_mId2Channels.emplace(nChannelId, pChannel);
            m_mName2Channels.emplace(strChannelName, pChannel);
        }
        if (!pRecord)
        {
            pRecord = FindAvailableChannelRecord();
            if (pRecord)
            {
                pRecord->ChannelId = nChannelId;
                memcpy(pRecord->Guid, pChannel->GetChannelGuid().c_str(), MAX_GUID);
                pRecord->RefCnt++;
                pRecord->IsGlobal = bGlobal;
            }
        }
        else
        {
            pRecord->RefCnt++;
        }
        return pChannel;
    }
    return T_NULL;
}



//
RC CChannelMgr::CreateNamedObject()
{
    T_STRING strObjName = MakeObjectName();
    m_pChannelShm = new SharedMemory(strObjName.c_str(),
        sizeof(TChannelRecord) * m_nMaxProc);

    strObjName += "_Mutex";
    m_pChannelShmMutex = new NamedMutex(strObjName.c_str());
    if (!m_pChannelShm || !m_pChannelShmMutex)
    {
        LogVital("Failed to create shared memory or named mutex.");
    }
    return RC::SUCCESS;
}


//
CChannelMgr::CChannelMgr():
    m_nMaxProc(MAX_SUBSCRIBERS_PER_CHANNEL),
    m_hStopEvent(T_INVHDL),
    m_pChannelShm(T_NULL),
    m_bGlobal(T_FALSE)
{
    m_hStopEvent = TCreateEvent();
    CreateNamedObject();
}


//
RC CChannelMgr::ShiftToGlobal()
{
    if(!m_bGlobal)
    {
        SharedMemory* pChannelShm = m_pChannelShm;
        NamedMutex* pChannelShmMutex = m_pChannelShmMutex;
        CreateNamedObject();
        memcpy(m_pChannelShm->Begin(), pChannelShm->Begin(), pChannelShm->GetSize());
        SAFE_DELETE_OBJ(pChannelShm);
        SAFE_DELETE_OBJ(pChannelShmMutex);
        m_bGlobal = T_TRUE;
        LogInfo("Now channel manager of process #%d has shift to global.", TGetProcId());
    }
    return RC::SUCCESS;
}


//
TPChannelRecord CChannelMgr::FindAvailableChannelRecord()
{
    ScopedLock<NamedMutex> Lock(*m_pChannelShmMutex);
    TPChannelRecord pRecord = (TPChannelRecord)m_pChannelShm->Begin();
    for(int i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if((pRecord->ChannelId == 0) && (pRecord->RefCnt == 0))
        {
            return pRecord;
        }
        pRecord++;
    }
    return T_NULL;
}


//
TPChannelRecord CChannelMgr::FindChannelRecordById(T_ID nChannelId)
{
    ScopedLock<NamedMutex> Lock(*m_pChannelShmMutex);
    TPChannelRecord pRecord = (TPChannelRecord)m_pChannelShm->Begin();
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if (pRecord->ChannelId == nChannelId)
        {
            return pRecord;
        }
        pRecord++;
    }
    return T_NULL;
}


//
RC CChannelMgr::IncDecChannelRecordRef(T_ID nChannelId, T_BOOL bIncrease)
{
    TPChannelRecord pRecord = FindChannelRecordById(nChannelId);
    if(pRecord)
    {
        ScopedLock<NamedMutex> Lock(*m_pChannelShmMutex);
        if (bIncrease)
        {
            pRecord->RefCnt++;
        }
        else
        {
            pRecord->RefCnt--;
        }
        return RC::SUCCESS;
    }
    return RC::FAILED;
}


//
CChannelMgr::~CChannelMgr()
{

    if (!TSetEvent(m_hStopEvent))
    {
        LogError("Failed to trigger stop event.");
    }
    for(T_CHANNEL_ID_MAP::iterator it = m_mId2Channels.begin();
        it != m_mId2Channels.end();
        it++)
    {
        if(it->second)
        {
            delete it->second;
        }
    }
    SAFE_DELETE_OBJ(m_pChannelShm);
    SAFE_CLOSE_HANDLE(m_hStopEvent);
}


//
CChannel::CChannel(T_PCSTR pChannelName, T_ID nChannelId, T_HANDLE hStopEvent, T_STRING strGUID, T_BOOL bGlobal, ChannelPolicy ePolicy) :
    m_strChannelName(""),
    m_nChannelId(nChannelId),
    m_nMsgId(0),
    m_bActivated(T_TRUE),
    m_bWriting(T_FALSE),
    m_ePolicy(ePolicy),
    m_strNamedObjName(""),
    m_strGUID(strGUID),
    m_pAckRecord(T_NULL),
    m_bGlobal(bGlobal),
    m_pEventSubRead(T_NULL),
    m_pEventReadDone(T_NULL),
    m_pEventCallback(T_NULL),
    m_pChannelData(T_NULL),
    m_nProcId(TGetProcId())
{
    if(pChannelName)
    {
        m_strChannelName = pChannelName;
    }
    if(m_strChannelName.size() == 0)
    {
        LogVital("Channel name cant be empty!");
    }
    RC rc = MakeNamedObjName();
    if(!IS_SUCCESS(rc))
    {
        LogVital("Failed to make named object name!");
    }

    T_STRING strEvent = m_strNamedObjName + "_evt_subread";
    m_pEventSubRead   = new NamedEvent(strEvent.c_str());
    strEvent          = m_strNamedObjName + "_evt_readdone";
    m_pEventReadDone  = new NamedEvent(strEvent.c_str());
    strEvent          = m_strNamedObjName + "_evt_callback";
    m_pEventCallback  = new GenericEvent();

    if( !m_pEventSubRead  || !m_pEventSubRead->BaseEvent::IsValid() ||
        !m_pEventReadDone || !m_pEventReadDone->BaseEvent::IsValid() ||
        !m_pEventCallback || !m_pEventCallback->IsValid() )
    {
        LogVital("CChannel: Failed to create NamedEvent object!");
    }
    m_pChannelData = new CChannelData(m_strNamedObjName.c_str(), DEFAULT_SHM_SIZE, ePolicy);
    if (!m_pChannelData)
    {
        LogVital("CChannel: Failed to create CChannelData object!");
    }
    Start();
}


//
CChannel::~CChannel()
{
    if (IsOpenned())
    {
        Unsubscribe(0);
    }
    else
    {
        Stop();
    }
    SAFE_DELETE_OBJ(m_pEventSubRead);
    SAFE_DELETE_OBJ(m_pEventReadDone);
    SAFE_DELETE_OBJ(m_pEventCallback);
    SAFE_DELETE_OBJ(m_pChannelData);
}


//
RC CChannel::Publish(T_PCVOID pData, T_UINT32 nSizeInByte, T_UINT32 nFlags, T_UINT64 nCorrelationId)
{
    if (!m_bActivated)
    {
        return RC::CLOSED;
    }
    if (nSizeInByte > MAX_LOG_MESSAGE_SIZE)
    {
        return RC::EXCEED_LIMIT;
    }
    T_MSG_ID nOutMsgId = 0;
    m_bWriting = T_TRUE;
    RC rc = m_pChannelData->WriteRingMsg(pData, nSizeInByte, nOutMsgId, m_nProcId, nFlags, nCorrelationId);
    m_bWriting = T_FALSE;
    if (IS_SUCCESS(rc))
    {
        if (m_pChannelData->GetShmHeader()->nWaitingSubs > 0)
        {
            m_pEventSubRead->Post(T_FALSE);
        }
    }
    return rc;
}


//
RC CChannel::AcquireBuffer(T_UINT32 nSizeInByte, T_PVOID& pBuffer, T_UINT64& nToken, T_UINT32 nFlags, T_UINT64 nCorrelationId)
{
    if (!m_bActivated)
    {
        return RC::CLOSED;
    }
    return m_pChannelData->AcquireRingBuffer(nSizeInByte, pBuffer, nToken, nFlags, nCorrelationId);
}


//
RC CChannel::CommitBuffer(T_UINT64 nToken, T_UINT32 nSizeInByte)
{
    if (!m_bActivated)
    {
        return RC::CLOSED;
    }
    T_MSG_ID nOutMsgId = 0;
    RC rc = m_pChannelData->CommitRingBuffer(nToken, nSizeInByte, m_nProcId, nOutMsgId);
    if (IS_SUCCESS(rc))
    {
        if (m_pChannelData->GetShmHeader()->nWaitingSubs > 0)
        {
            m_pEventSubRead->Post(T_FALSE);
        }
    }
    return rc;
}


//
RC CChannel::Subscribe(OpenFlag Flag, T_BOOL bGlobal)
{
    m_pChannelData->LockHdr();
    TPAckRecord pRecord = m_pChannelData->GetFirstAvailRecord();
    if (!pRecord)
    {
        m_pChannelData->CleanZombieSubscribers();
        pRecord = m_pChannelData->GetFirstAvailRecord();
    }
    if (!pRecord)
    {
        m_pChannelData->UnlockHdr();
        return RC::EXCEED_LIMIT;
    }
    LogInfo("Channel#%d proc %d was subscribed.", m_nChannelId, m_nProcId);
    pRecord->ProcId         = m_nProcId;
    pRecord->AckFlag        = ACK_FLAG::INIT;
    pRecord->LastReadSeq    = m_pChannelData->GetShmHeader()->PubHeader.WriteMsgSeq;
    pRecord->LastReadOffset = m_pChannelData->GetShmHeader()->PubHeader.WriteCursor;
    pRecord->HeartbeatTick  = 0;
    pRecord->Status         = SUB_STATUS_ACTIVE;
    pRecord->DropCount      = 0;
    RC rc = AddSession(Flag, pRecord);
    m_pChannelData->UnlockHdr();
    CHK_RC(rc);
    m_pAckRecord = pRecord;
    m_pChannelData->IncDecSubscriber(T_TRUE);
    return rc;
}


//
RC CChannel::Unsubscribe(T_ID nProcId)
{
    if(!m_pAckRecord || m_pAckRecord->ProcId == 0)
    {
        return RC::SUCCESS;
    }
    Stop();
    m_pChannelData->LockHdr();
    m_pAckRecord->ProcId = 0;
    m_pAckRecord->LastReadSeq = 0;
    m_pAckRecord->LastReadOffset = 0;
    m_pAckRecord->AckFlag = ACK_FLAG::NONE;
    m_pAckRecord->Status = SUB_STATUS_ACTIVE;
    m_pAckRecord->DropCount = 0;
    m_pChannelData->UnlockHdr();
    m_pChannelData->IncDecSubscriber(T_FALSE);
    
    LogInfo("Channel#%d proc %d was unsubscribed.", m_nChannelId, m_nProcId);
    if(0 == nProcId)
    {
        nProcId = m_nProcId;
    }
    RC rc = RemoveSession(GetSessionId());
    CHK_RC(rc);
    memset((void*)m_pAckRecord, 0, sizeof(TAckRecord));
    m_pAckRecord = T_NULL;
    return RC::SUCCESS;
}


//
T_ID CChannel::GetChannelId()
{
    return m_nChannelId;
}


//
T_PCSTR CChannel::GetChannelName()
{
    return m_strChannelName.c_str();
}


//
T_PCSTR CChannel::GetChannelObjName()
{
    return m_strNamedObjName.c_str();
}


//
T_STRING CChannel::GetChannelGuid()
{
    return m_strGUID;
}


//
T_BOOL CChannel::IsOpenned()
{
    TSessionId Sid = GetSessionId();
    T_SESSION_MAP::iterator it = m_mSessions.find(Sid.Val);
    return (it != m_mSessions.end());
}


//
TSessionId CChannel::GetSessionId()
{
    TSessionId Sid;
    Sid.Val      = 0;
    Sid.ProcId   = m_nProcId;
    Sid.ThreadId = TGetThreadId();
    return Sid;
}


//
RC CChannel::WaitAllEventDone()
{
    m_bActivated = T_FALSE;
    T_UINT64 writeCursor = m_pChannelData->GetShmHeader()->PubHeader.WriteCursor;
    T_UINT32 nWait = 0;
    while (m_pChannelData->GetMinSubscriberOffset() < writeCursor && nWait++ < 500)
    {
        TSleep(10);
    }
    while (m_bWriting || m_qPubQueue.Size() > 0 || m_qCallbackQueue.Size() > 0)
    {
        TSleep(1);
    }
    return RC::SUCCESS;
}


//
T_SHORT CChannel::GetSubscriberCount()
{
    return m_pChannelData->GetShmHeader()->nSubscribers;
}


//
RC CChannel::OnPubAckFailed(T_MSG_ID nMsgId)
{
    TPAckRecord pRecords = m_pChannelData->ReadAckRecords();
    RC rc = RC::SUCCESS;
    for (T_UINT32 i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if ((pRecords->ProcId != 0) &&
            (pRecords->AckFlag != ACK_FLAG::DONE))
        {
            PutCallbackMsg(MsgType::MSG_PUB_ACK, nMsgId, pRecords->ProcId, T_NULL, 0, RC::FAILED);
            rc = RC::FAILED;
            //Unsubscribe(pRecords->ProcId);
        }
        pRecords++;
    }
    return rc;
}


//
RC CChannel::PutCallbackMsg(MsgType msgType,
    T_MSG_ID nMsgId,
    T_ID nProcId,
    T_PVOID pData, 
    T_UINT32 nLength, 
    RC result,
    T_UINT64 nCorrelationId)
{
    if(!m_pCallback)
    {
        return RC::INVALID_CALL;
    }
    TCbMessage msg;
    msg.eType          = msgType;
    msg.nChannelId     = m_nChannelId;
    msg.nProcessId     = nProcId;
    msg.nOriginalMsgId = nMsgId;
    msg.eResult        = result;
    msg.pData          = pData;
    msg.nLength        = nLength;
    msg.nCorrelationId = nCorrelationId;
    m_qCallbackQueue.Push(msg);
    m_pEventCallback->Post(T_FALSE);
    return RC::SUCCESS;
}


//
RC CChannel::ResetAckRecords(T_UINT32 nDataLength, T_MSG_ID nOriginMsgId)
{
    m_pChannelData->SetUnread(nOriginMsgId, m_nProcId);
    return RC::SUCCESS;
}


//
RC CChannel::MakeGuid()
{
    m_strGUID = TMakeGuid();
    if (m_strGUID.empty()) 
    {
        return RC::FAILED;
    }
    return RC::SUCCESS;
}


//
T_ID CChannel::MakeMsgId()
{
    m_nMsgId++;
    if(m_nMsgId >= MAX_ID)
    {
        m_nMsgId = 1;
    }
    return m_nMsgId;
}


//
RC CChannel::MakeNamedObjName()
{
    RC rc = RC::SUCCESS;
    if(m_strGUID.empty())
    {
        rc = MakeGuid();
        if(!IS_SUCCESS(rc))
        {
            return rc;
        }
    }
    if(m_bGlobal)
    {
        m_strNamedObjName = GLOBAL_STR;
    }
    m_strNamedObjName += (NAMED_OBJ_PREFIX + m_strGUID);
    return rc;
}


//
RC CChannel::AddSession(OpenFlag Flag, TPAckRecord pRecord)
{
    TPSessionData pData = new TSessionData();
    if(pData)
    {
        pData->Flag = Flag;
        pData->Record = pRecord;
        m_mSessions.emplace(GetSessionId().Val, pData);
        return RC::SUCCESS;
    }
    return RC::FAILED;
}


//
RC CChannel::RemoveSession(TSessionId Sid)
{
    T_SESSION_MAP::iterator it = m_mSessions.find(Sid.Val);
    if(it != m_mSessions.end())
    {
        m_mSessions.erase(it);
        return RC::SUCCESS;
    }
    return RC::NOT_FOUND;
}


//
RC CChannel::RunPubThread()
{
    TPubMessage msg;
    LogInfo("Channel %s(#%d) RunPubThread() start.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    while (!m_bStopped)
    {
        if (m_qPubQueue.Pop(msg))
        {
            T_MSG_ID nOutMsgId = 0;
            m_pChannelData->WriteRingMsg(msg.pData, msg.nLength, nOutMsgId, m_nProcId);
            if (m_pChannelData->GetShmHeader()->nWaitingSubs > 0)
            {
                m_pEventSubRead->Post(T_FALSE);
            }
            SAFE_FREE_POINTER(msg.pData);
        }
        else
        {
            if (WAIT_OBJECT_0 == WaitForSingleObject(m_hStopEvent, PUB_MSG_INTERVAL))
            {
                break;
            }
        }
    }
    LogInfo("Channel %s(#%d) RunPubThread() exit.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    m_tPub.StopRunning();
    return RC::SUCCESS;
}


//
RC CChannel::RunSubThread()
{
    LogInfo("Channel %s(#%d) RunSubThread() start.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    while (!m_bStopped)
    {
        if (!m_pAckRecord)
        {
            TSleep(10);
            continue;
        }

        T_PVOID pData = T_NULL;
        T_UINT32 nSize = 0;
        T_MSG_ID nMsgId = 0;
        T_ID nSenderProcId = 0;
        T_UINT32 nFlags = 0;
        T_UINT64 nCorrelationId = 0;

        RC rc = m_pChannelData->ReadRingMsg(m_pAckRecord, pData, nSize, nMsgId, nSenderProcId, nFlags, nCorrelationId);
        if (IS_SUCCESS(rc))
        {
            if (m_pCallback)
            {
                if (m_pAckRecord->Status == SUB_STATUS_DROPPED)
                {
                    TCbMessage dropMsg;
                    dropMsg.eType = MsgType::MSG_DROPPED;
                    dropMsg.nChannelId = m_nChannelId;
                    dropMsg.nProcessId = nSenderProcId;
                    dropMsg.nOriginalMsgId = nMsgId;
                    dropMsg.eResult = RC::SUCCESS;
                    dropMsg.pData = T_NULL;
                    dropMsg.nLength = m_pAckRecord->DropCount;
                    dropMsg.nCorrelationId = 0;
                    m_pCallback(&dropMsg);
                    m_pAckRecord->Status = SUB_STATUS_ACTIVE;
                }
                TCbMessage cbMsg;
                cbMsg.eType = (nFlags & LOG_RECORD_FLAG_RPC_REQ) ? MsgType::MSG_RPC_REQ :
                              (nFlags & LOG_RECORD_FLAG_RPC_RESP) ? MsgType::MSG_RPC_RESP : MsgType::MSG_SUB_GET;
                cbMsg.nChannelId = m_nChannelId;
                cbMsg.nProcessId = nSenderProcId;
                cbMsg.nOriginalMsgId = nMsgId;
                cbMsg.eResult = RC::SUCCESS;
                cbMsg.pData = pData;
                cbMsg.nLength = nSize;
                cbMsg.nCorrelationId = nCorrelationId;
                m_pCallback(&cbMsg);
            }
            m_pAckRecord->AckFlag = ACK_FLAG::DONE;
            if (m_pChannelData->GetShmHeader()->nWaitingPubs > 0)
            {
                m_pEventReadDone->Post(T_FALSE);
            }
            continue;
        }
        else if (rc == RC::FAILED)
        {
            LogWarn("Channel %s: Discarding corrupted slot sequence %llu",
                m_strChannelName.c_str(), (T_UINT64)(m_pAckRecord->LastReadSeq));
            continue;
        }
        else
        {
#ifdef Windows
            InterlockedIncrement((LONG*)&m_pChannelData->GetShmHeader()->nWaitingSubs);
#else
            __sync_add_and_fetch(&m_pChannelData->GetShmHeader()->nWaitingSubs, 1);
#endif
            T_UINT64 targetSeq = m_pAckRecord->LastReadSeq + 1;
            TPLogRecordHeader pRec = m_pChannelData->GetRecordHeader(m_pAckRecord->LastReadOffset);
            if (pRec->nSequence != targetSeq && !m_bStopped)
            {
                m_pEventSubRead->Wait(1);
            }
#ifdef Windows
            InterlockedDecrement((LONG*)&m_pChannelData->GetShmHeader()->nWaitingSubs);
#else
            __sync_sub_and_fetch(&m_pChannelData->GetShmHeader()->nWaitingSubs, 1);
#endif
        }
    }
    LogInfo("Channel %s(#%d) RunSubThread() exit.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    m_tSub.StopRunning();
    return RC::SUCCESS;
}


//
RC CChannel::ReadMsg(T_ID nProcId, T_MSG_ID nMsgId)
{
    if (!m_pAckRecord)
    {
        return RC::FAILED;
    }
    T_PVOID pData = T_NULL;
    T_UINT32 nSize = 0;
    T_MSG_ID nOutMsgId = 0;
    T_ID nSenderProcId = 0;
    T_UINT32 nFlags = 0;
    T_UINT64 nCorrelationId = 0;
    RC rc = m_pChannelData->ReadRingMsg(m_pAckRecord, pData, nSize, nOutMsgId, nSenderProcId, nFlags, nCorrelationId);
    if (IS_SUCCESS(rc))
    {
        if (m_pCallback)
        {
            if (m_pAckRecord->Status == SUB_STATUS_DROPPED)
            {
                TCbMessage dropMsg;
                dropMsg.eType = MsgType::MSG_DROPPED;
                dropMsg.nChannelId = m_nChannelId;
                dropMsg.nProcessId = nSenderProcId;
                dropMsg.nOriginalMsgId = nOutMsgId;
                dropMsg.eResult = RC::SUCCESS;
                dropMsg.pData = T_NULL;
                dropMsg.nLength = m_pAckRecord->DropCount;
                dropMsg.nCorrelationId = 0;
                m_pCallback(&dropMsg);
                m_pAckRecord->Status = SUB_STATUS_ACTIVE;
            }
            TCbMessage cbMsg;
            cbMsg.eType = (nFlags & LOG_RECORD_FLAG_RPC_REQ) ? MsgType::MSG_RPC_REQ :
                          (nFlags & LOG_RECORD_FLAG_RPC_RESP) ? MsgType::MSG_RPC_RESP : MsgType::MSG_SUB_GET;
            cbMsg.nChannelId = m_nChannelId;
            cbMsg.nProcessId = nSenderProcId;
            cbMsg.nOriginalMsgId = nOutMsgId;
            cbMsg.eResult = RC::SUCCESS;
            cbMsg.pData = pData;
            cbMsg.nLength = nSize;
            cbMsg.nCorrelationId = nCorrelationId;
            m_pCallback(&cbMsg);
        }
        return RC::SUCCESS;
    }
    return rc;
}



//
RC CChannel::WriteMsg(TPubMessage& msg)
{
    T_MSG_ID nOutMsgId = 0;
    RC rc = m_pChannelData->WriteRingMsg(msg.pData, msg.nLength, nOutMsgId, m_nProcId);
    if (IS_SUCCESS(rc))
    {
        if (m_pChannelData->GetShmHeader()->nWaitingSubs > 0)
        {
            m_pEventSubRead->Post(T_FALSE);
        }
    }
    return rc;
}


//
RC CChannel::RunCallback()
{
    TCbMessage msg;
    LogInfo("Channel %s(#%d) RunCallback() start.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    while (!m_bStopped)
    {
        RC rc = m_pEventCallback->Wait(100);
        m_pEventCallback->Reset();
        while (m_pCallback && m_qCallbackQueue.Pop(msg))
        {
            m_pCallback(&msg);
            if(msg.eType == MsgType::MSG_SUB_GET && msg.pData)
            {
                SAFE_FREE_POINTER(msg.pData);
            }
        }
    }
    while (m_pCallback && m_qCallbackQueue.Pop(msg))
    {
        m_pCallback(&msg);
        if(msg.eType == MsgType::MSG_SUB_GET && msg.pData)
        {
            SAFE_FREE_POINTER(msg.pData);
        }
    }
    LogInfo("Channel %s(#%d) RunCallback() exit.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    m_tCallback.StopRunning();
    return RC::SUCCESS;
}


//
CChannelData::CChannelData(T_PCSTR pChannelObjName, T_UINT32 nShmSizeInByte, ChannelPolicy ePolicy) :
    m_pSharedMemory(T_NULL),
    m_pChannelHdrMutex(T_NULL),
    m_pChannelDataMutex(T_NULL),
    m_pChannelHeader(T_NULL),
    m_pShmDataAddr(T_NULL),
    m_nDataOffset(0),
    m_nLogBufferSize(0),
    m_nLogBufferMask(0)
{
    if (nShmSizeInByte < DEFAULT_SHM_SIZE)
    {
        nShmSizeInByte = DEFAULT_SHM_SIZE;
    }
    m_nDataOffset = (sizeof(TChannelShmHeader) + 4095) & ~4095;
    T_STRING strShmName = pChannelObjName;
    strShmName += "_shm";
    m_pSharedMemory = new SharedMemory(strShmName.c_str(), nShmSizeInByte + m_nDataOffset);
    if (!m_pSharedMemory || !m_pSharedMemory->IsValid())
    {
        LogVital("CChannelData::Failed to create SharedMemory object.");
    }
    m_pChannelHdrMutex = new NamedMutex((strShmName + "_hdrmutex").c_str());
    m_pChannelDataMutex = new NamedMutex((strShmName + "_datamutex").c_str());

    if (!m_pChannelHdrMutex || !m_pChannelDataMutex)
    {
        LogVital("CChannelData::Failed to create named mutex.");
    }
    m_pChannelHeader = (TPChannelShmHeader)m_pSharedMemory->Begin();
    m_pShmDataAddr = m_pSharedMemory->Begin() + m_nDataOffset;

    // Calculate power of 2 continuous log buffer size
    T_UINT32 rawBufferSize = m_pSharedMemory->GetSize() - m_nDataOffset;
    T_UINT32 bufSize = 1;
    while ((bufSize << 1) <= rawBufferSize)
    {
        bufSize <<= 1;
    }
    m_nLogBufferSize = bufSize;
    m_nLogBufferMask = bufSize - 1;

    LockHdr();
    if (m_pChannelHeader->nMagic != TELEPORT_MAGIC)
    {
        m_pChannelHeader->nMagic = TELEPORT_MAGIC;
        m_pChannelHeader->nVersion = TELEPORT_VERSION;
        m_pChannelHeader->nUnreadCnt = 0;
        m_pChannelHeader->nSubscribers = 0;
        m_pChannelHeader->nOriginalMsgId = 0;
        m_pChannelHeader->nOriginalProcId = 0;
        m_pChannelHeader->nBufferSize = m_nLogBufferSize;
        m_pChannelHeader->nBufferMask = m_nLogBufferMask;
        m_pChannelHeader->nPolicy = ePolicy;
        m_pChannelHeader->nLagThreshold = (m_nLogBufferSize * 3) / 4;
        m_pChannelHeader->PubHeader.WriteCursor = 0;
        m_pChannelHeader->PubHeader.CommitCursor = 0;
        m_pChannelHeader->PubHeader.WriteMsgSeq = 0;
        m_pChannelHeader->PubHeader.CommitMsgSeq = 0;
        m_pChannelHeader->PubHeader.SpinLock = 0;
        m_pChannelHeader->nWaitingSubs = 0;
        m_pChannelHeader->nWaitingPubs = 0;
        memset((void*)m_pChannelHeader->AckRecords, 0, sizeof(m_pChannelHeader->AckRecords));
    }
    else
    {
        m_nLogBufferSize = m_pChannelHeader->nBufferSize;
        m_nLogBufferMask = m_pChannelHeader->nBufferMask;
    }
    UnlockHdr();
}


//
CChannelData::CChannelData():
    m_pSharedMemory(T_NULL),
    m_pChannelHdrMutex(T_NULL),
    m_pChannelDataMutex(T_NULL),
    m_pChannelHeader(T_NULL),
    m_pShmDataAddr(T_NULL),
    m_nDataOffset(0),
    m_nLogBufferSize(0),
    m_nLogBufferMask(0)
{
    m_nDataOffset = (sizeof(TChannelShmHeader) + 4095) & ~4095;
}


//
CChannelData::~CChannelData()
{
    Unlock();
    UnlockHdr();
    SAFE_DELETE_OBJ(m_pSharedMemory);
    SAFE_DELETE_OBJ(m_pChannelHdrMutex);
    SAFE_DELETE_OBJ(m_pChannelDataMutex);
}


//
RC CChannelData::Write(T_PCVOID pData, T_UINT32 nSizeInByte)
{
    T_MSG_ID nOutMsgId = 0;
    return WriteRingMsg(pData, nSizeInByte, nOutMsgId, 0);
}


//
RC CChannelData::Read(T_PVOID& pData, T_UINT32& nSizeInByte)
{
    T_UINT32 offset = (T_UINT32)(m_pChannelHeader->PubHeader.CommitCursor & m_nLogBufferMask);
    TPLogRecordHeader pRec = (TPLogRecordHeader)(m_pShmDataAddr + offset);
    nSizeInByte = pRec->nLength;
    pData = malloc(nSizeInByte + 2);
    if (!pData)
    {
        return RC::OUT_OF_MEMORY;
    }
    memset(pData, 0, nSizeInByte + 2);
    memcpy_s(pData, nSizeInByte, (const void*)(pRec + 1), nSizeInByte);
    return RC::SUCCESS;
}


//
RC CChannelData::Lock()
{
    RC rc = m_pChannelDataMutex->Lock();
    if (rc == RC::ABANDONED)
    {
        LogWarn("Channel data mutex abandoned, recovered.");
        return RC::SUCCESS;
    }
    return rc;
}


//
RC CChannelData::Unlock()
{
    return m_pChannelDataMutex->Unlock();
}


//
RC CChannelData::LockHdr()
{
    RC rc = m_pChannelHdrMutex->Lock();
    if (rc == RC::ABANDONED)
    {
        LogWarn("Channel hdr mutex abandoned, recovered.");
        return RC::SUCCESS;
    }
    return rc;
}


//
RC CChannelData::UnlockHdr()
{
    return m_pChannelHdrMutex->Unlock();
}


//
TPAckRecord CChannelData::ReadAckRecords()
{
    return m_pChannelHeader->AckRecords;
}


//
TPChannelShmHeader CChannelData::GetShmHeader()
{
    return m_pChannelHeader;
}


//
TPAckRecord CChannelData::GetFirstAvailRecord()
{
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;
    for(int i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if(pRecord->ProcId == 0)
        {
            return pRecord;
        }
        pRecord++;
    }
    return T_NULL;
}


//
T_BOOL CChannelData::SetRead()
{
    ScopedLock<NamedMutex> Lock(*m_pChannelHdrMutex);
    m_pChannelHeader->nUnreadCnt--;
    T_SHORT n = m_pChannelHeader->nUnreadCnt;
    if (n < 0) 
    {
        LogError("nUnreadCnt less than 0!");
    }
    return (n == 0);
}


//
RC CChannelData::SetUnread(T_MSG_ID nMsgId, T_ID nProcId)
{
    ScopedLock<NamedMutex> Lock(*m_pChannelHdrMutex);
    m_pChannelHeader->nOriginalProcId = nProcId;
    m_pChannelHeader->nOriginalMsgId  = nMsgId;
    T_SHORT nActiveSubscribers        = 0;
    TPAckRecord pRecord               = m_pChannelHeader->AckRecords;
    for (T_UINT32 i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if (pRecord->ProcId != 0)
        {
            pRecord->AckFlag = ACK_FLAG::INIT;
            nActiveSubscribers++;
        }
        pRecord++;
    }
    m_pChannelHeader->nUnreadCnt = nActiveSubscribers;
    return RC::SUCCESS;
}


//
RC CChannelData::DumpUnread()
{
    ScopedLock<NamedMutex> Lock(*m_pChannelHdrMutex);
    LogWarn("Unread process count:%d", m_pChannelHeader->nUnreadCnt);
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;
    for (T_UINT32 i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if (pRecord->ProcId != 0)
        {
            if(pRecord->AckFlag == ACK_FLAG::INIT)
            {
                LogWarn("Proc:%d not read yet.", pRecord->ProcId);
            }
        }
        pRecord++;
    }
    return RC::SUCCESS;
}


//
RC CChannelData::IncDecSubscriber(T_BOOL bIncrease)
{
    ScopedLock<NamedMutex> Lock(*m_pChannelHdrMutex);
    if (bIncrease)
    {
        m_pChannelHeader->nSubscribers++;
    }
    else
    {
        m_pChannelHeader->nSubscribers--;
    }
    return RC::SUCCESS;
}


//
TPRingSlot CChannelData::GetSlot(T_UINT32 nIndex)
{
    T_UINT32 offset = (T_UINT32)(nIndex & m_nLogBufferMask);
    return (TPRingSlot)(m_pShmDataAddr + offset);
}

TPLogRecordHeader CChannelData::GetRecordHeader(T_UINT64 nTokenOrSeq)
{
    T_UINT32 offset = (T_UINT32)(nTokenOrSeq & m_nLogBufferMask);
    return (TPLogRecordHeader)(m_pShmDataAddr + offset);
}

T_VOID CChannelData::SetChannelPolicy(ChannelPolicy ePolicy, T_UINT32 nLagThreshold)
{
    ScopedLock<NamedMutex> Lock(*m_pChannelHdrMutex);
    m_pChannelHeader->nPolicy = ePolicy;
    if (nLagThreshold > 0)
    {
        m_pChannelHeader->nLagThreshold = nLagThreshold;
    }
}

ChannelPolicy CChannelData::GetChannelPolicy()
{
    return m_pChannelHeader->nPolicy;
}


//
T_VOID CChannelData::CleanZombieSubscribers()
{
    ScopedLock<NamedMutex> Lock(*m_pChannelHdrMutex);
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;
    for (T_UINT32 i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL; i++)
    {
        if (pRecord->ProcId != 0)
        {
            if (!TCheckProcAlive(pRecord->ProcId))
            {
                LogWarn("Zombie subscriber (ProcId: %d) cleaned up.", pRecord->ProcId);
                pRecord->ProcId = 0;
                pRecord->LastReadSeq = 0;
                pRecord->LastReadOffset = 0;
                pRecord->AckFlag = ACK_FLAG::NONE;
                pRecord->Status = SUB_STATUS_ACTIVE;
                pRecord->DropCount = 0;
                if (m_pChannelHeader->nSubscribers > 0)
                {
                    m_pChannelHeader->nSubscribers--;
                }
            }
        }
        pRecord++;
    }
}


//
T_UINT64 CChannelData::GetMinSubscriberOffset()
{
    T_SHORT nTotalSubs = m_pChannelHeader->nSubscribers;
    if (nTotalSubs <= 0)
    {
        return m_pChannelHeader->PubHeader.WriteCursor;
    }

    T_UINT64 currentWrite = m_pChannelHeader->PubHeader.WriteCursor;
    T_UINT64 nMinOffset = UINT64_MAX;
    T_SHORT nFound = 0;
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;

    for (T_UINT32 i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL && nFound < nTotalSubs; i++)
    {
        if (pRecord->ProcId != 0)
        {
            nFound++;
            // Check for POLICY_ISOLATE_SLOW_CONSUMER
            if (m_pChannelHeader->nPolicy == ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER)
            {
                if (currentWrite > pRecord->LastReadOffset + m_pChannelHeader->nLagThreshold)
                {
                    pRecord->Status = SUB_STATUS_ISOLATED;
                    pRecord++;
                    continue;
                }
                else
                {
                    pRecord->Status = SUB_STATUS_ACTIVE;
                }
            }

            T_UINT64 nOffset = pRecord->LastReadOffset;
            if (nOffset < nMinOffset)
            {
                nMinOffset = nOffset;
            }
        }
        pRecord++;
    }

    if (nMinOffset == UINT64_MAX)
    {
        return currentWrite;
    }
    return nMinOffset;
}


//
T_UINT64 CChannelData::GetMinSubscriberSequence()
{
    T_SHORT nTotalSubs = m_pChannelHeader->nSubscribers;
    if (nTotalSubs <= 0)
    {
        return m_pChannelHeader->PubHeader.WriteMsgSeq;
    }
    T_UINT64 nMinSeq = UINT64_MAX;
    T_SHORT nFound = 0;
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;
    for (T_UINT32 i = 0; i < MAX_SUBSCRIBERS_PER_CHANNEL && nFound < nTotalSubs; i++)
    {
        if (pRecord->ProcId != 0)
        {
            nFound++;
            if (m_pChannelHeader->nPolicy == ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER &&
                pRecord->Status == SUB_STATUS_ISOLATED)
            {
                pRecord++;
                continue;
            }
            T_UINT64 nSeq = pRecord->LastReadSeq;
            if (nSeq < nMinSeq)
            {
                nMinSeq = nSeq;
            }
        }
        pRecord++;
    }
    if (nMinSeq == UINT64_MAX)
    {
        return m_pChannelHeader->PubHeader.WriteMsgSeq;
    }
    return nMinSeq;
}


//
RC CChannelData::WriteRingMsg(T_PCVOID pData, T_UINT32 nSizeInByte, T_MSG_ID& nOutMsgId, T_ID nSenderProcId, T_UINT32 nFlags, T_UINT64 nCorrelationId)
{
    if (nSizeInByte > MAX_LOG_MESSAGE_SIZE)
    {
        return RC::EXCEED_LIMIT;
    }

    T_UINT32 nRecordSize = (sizeof(TLogRecordHeader) + nSizeInByte + 63) & ~63;
    if (nRecordSize > m_nLogBufferSize / 2)
    {
        return RC::EXCEED_LIMIT;
    }

    ChannelPolicy policy = m_pChannelHeader->nPolicy;

    // Allocate in Continuous Log Buffer under spinlock for strictly ordered (writeStart, nextSeq)
    T_UINT64 writeStart = 0;
    T_UINT64 nextSeq = 0;
    T_UINT32 nSpin = 0;
    while (true)
    {
#ifdef Windows
        while (InterlockedCompareExchange((LONG*)&m_pChannelHeader->PubHeader.SpinLock, 1, 0) != 0)
        {
            T_CPU_PAUSE();
        }
#else
        while (!__sync_bool_compare_and_swap(&m_pChannelHeader->PubHeader.SpinLock, 0, 1))
        {
            T_CPU_PAUSE();
        }
#endif

        T_UINT64 currentWrite = m_pChannelHeader->PubHeader.WriteCursor;
        T_UINT64 minOffset = GetMinSubscriberOffset();
        if ((policy == ChannelPolicy::POLICY_BLOCK || policy == ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER) &&
            (currentWrite + nRecordSize > minOffset + m_nLogBufferSize))
        {
#ifdef Windows
            InterlockedExchange((LONG*)&m_pChannelHeader->PubHeader.SpinLock, 0);
#else
            __sync_lock_release(&m_pChannelHeader->PubHeader.SpinLock);
#endif
            if (++nSpin < 500)
            {
                T_CPU_PAUSE();
            }
            else if (nSpin < 550)
            {
                T_THREAD_YIELD();
            }
            else
            {
                CleanZombieSubscribers();
                TSleep(1);
            }
            continue;
        }

        T_UINT32 offset = (T_UINT32)(currentWrite & m_nLogBufferMask);
        T_UINT32 remaining = m_nLogBufferSize - offset;

        if (remaining < nRecordSize)
        {
            T_UINT32 paddingSize = remaining;
            TPLogRecordHeader pPad = (TPLogRecordHeader)(m_pShmDataAddr + offset);
            pPad->nMagic = 0;
            pPad->nSequence = 0;
            pPad->nLength = 0;
            pPad->nRecordSize = paddingSize;
            pPad->nFlags = LOG_RECORD_FLAG_PADDING;
            pPad->nChecksum = 0;
            pPad->nSenderProcId = 0;
            pPad->nCorrelationId = 0;
#ifdef Windows
            MemoryBarrier();
#else
            __sync_synchronize();
#endif
            pPad->nMagic = TELEPORT_MAGIC;

            currentWrite += paddingSize;
        }

        writeStart = currentWrite;
        m_pChannelHeader->PubHeader.WriteCursor = currentWrite + nRecordSize;
        nextSeq = ++m_pChannelHeader->PubHeader.WriteMsgSeq;

#ifdef Windows
        InterlockedExchange((LONG*)&m_pChannelHeader->PubHeader.SpinLock, 0);
#else
        __sync_lock_release(&m_pChannelHeader->PubHeader.SpinLock);
#endif
        break;
    }

    T_UINT32 recordOffset = (T_UINT32)(writeStart & m_nLogBufferMask);
    TPLogRecordHeader pRec = (TPLogRecordHeader)(m_pShmDataAddr + recordOffset);
    pRec->nMagic = 0;
    pRec->nSequence = 0;

    void* pPayload = (void*)(pRec + 1);
    if (pData && nSizeInByte > 0)
    {
        memcpy(pPayload, pData, nSizeInByte);
    }

    pRec->nLength = nSizeInByte;
    pRec->nRecordSize = nRecordSize;
    pRec->nFlags = nFlags | LOG_RECORD_FLAG_DATA;
    pRec->nSenderProcId = nSenderProcId;
    pRec->nCorrelationId = nCorrelationId;
    pRec->nChecksum = TComputeCRC32(pPayload, nSizeInByte);
    pRec->nMagic = TELEPORT_MAGIC;

#ifdef Windows
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    pRec->nSequence = nextSeq;

#ifdef Windows
    InterlockedCompareExchange64((LONG64*)&m_pChannelHeader->PubHeader.CommitCursor, (LONG64)(writeStart + nRecordSize), (LONG64)writeStart);
#else
    __sync_bool_compare_and_swap(&m_pChannelHeader->PubHeader.CommitCursor, writeStart, writeStart + nRecordSize);
#endif

    nOutMsgId = nextSeq;
    m_pChannelHeader->nOriginalMsgId = nextSeq;
    m_pChannelHeader->nOriginalProcId = nSenderProcId;
    return RC::SUCCESS;
}


//
RC CChannelData::AcquireRingBuffer(T_UINT32 nSizeInByte, T_PVOID& pBuffer, T_UINT64& nToken, T_UINT32 nFlags, T_UINT64 nCorrelationId)
{
    if (nSizeInByte > MAX_LOG_MESSAGE_SIZE)
    {
        return RC::EXCEED_LIMIT;
    }
    T_UINT32 nRecordSize = (sizeof(TLogRecordHeader) + nSizeInByte + 63) & ~63;
    if (nRecordSize > m_nLogBufferSize / 2)
    {
        return RC::EXCEED_LIMIT;
    }

    ChannelPolicy policy = m_pChannelHeader->nPolicy;

    T_UINT64 writeStart = 0;
    T_UINT64 nextSeq = 0;
    T_UINT32 nSpin = 0;
    while (true)
    {
#ifdef Windows
        while (InterlockedCompareExchange((LONG*)&m_pChannelHeader->PubHeader.SpinLock, 1, 0) != 0)
        {
            T_CPU_PAUSE();
        }
#else
        while (!__sync_bool_compare_and_swap(&m_pChannelHeader->PubHeader.SpinLock, 0, 1))
        {
            T_CPU_PAUSE();
        }
#endif

        T_UINT64 currentWrite = m_pChannelHeader->PubHeader.WriteCursor;
        T_UINT64 minOffset = GetMinSubscriberOffset();
        if ((policy == ChannelPolicy::POLICY_BLOCK || policy == ChannelPolicy::POLICY_ISOLATE_SLOW_CONSUMER) &&
            (currentWrite + nRecordSize > minOffset + m_nLogBufferSize))
        {
#ifdef Windows
            InterlockedExchange((LONG*)&m_pChannelHeader->PubHeader.SpinLock, 0);
#else
            __sync_lock_release(&m_pChannelHeader->PubHeader.SpinLock);
#endif
            if (++nSpin < 500)
            {
                T_CPU_PAUSE();
            }
            else if (nSpin < 550)
            {
                T_THREAD_YIELD();
            }
            else
            {
                CleanZombieSubscribers();
                TSleep(1);
            }
            continue;
        }

        T_UINT32 offset = (T_UINT32)(currentWrite & m_nLogBufferMask);
        T_UINT32 remaining = m_nLogBufferSize - offset;

        if (remaining < nRecordSize)
        {
            T_UINT32 paddingSize = remaining;
            TPLogRecordHeader pPad = (TPLogRecordHeader)(m_pShmDataAddr + offset);
            pPad->nMagic = 0;
            pPad->nSequence = 0;
            pPad->nLength = 0;
            pPad->nRecordSize = paddingSize;
            pPad->nFlags = LOG_RECORD_FLAG_PADDING;
            pPad->nChecksum = 0;
            pPad->nSenderProcId = 0;
            pPad->nCorrelationId = 0;
#ifdef Windows
            MemoryBarrier();
#else
            __sync_synchronize();
#endif
            pPad->nMagic = TELEPORT_MAGIC;

            currentWrite += paddingSize;
        }

        writeStart = currentWrite;
        m_pChannelHeader->PubHeader.WriteCursor = currentWrite + nRecordSize;
        nextSeq = ++m_pChannelHeader->PubHeader.WriteMsgSeq;

#ifdef Windows
        InterlockedExchange((LONG*)&m_pChannelHeader->PubHeader.SpinLock, 0);
#else
        __sync_lock_release(&m_pChannelHeader->PubHeader.SpinLock);
#endif
        break;
    }

    T_UINT32 recordOffset = (T_UINT32)(writeStart & m_nLogBufferMask);
    TPLogRecordHeader pRec = (TPLogRecordHeader)(m_pShmDataAddr + recordOffset);
    pRec->nMagic = 0;
    pRec->nSequence = nextSeq;
    pRec->nRecordSize = nRecordSize;
    pRec->nLength = nSizeInByte;
    pRec->nFlags = nFlags | LOG_RECORD_FLAG_DATA;
    pRec->nCorrelationId = nCorrelationId;

    pBuffer = (T_PVOID)(pRec + 1);
    nToken = writeStart;
    return RC::SUCCESS;
}


//
RC CChannelData::CommitRingBuffer(T_UINT64 nToken, T_UINT32 nSizeInByte, T_ID nSenderProcId, T_MSG_ID& nOutMsgId)
{
    T_UINT32 recordOffset = (T_UINT32)(nToken & m_nLogBufferMask);
    TPLogRecordHeader pRec = (TPLogRecordHeader)(m_pShmDataAddr + recordOffset);
    if (nSizeInByte == 0)
    {
        nSizeInByte = pRec->nLength;
    }
    pRec->nLength = nSizeInByte;
    pRec->nSenderProcId = nSenderProcId;
    pRec->nChecksum = TComputeCRC32((void*)(pRec + 1), nSizeInByte);
    if (pRec->nFlags & LOG_RECORD_FLAG_CORRUPT_CRC)
    {
        pRec->nChecksum ^= 0x12345678;
    }
    pRec->nMagic = TELEPORT_MAGIC;

#ifdef Windows
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    T_UINT64 nextSeq = pRec->nSequence;

#ifdef Windows
    InterlockedCompareExchange64((LONG64*)&m_pChannelHeader->PubHeader.CommitCursor, (LONG64)(nToken + pRec->nRecordSize), (LONG64)nToken);
#else
    __sync_bool_compare_and_swap(&m_pChannelHeader->PubHeader.CommitCursor, nToken, nToken + pRec->nRecordSize);
#endif

    nOutMsgId = nextSeq;
    m_pChannelHeader->nOriginalMsgId = nextSeq;
    m_pChannelHeader->nOriginalProcId = nSenderProcId;
    return RC::SUCCESS;
}


//
RC CChannelData::ReadRingMsg(TPAckRecord pSubRecord, T_PVOID& pOutData, T_UINT32& nOutSize, T_MSG_ID& nOutMsgId, T_ID& nOutSenderProcId, T_UINT32& nOutFlags, T_UINT64& nOutCorrelationId)
{
    if (!pSubRecord)
    {
        return RC::INVALID_PARAM;
    }

    T_UINT64 targetSeq = pSubRecord->LastReadSeq + 1;
    ChannelPolicy policy = m_pChannelHeader->nPolicy;

    // Check for POLICY_DROP_OLDEST lap / overrun
    T_UINT64 currentWrite = m_pChannelHeader->PubHeader.WriteCursor;
    if (policy == ChannelPolicy::POLICY_DROP_OLDEST)
    {
        if (currentWrite > pSubRecord->LastReadOffset + m_nLogBufferSize)
        {
            T_UINT64 oldestOffset = (currentWrite - m_nLogBufferSize + 127) & ~63;
            for (T_UINT32 step = 0; step < 2048; step++)
            {
                T_UINT32 off = (T_UINT32)(oldestOffset & m_nLogBufferMask);
                TPLogRecordHeader pCand = (TPLogRecordHeader)(m_pShmDataAddr + off);
                if (pCand->nMagic == TELEPORT_MAGIC && pCand->nSequence > pSubRecord->LastReadSeq)
                {
                    if (!(pCand->nFlags & LOG_RECORD_FLAG_PADDING))
                    {
                        T_UINT64 dropped = pCand->nSequence - targetSeq;
                        pSubRecord->DropCount += (T_UINT32)dropped;
                        pSubRecord->LastReadOffset = oldestOffset;
                        pSubRecord->LastReadSeq = pCand->nSequence - 1;
                        targetSeq = pCand->nSequence;
                        pSubRecord->Status = SUB_STATUS_DROPPED;
                        break;
                    }
                }
                oldestOffset += 64;
            }
        }
    }

    T_UINT32 readOffset = (T_UINT32)(pSubRecord->LastReadOffset & m_nLogBufferMask);
    TPLogRecordHeader pRec = (TPLogRecordHeader)(m_pShmDataAddr + readOffset);

    // Skip PADDING records
    while ((pRec->nFlags & LOG_RECORD_FLAG_PADDING) && pRec->nMagic == TELEPORT_MAGIC)
    {
        pSubRecord->LastReadOffset += pRec->nRecordSize;
        readOffset = (T_UINT32)(pSubRecord->LastReadOffset & m_nLogBufferMask);
        pRec = (TPLogRecordHeader)(m_pShmDataAddr + readOffset);
    }

    // Hybrid Adaptive Wait: Spin -> Yield
    if (pRec->nSequence < targetSeq)
    {
        for (T_UINT32 i = 0; i < 500; i++)
        {
            T_CPU_PAUSE();
            while ((pRec->nFlags & LOG_RECORD_FLAG_PADDING) && pRec->nMagic == TELEPORT_MAGIC)
            {
                pSubRecord->LastReadOffset += pRec->nRecordSize;
                readOffset = (T_UINT32)(pSubRecord->LastReadOffset & m_nLogBufferMask);
                pRec = (TPLogRecordHeader)(m_pShmDataAddr + readOffset);
            }
            if (pRec->nSequence >= targetSeq) break;
        }
        if (pRec->nSequence < targetSeq)
        {
            for (T_UINT32 j = 0; j < 30; j++)
            {
                T_THREAD_YIELD();
                while ((pRec->nFlags & LOG_RECORD_FLAG_PADDING) && pRec->nMagic == TELEPORT_MAGIC)
                {
                    pSubRecord->LastReadOffset += pRec->nRecordSize;
                    readOffset = (T_UINT32)(pSubRecord->LastReadOffset & m_nLogBufferMask);
                    pRec = (TPLogRecordHeader)(m_pShmDataAddr + readOffset);
                }
                if (pRec->nSequence >= targetSeq) break;
            }
        }
    }

    if (pRec->nSequence != targetSeq)
    {
        return RC::TIMEOUT;
    }

    if (pRec->nMagic != TELEPORT_MAGIC)
    {
        LogError("CChannelData::ReadRingMsg: Magic corrupted on seq %llu", targetSeq);
        return RC::FAILED;
    }

    if (pRec->nLength > MAX_LOG_MESSAGE_SIZE)
    {
        LogError("CChannelData::ReadRingMsg: Slot data length exceed limit (%u) on seq %llu", pRec->nLength, targetSeq);
        pSubRecord->LastReadOffset += pRec->nRecordSize;
        pSubRecord->LastReadSeq = targetSeq;
        return RC::FAILED;
    }

    void* pPayload = (void*)(pRec + 1);
    T_UINT32 nCrc = TComputeCRC32(pPayload, pRec->nLength);
    if (nCrc != pRec->nChecksum)
    {
        LogError("CChannelData::ReadRingMsg: CRC mismatch on seq %llu", targetSeq);
        pSubRecord->LastReadOffset += pRec->nRecordSize;
        pSubRecord->LastReadSeq = targetSeq;
        return RC::FAILED;
    }

    pOutData = pPayload;
    nOutSize = pRec->nLength;
    nOutMsgId = targetSeq;
    nOutSenderProcId = pRec->nSenderProcId;
    nOutFlags = pRec->nFlags;
    nOutCorrelationId = pRec->nCorrelationId;

    pSubRecord->LastReadOffset += pRec->nRecordSize;
    pSubRecord->LastReadSeq = targetSeq;

    return RC::SUCCESS;
}



//
RC CChannelData::Realloc(T_UINT32 nSizeInByte)
{
    T_UINT32 nShmSize = m_pSharedMemory->GetSize() - m_nDataOffset;
    T_PSTR   pShmAddr = m_pSharedMemory->Begin();
    if(nSizeInByte <= nShmSize)
    {
        return RC::INVALID_PARAM;
    }
    T_UINT32 nMultiple = 2;
    while (nSizeInByte > nShmSize*nMultiple)
    {
        nMultiple++;
    }
    T_STRING strName = m_pSharedMemory->GetName();
    AccessMode eAccess = m_pSharedMemory->GetMode();

    delete m_pSharedMemory;
    m_pSharedMemory = new SharedMemory(strName.c_str(), nShmSize*nMultiple + m_nDataOffset, eAccess);
    if (!m_pSharedMemory || !m_pSharedMemory->IsValid())
    {
        LogVital("CChannelData::Write Failed to create SharedMemory object.");
    }
    else
    {
        LogInfo("CChannelData::Write: SharedMemory realloc size in byte:%d -> %d",
            nShmSize, nShmSize*nMultiple);
    }
    return RC::SUCCESS;
}


//
T_VOID CBFCrypto::BF_encrypt(BF_LONG* data, const BF_KEY* key)
{
    BF_LONG l, r;
    const BF_LONG* p, * s;

    p = key->P;
    s = &(key->S[0]);
    l = data[0];
    r = data[1];

    l ^= p[0];
    BF_ENC(r, l, s, p[1]);
    BF_ENC(l, r, s, p[2]);
    BF_ENC(r, l, s, p[3]);
    BF_ENC(l, r, s, p[4]);
    BF_ENC(r, l, s, p[5]);
    BF_ENC(l, r, s, p[6]);
    BF_ENC(r, l, s, p[7]);
    BF_ENC(l, r, s, p[8]);
    BF_ENC(r, l, s, p[9]);
    BF_ENC(l, r, s, p[10]);
    BF_ENC(r, l, s, p[11]);
    BF_ENC(l, r, s, p[12]);
    BF_ENC(r, l, s, p[13]);
    BF_ENC(l, r, s, p[14]);
    BF_ENC(r, l, s, p[15]);
    BF_ENC(l, r, s, p[16]);
    r ^= p[BF_ROUNDS + 1];

    data[1] = l & 0xffffffffU;
    data[0] = r & 0xffffffffU;
}

T_VOID CBFCrypto::BF_decrypt(BF_LONG* data, const BF_KEY* key)
{
    BF_LONG l, r;
    const BF_LONG* p, * s;

    p = key->P;
    s = &(key->S[0]);
    l = data[0];
    r = data[1];

    l ^= p[BF_ROUNDS + 1];
    BF_ENC(r, l, s, p[16]);
    BF_ENC(l, r, s, p[15]);
    BF_ENC(r, l, s, p[14]);
    BF_ENC(l, r, s, p[13]);
    BF_ENC(r, l, s, p[12]);
    BF_ENC(l, r, s, p[11]);
    BF_ENC(r, l, s, p[10]);
    BF_ENC(l, r, s, p[9]);
    BF_ENC(r, l, s, p[8]);
    BF_ENC(l, r, s, p[7]);
    BF_ENC(r, l, s, p[6]);
    BF_ENC(l, r, s, p[5]);
    BF_ENC(r, l, s, p[4]);
    BF_ENC(l, r, s, p[3]);
    BF_ENC(r, l, s, p[2]);
    BF_ENC(l, r, s, p[1]);
    r ^= p[0];

    data[1] = l & 0xffffffffU;
    data[0] = r & 0xffffffffU;
}


T_VOID CBFCrypto::BF_cfb64_encrypt(T_PCUCHAR in,
    T_PUCHAR out,
    T_UINT32 length,
    const BF_KEY* schedule,
    T_PUCHAR ivec,
    T_PINT32 num,
    BF_ACTION eAction)
{
    BF_LONG v0, v1, t;
    int n = *num;
    long l = length;
    BF_LONG ti[2];
    unsigned char* iv, c, cc;

    iv = (unsigned char*)ivec;
    if (eAction == BF_ACTION::BF_ENCRYPT) {
        while (l--) {
            if (n == 0) {
                n2l(iv, v0);
                ti[0] = v0;
                n2l(iv, v1);
                ti[1] = v1;
                BF_encrypt((BF_LONG*)ti, schedule);
                iv = (unsigned char*)ivec;
                t = ti[0];
                l2n(t, iv);
                t = ti[1];
                l2n(t, iv);
                iv = (unsigned char*)ivec;
            }
            c = *(in++) ^ iv[n];
            *(out++) = c;
            iv[n] = c;
            n = (n + 1) & 0x07;
        }
    }
    else {
        while (l--) {
            if (n == 0) {
                n2l(iv, v0);
                ti[0] = v0;
                n2l(iv, v1);
                ti[1] = v1;
                BF_encrypt((BF_LONG*)ti, schedule);
                iv = (unsigned char*)ivec;
                t = ti[0];
                l2n(t, iv);
                t = ti[1];
                l2n(t, iv);
                iv = (unsigned char*)ivec;
            }
            cc = *(in++);
            c = iv[n];
            iv[n] = cc;
            *(out++) = c ^ cc;
            n = (n + 1) & 0x07;
        }
    }
    v0 = v1 = ti[0] = ti[1] = t = c = cc = 0;
    *num = n;
}


//
CBFCrypto::CBFCrypto():m_bKeySet(T_FALSE),m_ullIvec(BF_DEFAULT_IVEC)
{
    memset(&m_Key, 0, sizeof(BF_KEY));
}


//
RC CBFCrypto::SetKey(T_UINT64 ullKey, T_UINT64 ullIvec)
{
    BF_set_key(&m_Key, sizeof(T_UINT64), (T_PCUCHAR)&ullKey);
    m_ullIvec = ullIvec;
    m_bKeySet = T_TRUE;
    return RC::SUCCESS;
}


//
CBFCrypto& CBFCrypto::Instance()
{
    static CBFCrypto _cbf_crypto_inst;
    return _cbf_crypto_inst;
}


//
RC CBFCrypto::Encrypt(T_PUCHAR pInData, T_PUCHAR pOutData, T_UINT32 nLengthInByte)
{
    if (!m_bKeySet) 
    {
        return RC::INVALID_CALL;
    }
    T_INT32 nNum = 0;
    T_UINT64 ullIvec = m_ullIvec;
    BF_cfb64_encrypt(pInData, pOutData, nLengthInByte, &m_Key, (T_PUCHAR) &ullIvec, &nNum, BF_ACTION::BF_ENCRYPT);
    return RC::SUCCESS;
}


//
RC CBFCrypto::Decrypt(T_PUCHAR pInData, T_PUCHAR pOutData, T_UINT32 nLengthInByte)
{
    if (!m_bKeySet)
    {
        return RC::INVALID_CALL;
    }
    T_INT32 nNum = 0;
    T_UINT64 ullIvec = m_ullIvec;
    BF_cfb64_encrypt(pInData, pOutData, nLengthInByte, &m_Key, (T_PUCHAR)&ullIvec, &nNum, BF_ACTION::BF_DECRYPT);
    return RC::SUCCESS;
}


//
T_VOID CBFCrypto::BF_set_key(BF_KEY* pKey, T_UINT32 nLen, T_PCUCHAR pData)
{

    T_INT32 i;
    BF_LONG* p, ri, in[2];
    T_PCUCHAR d, end;

    memcpy(pKey, &bf_init, sizeof(BF_KEY));
    p = pKey->P;

    if (nLen > ((BF_ROUNDS + 2) * 4))
        nLen = (BF_ROUNDS + 2) * 4;

    d = pData;
    end = &(pData[nLen]);
    for (i = 0; i < (BF_ROUNDS + 2); i++) {
        ri = *(d++);
        if (d >= end)
            d = pData;

        ri <<= 8;
        ri |= *(d++);
        if (d >= end)
            d = pData;

        ri <<= 8;
        ri |= *(d++);
        if (d >= end)
            d = pData;

        ri <<= 8;
        ri |= *(d++);
        if (d >= end)
            d = pData;

        p[i] ^= ri;
    }

    in[0] = 0L;
    in[1] = 0L;
    for (i = 0; i < (BF_ROUNDS + 2); i += 2) {
        BF_encrypt(in, pKey);
        p[i] = in[0];
        p[i + 1] = in[1];
    }

    p = pKey->S;
    for (i = 0; i < 4 * 256; i += 2) {
        BF_encrypt(in, pKey);
        p[i] = in[0];
        p[i + 1] = in[1];
    }
}
