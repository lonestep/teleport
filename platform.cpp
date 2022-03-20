/**
*    File:         platform.hpp
*
*    Desc:
*
*    Author:     lonestep@gmail.com
*    Created:
*/
#include "platform.hpp"

using namespace TLP;


//
T_BOOL BaseObject::IsValid()
{
    return T_BOOL(m_hHandle != T_INVHDL);
}


//
T_HANDLE BaseObject::GetHandle()
{
    return m_hHandle;
}


//
BaseObject::BaseObject():
    m_hHandle(T_INVHDL)
{
}


//
BaseObject::~BaseObject()
{
    SAFE_CLOSE_HANDLE(m_hHandle);
}


//
BaseNamedObject::BaseNamedObject(T_PCSTR pName) :
    BaseObject::BaseObject(),
    m_bGlobal(T_FALSE),
    m_strName{}
{
    if (T_NULL != pName)
    {
        memset(m_strName, 0, MAX_NAME);
        strcpy_s(m_strName, MAX_NAME, pName);
        T_PSTR pGlobal = GLOBAL_STR;
        m_bGlobal = !_strnicmp(m_strName, pGlobal, strlen(pGlobal));
    }
    else
    {
        LogVital("BaseNamedObject(): pName could not be null!");
    }
}


//
BaseNamedObject::~BaseNamedObject()
{    
}


//WINDOWS SPECIFIC
BOOL SetPrivilege(
    HANDLE hToken,          // access token handle
    LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
    BOOL bEnablePrivilege   // to enable or disable privilege
)
{
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!LookupPrivilegeValue(
        NULL,            // lookup privilege on local system
        lpszPrivilege,   // privilege to lookup 
        &luid))          // receives LUID of privilege
    {
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    // Enable the privilege or disable all privileges.

    if (!AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tp,
        sizeof(TOKEN_PRIVILEGES),
        (PTOKEN_PRIVILEGES)NULL,
        (PDWORD)NULL))
    {
        return FALSE;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)

    {
        return FALSE;
    }

    return TRUE;
}

//
RC BaseNamedObject::InitSecurityAttr(SECURITY_ATTRIBUTES& sa)
{
    ZeroMemory(&sa, sizeof(sa));

    sa.nLength  = sizeof(sa);
    T_BOOL bRet = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;IU)",
        SDDL_REVISION_1,
        &sa.lpSecurityDescriptor,
        NULL);

    if (!bRet) 
    {
        LogError("InitSecurityAttr failed:");
        return RC::FAILED;
    }
    return RC::SUCCESS;
}


//
GenericEvent::GenericEvent()
{
    m_hHandle = CreateEventA(T_NULL, T_TRUE, T_FALSE, T_NULL);
    if (T_INVHDL == m_hHandle)
    {
        LogVital("Unable to create generic event!");
    }
}


//
GenericEvent::~GenericEvent()
{
}


//
NamedEvent::NamedEvent(T_PCSTR pName):
    BaseNamedObject::BaseNamedObject(pName)
{
    PSECURITY_ATTRIBUTES pSecAttr = T_NULL;
    SECURITY_ATTRIBUTES sa;
    if (m_bGlobal)
    {
        RC rc = InitSecurityAttr(sa);
        if (IS_SUCCESS(rc))
        {
            pSecAttr = &sa;
        }
    }

    BaseEvent::m_hHandle = CreateEventExA(pSecAttr,  
        m_strName, 
        CREATE_EVENT_MANUAL_RESET, 
        EVENT_MODIFY_STATE | SYNCHRONIZE);

    if(T_NULL == BaseEvent::m_hHandle)
    {
        LogVital("Unable to create named event!");
    }
    else
    {
        LogInfo("Named event:%s created!", m_strName);
    }
}


//
RC BaseEvent::Post(T_BOOL bReset)
{
    if (!SetEvent(m_hHandle))
    {
        return RC::FAILED;
    }
    if(bReset)
    {
        if(!ResetEvent(m_hHandle))
        {
            return RC::FAILED;
        }
    }
    return RC::SUCCESS;
}


//
RC BaseEvent::Reset()
{
    if (!ResetEvent(m_hHandle))
    {
        return RC::FAILED;
    }
    return RC::SUCCESS;
}


//
RC BaseEvent::Wait(T_UINT32 nMilliseconds)
{

    switch (WaitForSingleObject(m_hHandle, nMilliseconds))
    {
    case WAIT_OBJECT_0:
        return RC::SUCCESS;
    default:
        return RC::FAILED;
    }
}


//
GenericMutex::GenericMutex()
{
    m_hHandle = CreateMutexA(NULL, FALSE, NULL);
    if (T_INVHDL == m_hHandle)
    {
        LogVital("Unable to create generic mutex!");
    }
}


