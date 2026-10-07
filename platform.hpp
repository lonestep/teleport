/**
*    File:         platform.hpp
*
*    Desc:
*
*    Author:     lonestep@gmail.com
*    Created:
*/
#pragma once

#include <stdarg.h>
#include <time.h>
#include <fstream>
#include <cstdlib> 
#include <regex>
#include <map>
#include <vector>
#include <queue>
#include <mutex>
#include "typedefs.hpp"
#ifdef Windows
#include <sddl.h>
#include <objbase.h>
#else
#include <sys/time.h>
#endif


namespace TLP 
{

    //
    class BaseObject
    {
    public:
        BaseObject();
        virtual ~BaseObject();

        T_BOOL   IsValid();
        T_HANDLE GetHandle();

    protected:
        T_HANDLE    m_hHandle;

    };


    //
    class BaseNamedObject:public BaseObject
    {
    public:
        BaseNamedObject(T_PCSTR pName);
        virtual ~BaseNamedObject();

    protected:
        //
        RC InitSecurityAttr(SECURITY_ATTRIBUTES& sa);

        T_CHAR        m_strName[MAX_NAME];
        T_BOOL        m_bGlobal;
    };


    //
    class BaseMutex:public BaseObject
    {
    public:
        BaseMutex() {}
        virtual ~BaseMutex(){}

        RC Lock(T_UINT32 nMilliseconds = INFINITE);
        RC TryLock(T_UINT32 nMilliseconds = INFINITE);
        RC Unlock();
    };


    //
    class BaseEvent :public BaseObject
    {
    public:
        BaseEvent() {}
        virtual ~BaseEvent() {}

        RC Post(T_BOOL bReset = T_FALSE);
        RC Wait(T_UINT32 nMilliseconds = INFINITE);
        RC Reset();
    };


    //
    class GenericEvent:public BaseEvent
    {
    public:
        GenericEvent();
        virtual ~GenericEvent();
    };


    //
    class GenericMutex :public BaseMutex
    {
    public:
        GenericMutex();
        virtual ~GenericMutex();

    };


    //
    class NamedEvent:public BaseNamedObject, public BaseEvent
    {
    public:
        NamedEvent(T_PCSTR pName);
        virtual ~NamedEvent() {}
    };


    //
    class NamedMutex:public BaseNamedObject,public BaseMutex
    {
    public:
        NamedMutex(T_PCSTR pName);
        virtual ~NamedMutex() {}
    };

    

#ifdef Windows
#define T_CPU_PAUSE() YieldProcessor()
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#define T_CPU_PAUSE() __builtin_ia32_pause()
#else
#define T_CPU_PAUSE() ((void)0)
#endif


    //
    template <class T> class ScopedLock
    {
    public:
        //
        explicit ScopedLock(T& objMutex) : m_objMutex(objMutex)
        {
            m_rc = m_objMutex.Lock();
        }

        //
        ScopedLock(T& objMutex, long milliseconds) : m_objMutex(objMutex)
        {
            m_rc = m_objMutex.Lock(milliseconds);
        }

        T_BOOL IsLocked() const
        {
            return (m_rc == RC::SUCCESS || m_rc == RC::ABANDONED);
        }

        //
        virtual ~ScopedLock()
        {
            if (IsLocked())
            {
                try {
                    m_objMutex.Unlock();
                }catch (...){
                }
            }
        }

    private:
        RC m_rc;
        T& m_objMutex;
        ScopedLock();
        ScopedLock(const ScopedLock&);
        ScopedLock& operator = (const ScopedLock&) {};
    };


    //
    class SharedMemory:public BaseNamedObject
    {
    public:
        SharedMemory(T_PCSTR pName,
            T_UINT32 nSizeInByte,
            AccessMode eMode = AccessMode::AM_READWRITE);

        virtual ~SharedMemory();

        T_PSTR Begin() const;
        T_PSTR End() const;

        T_UINT32   GetSize();
        AccessMode GetMode();
        T_STRING   GetName();

        T_VOID Map();
        T_VOID Unmap();
        T_VOID Close();


    private:
        SharedMemory();
        SharedMemory(const SharedMemory&);
        //SharedMemory& operator = (const SharedMemory&);

        T_HANDLE   m_hFileHandle;
        T_UINT32   m_nSize;
        AccessMode m_eMode;
        T_BOOL     m_bCreated;
        T_PSTR     m_pAddress;
    };


