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
}


//
CChannelBase::~CChannelBase()
{ 
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


//
RC ITeleport::Open(T_PCSTR strChannelName,
    T_UINT32 eOpenFlag,
    T_ID& nChannelId,
    TLP_CALLBACK cbCallback,
    T_BOOL bGlobal)
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
        pChannel = pChannelMgr->CreateChannel(strChannelName, bGlobal);
        if (!pChannel)
        {
            return RC::FAILED;
        }
    }
    nChannelId = pChannel->GetChannelId();
    pChannel->SetCallBack(cbCallback);
    if ( ((T_USHORT)eOpenFlag & CH_LISTEN)&&
        !pChannel->IsOpenned() )
    {
        rc = pChannel->Subscribe((OpenFlag)eOpenFlag, bGlobal);
    }
    return rc;
}


//
RC ITeleport::Send(T_ID nChannelId, T_PCVOID pData, T_UINT32 nSizeInByte)
{
    if (nSizeInByte > MAX_SHM_SIZE)
    {
        return RC::EXCEED_LIMIT;
    }
    CChannel* pChannel = CChannelMgr::Instance().GetChannelById(nChannelId);
    if (pChannel)
    {
        return pChannel->Publish(pData, nSizeInByte);
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
CChannel* CChannelMgr::GetChannelByName(T_PCSTR pChannelName)
{
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
    if (!strChannelName||!pCallback)
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
CChannel* CChannelMgr::CreateChannel(T_PCSTR strChannelName, T_BOOL bGlobal)
{
    T_ID nChannelId         = MakeChannelId(strChannelName, bGlobal);
    TPChannelRecord pRecord = FindChannelRecordById(nChannelId);
    T_STRING strGUID        = pRecord ? pRecord->Guid : "";
    CChannel* pChannel      = new CChannel(strChannelName, nChannelId, m_hStopEvent, strGUID, bGlobal);
    if(pChannel)
    {
        m_mId2Channels.emplace(nChannelId, pChannel);
        m_mName2Channels.emplace(strChannelName, pChannel);
        TPChannelRecord pRecord = FindChannelRecordById(nChannelId);
        if (!pRecord)
        {
            pRecord = FindAvailableChannelRecord();
            if (pRecord)
            {
                ScopedLock<NamedMutex> Lock(*m_pChannelShmMutex);
                pRecord->ChannelId = nChannelId;
                memcpy(pRecord->Guid, pChannel->GetChannelGuid().c_str(), MAX_GUID);
                pRecord->RefCnt++;
                pRecord->IsGlobal = bGlobal;
            }
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
CChannel::CChannel(T_PCSTR pChannelName, T_ID nChannelId, T_HANDLE hStopEvent, T_STRING strGUID, T_BOOL bGlobal) :
    m_strChannelName(""),
    m_nChannelId(nChannelId),
    m_nMsgId(0),
    m_bActivated(T_TRUE),
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
    m_pChannelData = new CChannelData(m_strNamedObjName.c_str(), DEFAULT_SHM_SIZE);
    if (!m_pChannelData)
    {
        LogVital("CChannel: Failed to create CChannelData object!");
    }
    m_hStopEvent = hStopEvent;
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
RC CChannel::Publish(T_PCVOID pData, T_UINT32 nSizeInByte)
{
    if(!m_bActivated)
    {
        return RC::CLOSED;
    }
    TPubMessage msg = {0};
    msg.pData = malloc(nSizeInByte);
    if(msg.pData)
    {
        memcpy(msg.pData, pData, nSizeInByte);
        msg.nLength = nSizeInByte;
        msg.nOriginalMsgId = MakeMsgId();
        m_qPubQueue.Push(msg);
        return RC::SUCCESS;
    }
    return RC::FAILED;
}


//
RC CChannel::Subscribe(OpenFlag Flag, T_BOOL bGlobal)
{
    
    TPAckRecord pRecord = m_pChannelData->GetFirstAvailRecord();
    if (!pRecord)
    {
        return RC::EXCEED_LIMIT;
    }
    LogInfo("Channel#%d proc %d was subscribed.", m_nChannelId, m_nProcId);
    m_pChannelData->LockHdr();
    pRecord->ProcId           = m_nProcId;
    pRecord->AckFlag       = ACK_FLAG::INIT;
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
    m_pAckRecord->ProcId = 0;
    m_pChannelData->IncDecSubscriber(T_FALSE);
    
    LogInfo("Channel#%d proc %d was unsubscribed.", m_nChannelId, m_nProcId);
    if(0 == nProcId)
    {
        nProcId = m_nProcId;
    }
    RC rc = RemoveSession(GetSessionId());
    CHK_RC(rc);
    if(m_mSessions.size() > 0)
    {
        return rc;
    }
    Stop();
    memset(m_pAckRecord, 0, sizeof(TAckRecord));
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
    Sid.ProcId   = m_nProcId;
    Sid.ThreadId = TGetThreadId();
    return Sid;
}


//
RC CChannel::WaitAllEventDone()
{
    m_bActivated = T_FALSE;
    while (m_qPubQueue.Size() > 0 || m_qCallbackQueue.Size() > 0)
    {
        //LogInfo("Waiting messages to be sent before close.");
        TSleep(1000);
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
    RC result)
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
    m_qCallbackQueue.Push(msg);
    m_pEventCallback->Post(T_TRUE);
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
    // PUB_MSG_INTERVAL will affect the response efficiency on stop event
    // Also, affect the speed publishing messages & CPU usage
    while (WAIT_OBJECT_0 != WaitForSingleObject(m_hStopEvent, PUB_MSG_INTERVAL) && !m_bStopped)
    {
        if (m_qPubQueue.Size() > 0)
        {
            RC rc = m_pChannelData->Lock();
            if (m_qPubQueue.Pop(msg))
            {
                WriteMsg(msg);
            }
            rc = m_pChannelData->Unlock();
            CHK_RC(rc);
            SAFE_FREE_POINTER(msg.pData);
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
    TPChannelShmHeader pHeader = m_pChannelData->GetShmHeader();
    T_MSG_ID nLastMsgId  = 0;
    T_ID     nLastProcId = 0;
    while (WAIT_OBJECT_0 != WaitForSingleObject(m_hStopEvent, 0) && !m_bStopped)
    {
        RC rc = m_pEventSubRead->Wait(1000);
        if (IS_SUCCESS(rc) && m_pAckRecord)
        {
            //Re-entering
            if( (m_pAckRecord->AckFlag != ACK_FLAG::INIT)||
                (nLastMsgId == pHeader->nOriginalMsgId && nLastProcId == pHeader->nOriginalProcId) )
            {
                continue;
            }
            nLastMsgId  = pHeader->nOriginalMsgId;
            nLastProcId = pHeader->nOriginalProcId;
            ReadMsg(nLastProcId, nLastMsgId);
        }
    }
    LogInfo("Channel %s(#%d) RunSubThread() exit.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    m_tSub.StopRunning();
    return RC::SUCCESS;
}


//
RC CChannel::ReadMsg(T_ID nProcId, T_MSG_ID nMsgId)
{
    T_PVOID pData        = T_NULL;
    T_UINT32 nDataLength = 0;
    RC rc                = m_pChannelData->Read(pData, nDataLength);

    if(IS_FAILED(rc))
    {
        LogWarn("Channel %s(#%d) proc#%d Read failed:rc=%d.", 
            m_strChannelName.c_str(), 
            (T_UINT32)m_nChannelId, 
            nProcId,
            rc);

        return rc;
    }
    rc = PutCallbackMsg(MsgType::MSG_SUB_GET,
        nMsgId,
        nProcId,
        pData,
        nDataLength,
        RC::SUCCESS);

    m_pAckRecord->AckFlag = ACK_FLAG::DONE;
    T_BOOL bReadDone = m_pChannelData->SetRead();
    m_pEventSubRead->Reset();
    if (bReadDone)
    {
        m_pEventReadDone->Post(T_FALSE);
    }
    return rc;
}


//
RC CChannel::WriteMsg(TPubMessage& msg)
{
    RC rc = m_pChannelData->Write(msg.pData, msg.nLength);
    if(FAILED(rc)) 
    {
        LogWarn("Channel data write failed: Proc#%d Message:%lld", m_nProcId, msg.nOriginalMsgId);
        OnPubAckFailed(msg.nOriginalMsgId);
        return rc;
    }
    rc = PutCallbackMsg(MsgType::MSG_PUB_PUT, msg.nOriginalMsgId, m_nProcId);
    rc = m_pChannelData->SetUnread(msg.nOriginalMsgId, m_nProcId);
    rc = m_pEventSubRead->Post(T_FALSE);
    rc = m_pEventReadDone->Wait(PUB_ACK_TIMEOUT);

    if (IS_FAILED(rc))
    {
        LogWarn("Read done failed: Proc#%d Message:%lld, try again...", m_nProcId, msg.nOriginalMsgId);
        m_pChannelData->DumpUnread();
        rc = m_pEventSubRead->Post(T_TRUE);
        rc = m_pEventReadDone->Wait(PUB_ACK_TIMEOUT);
        if (IS_FAILED(rc))
        {
            LogWarn("Read done failed again: Proc#%d Message:%lld, give up.", m_nProcId, msg.nOriginalMsgId);
            rc = OnPubAckFailed(msg.nOriginalMsgId);
        }
    }
    m_pEventReadDone->Reset();
    return rc;
}


//
RC CChannel::RunCallback()
{
    TCbMessage msg;
    LogInfo("Channel %s(#%d) RunCallback() start.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    while (WAIT_OBJECT_0 != WaitForSingleObject(m_hStopEvent, 0) && !m_bStopped)
    {
        RC rc = m_pEventCallback->Wait(1000);
        {
            while (m_pCallback && m_qCallbackQueue.Pop(msg))
            {
                m_pCallback(&msg);
                if(msg.eType == MsgType::MSG_SUB_GET)
                {
                    SAFE_FREE_POINTER(msg.pData);
                }
            }
        }
    }
    LogInfo("Channel %s(#%d) RunCallback() exit.", m_strChannelName.c_str(), (T_UINT32)m_nChannelId);
    m_tCallback.StopRunning();
    return RC::SUCCESS;
}


//
CChannelData::CChannelData(T_PCSTR pChannelObjName, T_UINT32 nShmSizeInByte) :
    m_pSharedMemory(T_NULL),
    m_nDataOffset(0)
{
    m_nDataOffset = sizeof(TChannelShmHeader);
    T_STRING strShmName = pChannelObjName;
    strShmName += "_shm";
    m_pSharedMemory = new SharedMemory(strShmName.c_str(), nShmSizeInByte + m_nDataOffset);
    if(!m_pSharedMemory || !m_pSharedMemory->IsValid())
    {
        LogVital("CChannelData::Failed to create SharedMemory object.");
    }
    m_pChannelHdrMutex  = new NamedMutex((strShmName+"_hdrmutex").c_str());
    m_pChannelDataMutex = new NamedMutex((strShmName + "_datamutex").c_str());

    if (!m_pChannelHdrMutex || !m_pChannelDataMutex)
    {
        LogVital("CChannelData::Failed to create named mutex.");
    }
    m_pChannelHeader = (TPChannelShmHeader)m_pSharedMemory->Begin();
    m_pShmDataAddr   = m_pSharedMemory->Begin() + m_nDataOffset;
}


//
CChannelData::CChannelData():
    m_pSharedMemory(T_NULL),
    m_pChannelHdrMutex(T_NULL),
    m_pChannelDataMutex(T_NULL),
    m_pChannelHeader(T_NULL),
    m_pShmDataAddr(T_NULL),
    m_nDataOffset(0)
{
    m_nDataOffset = sizeof(TChannelShmHeader);
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
    
    T_UINT32 nShmSize = m_pSharedMemory->GetSize() - m_nDataOffset;
    //if (nSizeInByte > nShmSize)
    //{
    //    RC rc = Realloc(nSizeInByte);
    //    if(!IS_SUCCESS(rc))
    //    {
    //        return rc;
    //    }
    //    nShmSize = m_pSharedMemory->GetSize() - m_nDataOffset;
    //}
    TTRY
    {
        *((T_PUINT32)m_pShmDataAddr) = nSizeInByte;
        memcpy_s(m_pShmDataAddr + sizeof(T_UINT32),
            nShmSize,
            pData,
            nSizeInByte);
    }
    TEXCEPT_EXECUTE_HANDLER
    {
        LogError("CChannelData::Write failed.");
        return RC::FAILED;
    }
    return RC::SUCCESS;
}


//
RC CChannelData::Read(T_PVOID& pData, T_UINT32& nSizeInByte)
{
    TTRY
    {
        nSizeInByte = *((T_PUINT32)m_pShmDataAddr);
        pData = malloc(nSizeInByte + 2);
        if(!pData)
        {
            return RC::OUT_OF_MEMORY;
        }
        memset(pData, 0, nSizeInByte + 2);
        memcpy_s(pData,
        nSizeInByte,
        m_pShmDataAddr + sizeof(T_UINT32),
        nSizeInByte);
    }
    TEXCEPT_EXECUTE_HANDLER
    {
        pData = T_NULL;
        nSizeInByte = 0;
        return RC::FAILED;
    }
    return RC::SUCCESS;
}


//
RC CChannelData::Lock()
{
    return m_pChannelDataMutex->Lock();
}


//
RC CChannelData::Unlock()
{
    return m_pChannelDataMutex->Unlock();
}


//
RC CChannelData::LockHdr()
{
    return m_pChannelHdrMutex->Lock();
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
    m_pChannelHeader->nOriginalProcId = nProcId;
    m_pChannelHeader->nOriginalMsgId  = nMsgId;
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;
    for (T_USHORT i = 0; i < m_pChannelHeader->nSubscribers; i++)
    {
        if (pRecord->ProcId)
        {
            pRecord->AckFlag = ACK_FLAG::INIT;
        }
        else
        {
            i--;
        }
        pRecord++;
    }
    m_pChannelHeader->nUnreadCnt = m_pChannelHeader->nSubscribers;
    return RC::SUCCESS;
}


//
RC CChannelData::DumpUnread()
{
    LogWarn("Unread process count:%d", m_pChannelHeader->nUnreadCnt);
    TPAckRecord pRecord = m_pChannelHeader->AckRecords;
    for (T_USHORT i = 0; i < m_pChannelHeader->nSubscribers; i++)
    {
        if (pRecord->ProcId)
        {
            if(pRecord->AckFlag == ACK_FLAG::INIT)
            {
                LogWarn("Proc:%d not read yet.", pRecord->ProcId);
            }
        }
        else
        {
            i--;
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
    register BF_LONG l, r;
    register const BF_LONG* p, * s;

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
    register BF_LONG l, r;
    register const BF_LONG* p, * s;

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
    register BF_LONG v0, v1, t;
    register int n = *num;
    register long l = length;
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
