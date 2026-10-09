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
        T_PCSTR pGlobal = GLOBAL_STR;
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
        LogWarn("BaseEvent::Post() failed to SetEvent");
        return RC::FAILED;
    }
    if(bReset)
    {
        if(!ResetEvent(m_hHandle))
        {
            LogWarn("BaseEvent::Post() failed to ResetEvent");
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
        LogWarn("BaseEvent::Reset() failed to ResetEvent");
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
#ifdef Windows
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
#else
RC BaseMutex::Lock(T_UINT32 nMilliseconds)
{
    if (m_hHandle == T_INVHDL)
    {
        return RC::FAILED;
    }
    pthread_mutex_t* pMutex = (pthread_mutex_t*)m_hHandle;
    int err = 0;
    if (nMilliseconds == INFINITE)
    {
        err = pthread_mutex_lock(pMutex);
    }
    else
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += nMilliseconds / 1000;
        ts.tv_nsec += (nMilliseconds % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        err = pthread_mutex_timedlock(pMutex, &ts);
    }
    if (err == 0)
    {
        return RC::SUCCESS;
    }
    if (err == EOWNERDEAD)
    {
        pthread_mutex_consistent(pMutex);
        return RC::ABANDONED;
    }
    if (err == ETIMEDOUT)
    {
        return RC::TIMEOUT;
    }
    return RC::FAILED;
}


//
RC BaseMutex::TryLock(T_UINT32 nMilliseconds)
{
    if (m_hHandle == T_INVHDL)
    {
        return RC::FAILED;
    }
    pthread_mutex_t* pMutex = (pthread_mutex_t*)m_hHandle;
    int err = pthread_mutex_trylock(pMutex);
    if (err == 0)
    {
        return RC::SUCCESS;
    }
    if (err == EOWNERDEAD)
    {
        pthread_mutex_consistent(pMutex);
        return RC::ABANDONED;
    }
    if (err == EBUSY)
    {
        return RC::TIMEOUT;
    }
    return RC::FAILED;
}


//
RC BaseMutex::Unlock()
{
    if (m_hHandle == T_INVHDL)
    {
        return RC::FAILED;
    }
    pthread_mutex_t* pMutex = (pthread_mutex_t*)m_hHandle;
    if (pthread_mutex_unlock(pMutex) == 0)
    {
        return RC::SUCCESS;
    }
    return RC::FAILED;
}
#endif


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
        LogInfo("CreateFileMappingA：%s with mode:%d size:%d", m_strName, m_eMode, m_nSize);
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
        pTypeStr = (T_PSTR)"VITAL";
        break;
    case LoggerType::LOG_ERROR:
        pTypeStr = (T_PSTR)"ERROR";
        break;
    case LoggerType::LOG_WARNING:
        pTypeStr = (T_PSTR)"WARNING";
        break;
    case LoggerType::LOG_INFO:
        pTypeStr = (T_PSTR)"INFO";
        break;
    case LoggerType::LOG_TRIVIAL:
        pTypeStr = (T_PSTR)"TRIVIAL";
        break;
    default:
        pTypeStr = (T_PSTR)"UNKNOWN";
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


//
T_BOOL TLP::TCheckProcAlive(T_ID nProcId)
{
    if (nProcId == 0)
    {
        return T_FALSE;
    }
    if (nProcId == TGetProcId())
    {
        return T_TRUE;
    }
#ifdef Windows
    HANDLE hProcess = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)nProcId);
    if (!hProcess)
    {
        DWORD dwErr = ::GetLastError();
        if (dwErr == ERROR_ACCESS_DENIED)
        {
            return T_TRUE;
        }
        return T_FALSE;
    }
    DWORD dwWait = ::WaitForSingleObject(hProcess, 0);
    ::CloseHandle(hProcess);
    if (dwWait == WAIT_TIMEOUT)
    {
        return T_TRUE;
    }
    return T_FALSE;
#else
    if (kill((pid_t)nProcId, 0) == 0)
    {
        return T_TRUE;
    }
    return (errno != ESRCH);
#endif
}


//
T_UINT32 TLP::TComputeCRC32(T_PCVOID pData, T_UINT32 nLength)
{
    if (!pData || nLength == 0)
    {
        return 0;
    }
    static const T_UINT32 s_crc32Table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    const T_UINT8* pBytes = (const T_UINT8*)pData;
    T_UINT32 nCrc = 0xFFFFFFFF;
    for (T_UINT32 i = 0; i < nLength; i++)
    {
        nCrc = (nCrc >> 8) ^ s_crc32Table[(nCrc ^ pBytes[i]) & 0xFF];
    }
    return ~nCrc;
}


