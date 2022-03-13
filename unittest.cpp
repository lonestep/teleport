#include <iostream>
#include "teleport.hpp"

using namespace TLP;
using namespace std;

T_UINT32 g_nMsgRecieved = 0;
std::map<T_ID, T_MSG_ID> g_proc2msgid;


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
		LogInfo("Put Message(#%lld) to shared memory by channel(%d) process(%d)", pMessage->nOriginalMsgId, pMessage->nChannelId, pMessage->nProcessId);
		break;
	case MsgType::MSG_PUB_ACK:
		if (IS_FAILED(pMessage->eResult)) 
		{
			LogError("Message(#%lld was NOT acknowldeged by channel(%d) process(%d)", pMessage->nOriginalMsgId, pMessage->nChannelId, pMessage->nProcessId);
		}
		break;
	case MsgType::MSG_SUB_GET:
		if(g_proc2msgid.find(pMessage->nProcessId) == g_proc2msgid.end())
		{
			g_proc2msgid[pMessage->nProcessId] = 0;
		}
		g_nMsgRecieved++;
		
		if(pMessage->nOriginalMsgId != g_proc2msgid[pMessage->nProcessId] +1)
		{
			LogError("Inconsistency detected:LastMsg:%lld MsgId:%lld", g_proc2msgid[pMessage->nProcessId], pMessage->nOriginalMsgId);
		}
		g_proc2msgid[pMessage->nProcessId] = pMessage->nOriginalMsgId;
		LogInfo("Got message(#%lld) from channel(%d) process(%d):%s", pMessage->nOriginalMsgId, pMessage->nChannelId, pMessage->nProcessId, pMessage->pData);
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
	printf("teleport [unittest|listen|send]");
	exit(-1);
}


//
T_VOID RunProcessWithArg(T_PSTR pPath, T_UINT32 nProcs, T_PSTR pArg)
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
#define TEST_PLAIN_TEXT "Genius Shawn said:Feed face bee bacca!"
T_VOID UT_TestBFCrypto()
{

	T_UCHAR		szPlaintext[]						= { TEST_PLAIN_TEXT };
	T_UINT32	nLen								= sizeof(szPlaintext);
	T_UCHAR		szCiphertext[sizeof(szPlaintext)]	= { 0 };
	T_UINT32	nLoop								= 1000;

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
int main(int argc, char** argv)
{	
	UT_TestBFCrypto();
	UT_TestThread();
	Logger::Instance().SetLevel(LoggerType::LOG_INFO);
	T_PCSTR  pTopic			= "some_topic";
	T_UINT32 nMsgSend		= 1000;
	T_UINT32 nListenProcs	= 100;
	T_UINT32 nSendProcs		= 50;
	T_BOOL   bGlobal		= T_TRUE;

	
	if( argc == 2)
	{
		T_PCSTR pCmd = argv[1];
		if(0 == _stricmp(pCmd, "unittest"))
		{
			RunProcessWithArg(argv[0], nListenProcs, "listen");
			Sleep(1000 + nSendProcs*100);
			RunProcessWithArg(argv[0], nSendProcs, "send");
			return 0;
		}
		else if(0 == _stricmp(pCmd, "listen")) 
		{
			T_UINT32 nTotalMsg = nSendProcs * nMsgSend;
			ListenMessageFromTopic(pTopic, nTotalMsg, bGlobal);
			return 0;
		}
		else if(0 == _stricmp(pCmd, "send"))
		{
			SendMessageToTopic(pTopic, nMsgSend, bGlobal);
			return 0;
		}
	}
	Usage();
	return -1;
}
