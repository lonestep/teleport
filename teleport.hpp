/**
*    File:         teleport.hpp
*
*    Desc:
*
*    Author:       lonestep@gmail.com
*    Created:     
*/
#pragma once
#include "platform.hpp"

namespace TLP
{

    //
    class CChannelBase
    {
    public:
        CChannelBase();
        virtual ~CChannelBase();
        virtual RC SetCallBack(TLP_CALLBACK cb);
        RC Start();
        RC Stop();
    protected:

        virtual RC RunPubThread() { return RC::SUCCESS; };
        virtual RC RunCallback()  { return RC::SUCCESS; };
        virtual RC RunSubThread() { return RC::SUCCESS; };

        volatile T_BOOL           m_bRunning;
        volatile T_BOOL           m_bStopped;
        T_HANDLE                  m_hStopEvent;
        TLP_CALLBACK              m_pCallback;
        Thread                    m_tPub;
        Thread                    m_tSub;
        Thread                    m_tCallback;



    private:

        static RC SubRunWrapper(T_PVOID pThis);
        static RC PubRunWrapper(T_PVOID pThis);
        static RC CallbackRunWrapper(T_PVOID pThis);

    };


    //
    class CChannelData
    {
    public:
        CChannelData();
        CChannelData(T_PCSTR pChannelObjName, T_UINT32 nShmSizeInByte);
        virtual ~CChannelData();

        RC Write(T_PCVOID pData, T_UINT32 nSizeInByte);
        RC Read(T_PVOID &pData, T_UINT32& nSizeInByte);
        RC Lock();
        RC Unlock();
        RC LockHdr();
        RC UnlockHdr();

        TPAckRecord           ReadAckRecords();
        TPChannelShmHeader    GetShmHeader();
        TPAckRecord           GetFirstAvailRecord();
        T_BOOL                SetRead();
        RC                    SetUnread(T_MSG_ID nMsgId, T_ID nProcId);
        RC                    DumpUnread();
        RC                    IncDecSubscriber(T_BOOL bIncrease);

    protected:
        RC Realloc(T_UINT32 nSizeInByte);
    private:
        SharedMemory*        m_pSharedMemory;
        UINT32               m_nDataOffset;
        TPChannelShmHeader   m_pChannelHeader;
        NamedMutex*          m_pChannelHdrMutex;
        NamedMutex*          m_pChannelDataMutex;
        T_PSTR               m_pShmDataAddr;
    };


    //
    class CChannel:public CChannelBase
    {
    public:
        volatile CChannel(T_PCSTR pChannelName, T_ID nChannelId, T_HANDLE hStopEvent, T_STRING strGUID, T_BOOL bGlobal);

        virtual ~CChannel();

        RC Publish(T_PCVOID pData, T_UINT32 nSizeInByte);
        RC Subscribe(OpenFlag nFlag, T_BOOL bGlobal);
        RC Unsubscribe(T_ID nProcId);

        T_ID     GetChannelId();
        T_PCSTR  GetChannelName();
        T_PCSTR  GetChannelObjName();
        T_STRING GetChannelGuid();
        T_BOOL   IsOpenned();
        RC       WaitAllEventDone();
        T_SHORT  GetSubscriberCount();
        

    protected:
        
        RC OnPubAckFailed(T_MSG_ID nMsgId);
        RC PutCallbackMsg(MsgType msgType, 
            T_MSG_ID nMsgId,
            T_ID nProcId,
            T_PVOID pData = T_NULL, 
            T_UINT32 nLength = 0, 
            RC result = RC::SUCCESS);

        RC   ResetAckRecords(T_UINT32 nDataLength, T_MSG_ID nOriginMsgId);
        RC   ReadMsg(T_ID nProcId, T_MSG_ID nMsgId);
        RC   WriteMsg(TPubMessage& msg);
        RC   MakeGuid();
        T_ID MakeMsgId();
        RC   MakeNamedObjName();
        RC   AddSession(OpenFlag eFlag, TPAckRecord pRecord);
        RC   RemoveSession(TSessionId Sid);
        TSessionId GetSessionId();

        virtual RC RunPubThread() override;
        virtual RC RunSubThread() override;
        virtual RC RunCallback() override;

    private:
        T_BOOL                  m_bGlobal;
        T_BOOL                  m_bActivated;
        T_ID                    m_nChannelId;
        T_ID                    m_nProcId;
        T_ID                    m_nMsgId;
        T_STRING                m_strGUID;
        T_STRING                m_strChannelName;
        T_STRING                m_strNamedObjName;
        TPAckRecord             m_pAckRecord;
        T_SESSION_MAP           m_mSessions;
        CChannelData*           m_pChannelData;
        GenericMutex            m_mMapMutex;
        GenericEvent*           m_pEventCallback;
        NamedEvent*             m_pEventSubRead;
        NamedEvent*             m_pEventReadDone;
        TMsgQueue<TCbMessage>   m_qCallbackQueue;
        TMsgQueue<TPubMessage>  m_qPubQueue;
    };