//
GenericMutex::~GenericMutex()
{
}


//
NamedMutex::NamedMutex(T_PCSTR pName) :
    BaseNamedObject::BaseNamedObject(pName)
{

    if (m_bGlobal)
    {
        SECURITY_ATTRIBUTES sa;
        RC rc = InitSecurityAttr(sa);
        if (IS_SUCCESS(rc))
        {
            BaseMutex::m_hHandle = CreateMutexA(&sa, FALSE, m_strName);
        }
    }
    else
    {
        BaseMutex::m_hHandle = CreateMutexA(NULL, FALSE, m_strName);
    }
    if (T_INVHDL == BaseMutex::m_hHandle)
    {
        LogVital("Unable to create named mutex!");
    }
}


//
RC BaseMutex::Lock(T_UINT32 nMilliseconds)
{
    switch (WaitForSingleObject(m_hHandle, nMilliseconds))
    {
    case WAIT_OBJECT_0:
        return RC::SUCCESS;
    case WAIT_ABANDONED:
        return RC::ABANDONED;
    case WAIT_TIMEOUT:
        return RC::TIMEOUT;
    default:
        LogError("WaitForSingleObject error:%d", GetLastError());
        return RC::FAILED;
    }
}


//
RC BaseMutex::TryLock(T_UINT32 nMilliseconds)
{
    switch (WaitForSingleObject(m_hHandle, nMilliseconds))
    {
    case WAIT_OBJECT_0:
        return RC::SUCCESS;
    case WAIT_TIMEOUT:
        return RC::TIMEOUT;
    case WAIT_ABANDONED:
        return RC::ABANDONED;
    default:
        return RC::FAILED;
    }
}


//
RC BaseMutex::Unlock()
{
    if (ReleaseMutex(m_hHandle)) 
    {
        return RC::SUCCESS;
    }
    return RC::FAILED;
}


//
SharedMemory::SharedMemory(T_PCSTR pName,
    T_UINT32 nSize,
    AccessMode eMode) :
    BaseNamedObject::BaseNamedObject(pName),
    m_bCreated(T_FALSE),
    m_hFileHandle(T_INVHDL),
    m_nSize(nSize),
    m_eMode(eMode),
    m_pAddress(T_NULL)
{

    //Try to open
    m_hHandle = OpenFileMappingA((m_eMode == AccessMode::AM_READWRITE) ? FILE_MAP_WRITE | FILE_MAP_READ : FILE_MAP_READ, T_FALSE, m_strName);
    if(!m_hHandle)
    {
        if (m_bGlobal)
        {
            SECURITY_ATTRIBUTES sa;
            RC rc = InitSecurityAttr(sa);
            if (!IS_SUCCESS(rc))
            {
                return;
            }
            T_HANDLE hToken = T_INVHDL;
            if (!OpenProcessToken(GetCurrentProcess(),
                TOKEN_ADJUST_PRIVILEGES,
                &hToken))
            {
                LogVital("SharedMemory():Failed to open process token!");
                return;
            }

            if (SetPrivilege(hToken, SE_CREATE_GLOBAL_NAME, T_TRUE))
            {
                m_hHandle = CreateFileMappingA(T_INVHDL, 
                    &sa, 
                    (T_UINT32)m_eMode, 
                    0, 
                    m_nSize, 
                    m_strName);
            }
        }
        else
        {
            m_hHandle = CreateFileMappingA(T_INVHDL, 
                T_NULL, 
                (T_UINT32)m_eMode,
                0, 
                m_nSize, 
                m_strName);
        }
    }
    if(!m_hHandle)
    {
        LogVital("SharedMemory():Failed to create file mapping.");
    }
    else
    {
        LogInfo("CreateFileMappingA£º%s with mode:%d size:%d", m_strName, m_eMode, m_nSize);
    }
    Map();
}


//
SharedMemory::~SharedMemory()
{
    Unmap();
    Close();
}


//
T_VOID SharedMemory::Map()
{
    T_PVOID pAddr = MapViewOfFile(m_hHandle, 
        (m_eMode == AccessMode::AM_READWRITE)? FILE_MAP_WRITE| FILE_MAP_READ: FILE_MAP_READ,
        0, 
        0, 
        m_nSize);

    if (!pAddr)
    {
        LogVital("SharedMemory::Map():Failed to map view of file, size:%d.", m_nSize);
    }
    else
    {
        LogInfo("MapViewOfFile success with size:%d", m_nSize);
    }
    m_pAddress = static_cast<T_PSTR>(pAddr);
}


//
T_PSTR SharedMemory::Begin() const
{
    return m_pAddress;
}


//
T_PSTR SharedMemory::End() const
{
    return m_pAddress + m_nSize;
}