    //
    class Logger
    {
    public:
        static Logger& Instance();
        
        RC Log(LoggerType eType, T_PCSTR pFormat, ...);

        RC Enable();
        RC Disable();
        RC SetLevel(LoggerType eType);

    private:
        T_BOOL              m_bLoggerEnable;
        LoggerType          m_loggerLevel;
        T_CHAR              m_szBuffer[MAX_BUFFER_LEN];
        std::ofstream       m_ofStream;

        T_PSTR TypeToString(LoggerType eType);
        Logger();
        virtual ~Logger();
        Logger(const Logger&) :m_bLoggerEnable(T_TRUE), m_szBuffer{ 0 }{};
        Logger& operator =(const Logger&) {}
        CRITICAL_SECTION m_cs;

    };


    //!!! NOTE: LogVital() will log and exit the process!
#define LogVital(_pFormat, ...)    Logger::Instance().Log(TLP::LoggerType::LOG_VITAL, _pFormat, ##__VA_ARGS__)
#define LogError(_pFormat, ...)    Logger::Instance().Log(TLP::LoggerType::LOG_ERROR, _pFormat, ##__VA_ARGS__)
#define LogWarn(_pFormat, ...)     Logger::Instance().Log(TLP::LoggerType::LOG_WARNING, _pFormat, ##__VA_ARGS__)
#define LogInfo(_pFormat, ...)     Logger::Instance().Log(TLP::LoggerType::LOG_INFO, _pFormat, ##__VA_ARGS__)
#define LogTrivial(_pFormat, ...)  Logger::Instance().Log(TLP::LoggerType::LOG_TRIVIAL, _pFormat, ##__VA_ARGS__)


    //
    class Thread
    {
    public:
        Thread():m_hThread(T_INVHDL),m_nThreadId(0), m_bRunning(T_FALSE) {}
        virtual ~Thread(){}
        RC Create(PFN_ThreadRoutine, T_PVOID pArgs);
        RC Start();
        RC Stop();
        RC StopRunning();

    private:
        T_HANDLE        m_hThread;
        T_ULONG         m_nThreadId;
        volatile T_BOOL m_bRunning;
    };


    //
    template <class T> class TMsgQueue
    {
    public:
        static const T_UINT32 CAPACITY = 4096;

        TMsgQueue() : m_nHead(0), m_nTail(0), m_nCount(0)
        {
        }

        T_VOID Push(T& r)
        {
            ScopedLock<GenericMutex> lock(m_mQueueMutex);
            if (m_nCount < CAPACITY)
            {
                m_array[m_nTail] = r;
                m_nTail = (m_nTail + 1) % CAPACITY;
                m_nCount++;
            }
        }

        T_BOOL Pop(T& r) 
        {
            T_BOOL bPop = T_FALSE;
            ScopedLock<GenericMutex> lock(m_mQueueMutex);
            if (m_nCount > 0)
            {
                r = m_array[m_nHead];
                m_nHead = (m_nHead + 1) % CAPACITY;
                m_nCount--;
                bPop = T_TRUE;
            }
            return bPop;
        }

        T_UINT64 Size()
        {
            ScopedLock<GenericMutex> lock(m_mQueueMutex);
            return m_nCount;
        }

    private:
        GenericMutex    m_mQueueMutex;
        T_UINT32        m_nHead;
        T_UINT32        m_nTail;
        T_UINT32        m_nCount;
        T               m_array[CAPACITY];
    };


    //Utilities
    T_ID        TGetProcId();
    T_ID        TGetThreadId();
    T_UINT32    TGetError();
    T_PTSTR     TGetErrorMessage(T_UINT32 nErrorCode);
    T_VOID      TSleep(T_UINT32 nMilliseconds);
    T_VOID      TFree(T_PVOID pBuffer);
    T_STRING    TMakeGuid();
    T_HANDLE    TCreateEvent();
    T_BOOL      TSetEvent(T_HANDLE hEvent);
    T_UINT32    BKDRHash(T_PCSTR str);
    T_BOOL      TFileExist(T_PCSTR pDirFile);
    T_BOOL      TMakeDirectory(T_PCSTR pDirPathName);
    RC          TShellRun(T_PCSTR pFile, T_PCSTR pParams);
    T_BOOL      TCheckProcAlive(T_ID nProcId);
    T_UINT32    TComputeCRC32(T_PCVOID pData, T_UINT32 nLength);

};