    typedef std::vector<CChannel*>                T_CHANNEL_VECTOR;
    typedef std::map<T_ID, CChannel*>             T_CHANNEL_ID_MAP;
    typedef std::map<T_STRING, CChannel*>         T_CHANNEL_NAME_MAP;


    //
    class CChannelMgr
    {
    public:

        CChannel*    GetChannelByName(T_PCSTR strChannelName);
        CChannel*    GetChannelById(T_ID nChannelId);
        RC           ValidateChannelParam(T_PCSTR strChannelName, T_UINT32 nFlag, TLP_CALLBACK pCallback);

        static CChannelMgr& Instance();

        CChannel*                CreateChannel(T_PCSTR strChannelName, T_BOOL bGlobal);
        RC                       ShiftToGlobal();
        TPChannelRecord          FindAvailableChannelRecord();
        TPChannelRecord          FindChannelRecordById(T_ID nChannelId);
        RC                       IncDecChannelRecordRef(T_ID nChannelId, T_BOOL bIncrease);
        T_ID                     MakeChannelId(T_PCSTR strChannelName, T_BOOL bGlobal);

    protected:
        
        T_STRING    MakeObjectName();
        RC          CreateNamedObject();

    private:
        volatile CChannelMgr();
        virtual ~CChannelMgr();
        

        T_CHANNEL_ID_MAP        m_mId2Channels;
        T_CHANNEL_NAME_MAP      m_mName2Channels;
        SharedMemory*           m_pChannelShm;
        NamedMutex*             m_pChannelShmMutex;
        T_UINT32                m_nMaxProc;
        T_HANDLE                m_hStopEvent;
        T_BOOL                  m_bGlobal;
    };


    //
    class CBFCrypto
    {
    public:

        static CBFCrypto& Instance();
        RC    SetKey(T_UINT64 ullKey, T_UINT64 ullIvec = BF_DEFAULT_IVEC);
        RC    Encrypt(T_PUCHAR pInData, T_PUCHAR pOutData, T_UINT32 nLengthInByte);
        RC    Decrypt(T_PUCHAR pInData, T_PUCHAR pOutData, T_UINT32 nLengthInByte);

    protected:
        T_VOID BF_set_key(BF_KEY* pKey, T_UINT32 nLen, T_PCUCHAR pData);
        T_VOID BF_encrypt(BF_LONG* data, const BF_KEY* key);
        T_VOID BF_decrypt(BF_LONG* data, const BF_KEY* key);
        T_VOID BF_cfb64_encrypt(T_PCUCHAR pInData,
            T_PUCHAR pOutData,
            T_UINT32 nLength,
            const BF_KEY* pSchedule,
            T_PUCHAR szIvec,
            T_PINT32 nNum,
            BF_ACTION eAction);

    private:
        CBFCrypto();
        virtual ~CBFCrypto() {};
        T_UINT64    m_ullIvec;
        T_INT32     m_nNum;
        BF_KEY      m_Key;
        T_BOOL      m_bKeySet;
    };


    //
    interface ITeleport
    {
    public:
        //
        // strChannelName [IN]: Arbitrary ansic string indicating the channel, without slash "\\". 
        //                      no more than MAX_NAME(128) characters.
        // 
        // eOpenFlag [IN]:        Bit wise flag indicating the purpose openning this channel:
        //                             OpenFlag::READ              -  For publishing message
        //                             OpenFlag::WRITE             -  To get message from this channel
        //                             OpenFlag::READWRITE         -  Both
        //                             OpenFLag::CREATE_IF_NOEXIST - Create the channel if it's not exist
        //                             OpenFlag::ALL               -  ALL = READWRITE|CREATE_IF_NOEXIST
        // 
        // nChannelId [OUT]:    The id of the openned channel
        // cbCallback [IN]:     The callback processing channel message, see PTCbMessage 
        // 
        // bGlobal [IN]:        Indicating whether this is a global channel
        //                      For more of Global object in Windows, ref. to:
        //                      https://docs.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces
        static RC Open(T_PCSTR          strChannelName, 
                       T_UINT32         eOpenFlag, 
                       T_ID&            nChannelId, 
                       TLP_CALLBACK     cbCallback, 
                       T_BOOL           bGlobal = T_FALSE);

        //
        // nChannelId:  The ID of channel to send 
        // pData:       The buffer to be sent to channel
        // nSizeInByte: The length in byte of pData
        //
        static RC Send(T_ID nChannelId, T_PCVOID pData, T_UINT32 nSizeInByte);

        //
        // bSendMsgBeforeClose: 
        //  TRUE: Send buffered messages in queue before closing the channel
        //  FALSE: Discard buffered messages in queue and close the channel
        //
        static RC Close(T_ID nChannelId, T_BOOL bSendMsgBeforeClose = T_FALSE);
    };

};