//
T_UINT32 SharedMemory::GetSize()
{
    return m_nSize;
}


//
T_VOID SharedMemory::Unmap()
{
    if (m_pAddress)
    {
        UnmapViewOfFile(m_pAddress);
        m_pAddress = T_NULL;
    }
}


//
AccessMode SharedMemory::GetMode()
{
    return m_eMode;
}




//
T_VOID SharedMemory::Close()
{
    SAFE_CLOSE_HANDLE(m_hFileHandle);
}


//
T_STRING SharedMemory::GetName()
{
    return m_strName;
}


//
Logger::Logger() :
    m_bLoggerEnable(T_TRUE),
    m_loggerLevel(LoggerType::LOG_ALL),
    m_szBuffer{0}
{
    time_t t    = time(T_NULL);
    tm t1        = { 0 };
    localtime_s(&t1, &t);
    T_STRING strFormat = "%s\\teleport_proc";
    strFormat += I64_FMT;
    sprintf_s(m_szBuffer, strFormat.c_str(), LOG_DIR, TLP::TGetProcId());
    T_UINT32 nLen = (T_UINT32)strlen(m_szBuffer);
    strftime(m_szBuffer + nLen, MAX_BUFFER_LEN, "_%Y%m%d_%H%M%S.log", &t1);
    TMakeDirectory(LOG_DIR);
    m_ofStream.open(m_szBuffer);
    if(!m_ofStream.is_open())
    {
        m_bLoggerEnable = T_FALSE;
    }
    InitializeCriticalSection(&m_cs);
}


//
Logger::~Logger()
{
    m_ofStream.close();
}


//
Logger& Logger::Instance()
{
    static Logger _logger_inst;
    return _logger_inst;
}


//
RC Logger::Log(LoggerType eType, T_PCSTR pFormat, ...)
{
    
    if(m_bLoggerEnable)
    {
        if(eType > m_loggerLevel)
        {
            return RC::ABANDONED;
        }
        EnterCriticalSection(&m_cs);
        try 
        {
            time_t t = time(T_NULL);
            tm t1    = { 0 };
            localtime_s(&t1, &t);
            va_list l;
            va_start(l, pFormat);
            strftime(m_szBuffer, MAX_BUFFER_LEN, "%Y-%m-%d %H:%M:%S", &t1);
            m_ofStream << m_szBuffer;
            m_ofStream << " [" << TypeToString(eType) << "] ";
            vsprintf_s(m_szBuffer, pFormat, l);
            m_ofStream << m_szBuffer;

            if ( (eType == LoggerType::LOG_ERROR) ||
                (eType == LoggerType::LOG_VITAL) )
            {
                T_UINT32 nErrCode = TGetError();
                T_PTSTR  pErrMsg  = TGetErrorMessage(nErrCode);
                if (nErrCode && pErrMsg)
                {
                    m_ofStream << " Error:(" << nErrCode << ")" << pErrMsg;
                    TFree(pErrMsg);
                }
            }
            m_ofStream << "\n";
            va_end(l);
            m_ofStream.flush();
        }
        catch(...)
        {
            LeaveCriticalSection(&m_cs);
            return RC::FAILED;
        }
        LeaveCriticalSection(&m_cs);
        if (eType == LoggerType::LOG_VITAL)
        {
            exit(-1);
        }
    }
    
    return RC::SUCCESS;
}


//
T_PSTR Logger::TypeToString(LoggerType eType)
{
    T_PSTR pTypeStr = T_NULL;
    switch (eType)
    {
    case LoggerType::LOG_VITAL:
        pTypeStr = "VITAL";
        break;
    case LoggerType::LOG_ERROR:
        pTypeStr = "ERROR";
        break;
    case LoggerType::LOG_WARNING:
        pTypeStr = "WARNING";
        break;
    case LoggerType::LOG_INFO:
        pTypeStr = "INFO";
        break;
    case LoggerType::LOG_TRIVIAL:
        pTypeStr = "TRIVIAL";
        break;
    default:
        pTypeStr = "UNKNOWN";
    }
    return pTypeStr;
}


//
RC Logger::Enable()
{
    m_bLoggerEnable = T_TRUE;
    return RC::SUCCESS;
}


//
RC Logger::Disable()
{
    m_bLoggerEnable = T_FALSE;
    return RC::SUCCESS;
}


//
RC Logger::SetLevel(LoggerType eType)
{
    m_loggerLevel = eType;
    return RC::SUCCESS;
}


//
RC Thread::Create(PFN_ThreadRoutine pRoutineAddr, T_PVOID pArgs) 
{
#ifdef Windows
    m_hThread = CreateThread( T_NULL, 
        0, 
        (LPTHREAD_START_ROUTINE)pRoutineAddr, 
        pArgs, 
        CREATE_SUSPENDED, 
        &m_nThreadId );

    if(T_NULL == m_hThread)
    {
        LogError("Failed to create thread!");
        return RC::FAILED;
    }
    return RC::SUCCESS;
#else
    return RC::NOT_IMPLEMENTED;
#endif
    
}


//
RC Thread::Start()
{
    m_bRunning = T_TRUE;
#if Windows
    if(0 >  ResumeThread(m_hThread))
    {
        return RC::FAILED;
    }
    return RC::SUCCESS;
#else
#endif
    return RC::NOT_IMPLEMENTED;
}


//
RC Thread::Stop()
{
    while (m_bRunning)
    {
        LogInfo("Stop thread#%d, trying...", m_nThreadId);
        TSleep(50);
    }
    LogInfo("Thread #%d Stopped.", m_nThreadId);
    return RC::SUCCESS;
}


//
RC Thread::StopRunning()
{
    m_bRunning = T_FALSE;
    return RC::SUCCESS;
}


//
T_ID TLP::TGetProcId()
{
#ifdef Windows
    return (T_ID)::GetCurrentProcessId();
#else
    return 0;
#endif
}

//
T_ID TLP::TGetThreadId()
{
#ifdef Windows
    return (T_ID)::GetCurrentThreadId();
#else
    return 0;
#endif
}

//
T_UINT32 TLP::TGetError()
{
#ifdef Windows
    return GetLastError();
#else
    return 0;
#endif
}


//
T_PTSTR TLP::TGetErrorMessage(T_UINT32 nErrorCode)
{
    if (0 == nErrorCode)
    {
        return T_NULL;
    }
#ifdef Windows
    DWORD dwFlg = FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS;

    LPTSTR lpMsgBuf = 0;
    if (FormatMessage(dwFlg, 0, nErrorCode, 0, (LPTSTR)&lpMsgBuf, 0, NULL))
    {
        return lpMsgBuf;
    }
    return T_NULL;
#else
    return T_NULL;
#endif
}


//
T_VOID TLP::TSleep(UINT32 nMilliseconds)
{
#ifdef Windows
    ::Sleep(nMilliseconds);
#else
#endif
}


//
T_VOID TLP::TFree(T_PVOID pBuffer)
{
#ifdef Windows
    LocalFree(pBuffer);
#else
    free(pBuffer);
#endif
    pBuffer = T_NULL;
}


//
std::string TLP::TMakeGuid()
{
#if Windows
    char szBuffer[MAX_GUID] = { 0 };
    GUID guid;
    HRESULT hr = CoCreateGuid(&guid);
    if(SUCCEEDED(hr))
    {
        sprintf_s(szBuffer, sizeof(szBuffer),
            "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            guid.Data1, guid.Data2,
            guid.Data3, guid.Data4[0],
            guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4],
            guid.Data4[5], guid.Data4[6],
            guid.Data4[7]);
        return std::string(szBuffer);
    }
#endif
    return std::string();

}


//
T_HANDLE TLP::TCreateEvent()
{
#if Windows
    return CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    return T_INVHDL;
#endif
}


//
T_BOOL TLP::TSetEvent(T_HANDLE hEvent)
{
#ifdef Windows
    return SetEvent(hEvent);
#else
    return T_FALSE;
#endif
}

T_BOOL TLP::TMakeDirectory(T_PCSTR pDirPathName)
{
    if (TFileExist(pDirPathName))
        return T_FALSE;
#if Windows
    return CreateDirectory(pDirPathName, T_NULL);
#else
    return RC::NOT_IMPLEMENTED;
#endif
}

T_BOOL TLP::TFileExist(T_PCSTR pDirFile)
{
    std::fstream f;
    f.open(pDirFile, std::ios::in);
    return f.is_open();
}


//
T_UINT32 TLP::BKDRHash(T_PCSTR str)
{
    T_UINT32 seed = 13131;
    T_UINT32 hash = 0;

    while (*str)
    {
        hash = hash * seed + (*str++);
    }
    return (hash & 0x7FFFFFFF);
}


//
RC TLP::TShellRun(T_PCSTR pFile, T_PCSTR pParams)
{
#ifdef Windows
    SHELLEXECUTEINFO ShExecInfo = { 0 };
    ShExecInfo.cbSize       = sizeof(SHELLEXECUTEINFO);
    ShExecInfo.fMask        = SEE_MASK_NOCLOSEPROCESS;
    ShExecInfo.lpFile       = pFile;
    ShExecInfo.lpParameters = pParams;
    ShExecInfo.nShow        = SW_SHOW;
    if(ShellExecuteEx(&ShExecInfo))
    {
        return RC::SUCCESS;
    }
    return RC::FAILED;
#else
    return RC::NOT_IMPLEMENTED;
#endif
}


