// domprobe.cpp
// Domino servertask for probing Domino servers (NRPC, databases, views, documents)
// Compile with Domino C-API SDK: cl /I"C:\HCL\Domino\include" ...
// Cross-platform: Windows and Linux

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif

#include <global.h>
#include <addin.h>
#include <names.h>
#include <ns.h>
#include <nsfdb.h>
#include <osmisc.h>
#include <osmem.h>
#include <oserr.h>
#include <osenv.h>
#include <stdnames.h>
#include <mq.h>
#include <miscerr.h>
#include <srverr.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define sizeofstring(x) (sizeof (x) - 1)

// Platform-specific socket headers and constants

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <signal.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define SOCKET_ERROR_VAL SOCKET_ERROR
    #define SOCKET_CLOSE(fd) closesocket(fd)
    #define strcasecmp _stricmp
    typedef int ssize_t;
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <signal.h>
    #define SOCKET_ERROR_VAL (-1)
    #define SOCKET_CLOSE(fd) close(fd)
    #define _strnicmp strncasecmp
#endif

// Task and version info
#define TASKNAME "DOMPROBE"
#define MsgQueueName TASK_QUEUE_PREFIX TASKNAME

#define VERSION "1.0.0"

#define CONSOLE_DISABLED 0
#define CONSOLE_LOOPBACK 1
#define CONSOLE_ALL      2

struct ClientConn
{
    int         hFd;
    sockaddr_in clientAddr;
};


static const char g_szTask[]     = "DomProbe";
static const char g_szTaskLong[] = "DomProbe";
static const char g_szVersion[]  = VERSION;

static WORD  g_wListenPort       = 9115;
static WORD  g_wNumWorkers       = 10;
static WORD  g_wQueueSize        = 1000;
static WORD  g_wDebug            = 0;
static WORD  g_wLogLevel         = 0;
static BOOL  g_bTestMode         = FALSE;
static WORD  g_wAllowConsole     = 0;

static char  g_szProbeAuthToken[256]   = {0};
static char  g_szConsoleAuthToken[256] = {0};

static volatile WORD g_wShutdownPending = 0;

static std::atomic<uint64_t> g_ProbeCount(0);
static std::atomic<uint64_t> g_ProbeDurationMsTotal(0);
static std::atomic<uint64_t> g_ProbeErrorCount(0);

static std::atomic<uint64_t> g_PingCount(0);
static std::atomic<uint64_t> g_PingDurationMsTotal(0);
static std::atomic<uint64_t> g_PingErrorCount(0);

static std::atomic<uint64_t> g_LatencyCount(0);
static std::atomic<uint64_t> g_LatencyDurationMsTotal(0);
static std::atomic<uint64_t> g_LatencyErrorCount(0);

static std::atomic<uint64_t> g_DbOpenCount(0);
static std::atomic<uint64_t> g_DbOpenDurationMsTotal(0);
static std::atomic<uint64_t> g_DbOpenErrorCount(0);

static std::atomic<uint64_t> g_ConsoleCount(0);
static std::atomic<uint64_t> g_ConsoleErrorCount(0);
static std::atomic<uint64_t> g_QueueRejectedCount(0);

static uint64_t    g_StartTimeSec  = 0;
static time_t      g_StartUnixTime = 0;
static std::string g_strServerList;

static const char g_szEmpty[]           = "";
static const char g_szPromTypeGauge[]   = "gauge";
static const char g_szPromTypeCounter[] = "counter";

static const char g_szProbPrefix[]      = "domino_probe";

#define SERVER_STATE_AVAILABLE     0
#define SERVER_STATE_RESTRICTED    1
#define SERVER_STATE_UNAVAILABLE   2
#define SERVER_STATE_NOT_REACHABLE 3


bool IsNullStr (const char *pszStr)
{
    if (NULL == pszStr)
        return true;

    if ('\0' == *pszStr)
        return true;

    return false;
}


#ifdef _WIN32

uint64_t GetTimeMs(void)
{
    return (uint64_t)GetTickCount64();
}

#else

uint64_t GetTimeMs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ((uint64_t)ts.tv_sec * 1000ULL) +
           ((uint64_t)ts.tv_nsec / 1000000ULL);
}

#endif


uint64_t GetTimeSec(void)
{
    return GetTimeMs() / 1000ULL;
}


bool WriteHelpAndType (std::ostringstream &out, const char *pszPrefix, const char *pszStatName, const char *pszType, const char *pszDescription)
{
    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    if (IsNullStr (pszType))
        pszType = g_szPromTypeGauge;

    if (NULL == pszDescription)
        pszDescription = g_szEmpty;

    out << "# HELP " << pszPrefix << "_" << pszStatName << " " << pszDescription << "\n";
    out << "# TYPE " << pszPrefix << "_" << pszStatName << " " << pszType        << "\n";

    return true;
}


bool WriteStatEntry (std::ostringstream &out, const char *pszPrefix, const char *pszStatName, const char *pszDescription, uint64_t ValueNum)
{
    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    WriteHelpAndType (out, pszPrefix, pszStatName, NULL, pszDescription);

    out << pszPrefix << "_" << pszStatName << " " << ValueNum << "\n";

    return true;
}


bool WriteStatEntry (std::ostringstream &out, const char *pszPrefix, const char *pszStatName, const char *pszDescription, const char *pszValueString)
{
    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    if (NULL == pszValueString)
        return false;

    WriteHelpAndType (out, pszPrefix, pszStatName, NULL, pszDescription);

    out << pszPrefix << "_" << pszStatName << " " << pszValueString << "\n";

    return true;
}


static void WriteErrorEntry (std::ostringstream &out, STATUS error)
{
    char szErrMsg[MAXSPRINTF+1] = {0};

    if (NOERROR == error)
        return;

    /* Strip off remote error flag */
    error = ERR(error);

    OSLoadString (NULLHANDLE, error, szErrMsg, sizeofstring (szErrMsg));

    WriteStatEntry (out, g_szProbPrefix, "error_code", szErrMsg, (uint64_t) error);
}


bool WriteStatEntryMSecToSeconds (std::ostringstream &out, const char *pszPrefix, const char *pszStatName, const char *pszDescription, DWORD dwValue)
{
    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    WriteHelpAndType (out, pszPrefix, pszStatName, NULL, pszDescription);

    DWORD sec  = dwValue / 1000;
    DWORD frac = dwValue % 1000;

    char szSeconds[32] = {0};
    snprintf (szSeconds, sizeof(szSeconds), "%u.%03u", sec, frac);

    out << pszPrefix << "_" << pszStatName << " " << szSeconds << "\n";

    return true;
}


static void CloseFd(int hFd)
{
    if (hFd >= 0)
    {
        SOCKET_CLOSE(hFd);
    }
}


static void PrintHelp()
{
    AddInLogMessageText("%s: DomProbe - Domino NRPC Blackbox Exporter", 0, g_szTask);
    AddInLogMessageText("Usage: tell task %s [options]", 0, TASKNAME);
    AddInLogMessageText("  help              - Show this help", 0);
    AddInLogMessageText("  config, status    - Show current configuration", 0);
    AddInLogMessageText("  quit              - Graceful shutdown", 0);
}


static void PrintConfig()
{
    AddInLogMessageText("%s: Configuration", 0, g_szTask);
    AddInLogMessageText("  Port: %u",        0, g_wListenPort);
    AddInLogMessageText("  Workers: %u",     0, g_wNumWorkers);
    AddInLogMessageText("  Queue Size: %u",  0, g_wQueueSize);
    AddInLogMessageText("  Debug Level: %u", 0, g_wDebug);
}


static void ProcessCommand(const char* pszCmdBuffer)
{
    if (!pszCmdBuffer || pszCmdBuffer[0] == '\0')
        return;

    if (strcasecmp(pszCmdBuffer, "help") == 0)
    {
        PrintHelp();
    }
    else if (strcasecmp(pszCmdBuffer, "config") == 0 || strcasecmp(pszCmdBuffer, "status") == 0)
    {
        PrintConfig();
    }
    else if (strcasecmp(pszCmdBuffer, "quit") == 0)
    {
        AddInLogMessageText("%s: Quit command received", 0, g_szTask);
        g_wShutdownPending = 1;
    }
    else
    {
        AddInLogMessageText("%s: Invalid command: %s", 0, g_szTask, pszCmdBuffer);
    }
}


static BOOL CheckAndProcessCommand(MQHANDLE hQueue)
{
    STATUS error = NOERROR;
    WORD wMsgLen = 0;
    char szMsgBuffer[MQ_MAX_MSGSIZE + 1] = {0};

    if (NULLHANDLE == hQueue)
        return FALSE;

    if (MQIsQuitPending(hQueue))
        return TRUE;

    error = MQGet(hQueue, szMsgBuffer, MQ_MAX_MSGSIZE, 0, 0, &wMsgLen);

    if (NOERROR == error)
    {
        if (wMsgLen < MQ_MAX_MSGSIZE)
        {
            szMsgBuffer[wMsgLen] = '\0';
            ProcessCommand(szMsgBuffer);
        }
    }

    return FALSE;
}


STATUS LoadConfig()
{
    long nValue = 0;

    nValue = OSGetEnvironmentLong("DOMPROBE_ListenPort");
    if (nValue > 0 && nValue < 65536)
    {
        g_wListenPort = static_cast<uint16_t>(nValue);
    }

    nValue = OSGetEnvironmentLong("DOMPROBE_Workers");
    if (nValue >= 1 && nValue <= 100)
    {
        g_wNumWorkers = static_cast<WORD>(nValue);
    }

    nValue = OSGetEnvironmentLong("DOMPROBE_QueueSize");
    if (nValue >= 100 && nValue <= 10000)
    {
        g_wQueueSize = static_cast<WORD>(nValue);
    }

    g_wDebug = static_cast<WORD>(OSGetEnvironmentLong("DOMPROBE_Debug"));
    g_bTestMode     = (1 == OSGetEnvironmentLong("KitType"));
    g_wAllowConsole = (WORD)OSGetEnvironmentLong("DOMPROBE_AllowConsole");
    if (g_wAllowConsole > CONSOLE_ALL)
        g_wAllowConsole = CONSOLE_ALL;

    OSGetEnvironmentString ("DOMPROBE_AuthToken",        g_szProbeAuthToken,   sizeofstring(g_szProbeAuthToken));
    OSGetEnvironmentString ("DOMPROBE_ConsoleAuthToken", g_szConsoleAuthToken, sizeofstring(g_szConsoleAuthToken));

    return NOERROR;
}


static bool IsLoopbackClient(const sockaddr_in &clientAddr)
{
    uint32_t nAddr = ntohl(clientAddr.sin_addr.s_addr);
    return ((nAddr & 0xFF000000) == 0x7F000000);
}


static std::string FormatClientIP(const sockaddr_in &clientAddr)
{
    char szIP[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &clientAddr.sin_addr, szIP, sizeof(szIP));
    return szIP;
}


class SocketQueue
{

public:
    explicit SocketQueue(size_t nMaxSize)
        : m_nMaxSize(nMaxSize)
    {
    }

    bool Push(const ClientConn &conn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_queue.size() >= m_nMaxSize)
        {
            return false;
        }

        m_queue.push_back(conn);
        m_cv.notify_one();

        return true;
    }

    bool Pop(ClientConn &conn)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        while (m_queue.empty() && !g_wShutdownPending)
        {
            m_cv.wait(lock);
        }

        if (m_queue.empty())
        {
            return false;
        }

        conn = m_queue.front();
        m_queue.pop_front();

        return true;
    }

    void WakeAll()
    {
        m_cv.notify_all();
    }

private:

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<ClientConn> m_queue;
    size_t m_nMaxSize;
};


static bool SocketWriteAll(int hFd, const std::string& strData)
{
    const char* pszData = strData.data();
    size_t nLeft = strData.size();

    while (nLeft > 0)
    {
        int nFlags = 0;
#ifndef _WIN32
        nFlags = MSG_NOSIGNAL;
#endif
        ssize_t nSent = send(hFd, pszData, static_cast<int>(nLeft), nFlags);

        if (nSent <= 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return false;
        }

        pszData += nSent;
        nLeft -= static_cast<size_t>(nSent);
    }

    return true;
}


static void SendResponse(int hFd, int nStatus, const std::string& strStatusText, const std::string& strBody)
{
    std::ostringstream out;

    out << "HTTP/1.1 " << nStatus << " " << strStatusText << "\r\n"
        << "Content-Type: text/plain; version=0.0.4\r\n"
        << "Content-Length: " << strBody.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << strBody;

    SocketWriteAll(hFd, out.str());
}


static bool IsValidTarget(const std::string& strTarget)
{
    if (strTarget.empty() || strTarget.size() > 128)
    {
        return false;
    }

    for (char ch : strTarget)
    {
        unsigned char uch = static_cast<unsigned char>(ch);

        if (!(std::isalnum(uch) || ch == '-' || ch == '.' || ch == '_'))
        {
            return false;
        }
    }

    return true;
}


static std::string GetQueryParam(const std::string& strPath, const std::string& strName)
{
    size_t nQueryPos = strPath.find('?');

    if (nQueryPos == std::string::npos)
    {
        return "";
    }

    std::string strQuery = strPath.substr(nQueryPos + 1);
    std::string strKey = strName + "=";

    size_t nPos = strQuery.find(strKey);

    if (nPos == std::string::npos)
    {
        return "";
    }

    nPos += strKey.size();

    size_t nEnd = strQuery.find('&', nPos);

    if (nEnd == std::string::npos)
    {
        return strQuery.substr(nPos);
    }

    return strQuery.substr(nPos, nEnd - nPos);
}


static std::string RunProbe(char *pszTargetServer, char *pszTargetDatabase)
{
    STATUS error = NOERROR;
    DBHANDLE hDB = NULLHANDLE;

    DWORD dwIndex            = 0;
    DWORD dwClientToServerMS = 0;
    DWORD dwServerToClientMS = 0;
    DWORD dwServerState      = SERVER_STATE_NOT_REACHABLE;
    char  szPath[MAXPATH]    = {0};

    std::ostringstream out;

    g_ProbeCount++;

    uint64_t dwStartMs = GetTimeMs();

    if (IsNullStr (pszTargetServer))
        goto Done;

    {
        uint64_t dwPingStartMs = GetTimeMs();
        error = NSPingServer(pszTargetServer, &dwIndex, NULL);
        g_PingDurationMsTotal.fetch_add(GetTimeMs() - dwPingStartMs);
        g_PingCount++;
    }

    if (NOERROR == error)
    {
        dwServerState = SERVER_STATE_AVAILABLE;
    }
    else
    {
        g_PingErrorCount++;

        switch (error)
        {
            case ERR_SERVER_RESTRICTED:
                dwServerState = SERVER_STATE_RESTRICTED;
                break;

            case ERR_SERVER_UNAVAILABLE:
                dwServerState = SERVER_STATE_UNAVAILABLE;
                break;

            default:
                dwServerState = SERVER_STATE_NOT_REACHABLE;
                break;
        }
    }

    WriteStatEntry (out, g_szProbPrefix, "success",            "Whether the NRPC probe succeeded",                                     (uint64_t)(NOERROR == error ? 1 : 0));
    WriteStatEntry (out, g_szProbPrefix, "server_state",       "Server state: 0=available 1=restricted 2=unavailable 3=not_reachable", (uint64_t) dwServerState);
    WriteStatEntry (out, g_szProbPrefix, "availability_index", "Server availability index returned by NSPingServer",                   (uint64_t) dwIndex);

    if (error)
    {
        WriteErrorEntry (out, error);
        goto Done;
    }

    {
        uint64_t dwLatencyStartMs = GetTimeMs();
        error = NSFGetServerLatency(pszTargetServer, 0, &dwClientToServerMS, &dwServerToClientMS, NULL);
        g_LatencyDurationMsTotal.fetch_add(GetTimeMs() - dwLatencyStartMs);
        g_LatencyCount++;
    }

    if (error)
    {
        g_LatencyErrorCount++;
        WriteErrorEntry (out, error);
        goto Done;
    }

    WriteStatEntryMSecToSeconds (out, g_szProbPrefix, "client_to_server_seconds", "Time to send request to server",       dwClientToServerMS);
    WriteStatEntryMSecToSeconds (out, g_szProbPrefix, "server_to_client_seconds", "Time for reply to return from server", dwServerToClientMS);

    if (IsNullStr (pszTargetDatabase))
        goto Done;

    snprintf (szPath, sizeof(szPath), "%s!!%s", pszTargetServer, pszTargetDatabase);

    {
        uint64_t dwDbStartMs = GetTimeMs();
        error = NSFDbOpen(szPath, &hDB);
        g_DbOpenDurationMsTotal.fetch_add(GetTimeMs() - dwDbStartMs);
        g_DbOpenCount++;

        WriteStatEntry              (out, g_szProbPrefix, "database_open",         "Whether the target database was opened successfully", (uint64_t)(NOERROR == error ? 1 : 0));
        WriteStatEntryMSecToSeconds (out, g_szProbPrefix, "database_open_seconds", "Time to open the target database",                   (DWORD)(GetTimeMs() - dwDbStartMs));

        if (error)
        {
            g_DbOpenErrorCount++;
            WriteErrorEntry (out, error);
        }
    }

Done:

    if (hDB)
    {
        NSFDbClose(hDB);
        hDB = NULLHANDLE;
    }

    DWORD dwDurationMs = (DWORD)(GetTimeMs() - dwStartMs);

    g_ProbeDurationMsTotal.fetch_add(dwDurationMs);

    if (SERVER_STATE_AVAILABLE != dwServerState)
        g_ProbeErrorCount++;

    WriteStatEntryMSecToSeconds (out, g_szProbPrefix, "duration_seconds", "Total probe duration", dwDurationMs);

    return out.str();
}


static bool PathMatches (const std::string &strPath, const char *pszRoute)
{
    size_t nLen = strlen (pszRoute);

    if (strPath.size() < nLen)
        return false;

    if (strPath.compare (0, nLen, pszRoute) != 0)
        return false;

    // Must be end of path or start of query string — nothing else
    if (strPath.size() == nLen)
        return true;

    return strPath[nLen] == '?';
}


static std::string GetHeaderValue (const char *pszRequest, const char *pszHeader)
{
    if (!pszRequest || !pszHeader)
        return "";

    const char *pszPos = pszRequest;

    while (*pszPos)
    {
        const char *pszLine = pszPos;

        // Skip past end of line
        while (*pszPos && *pszPos != '\r' && *pszPos != '\n')
            pszPos++;

        size_t nLineLen  = pszPos - pszLine;
        size_t nHdrLen   = strlen(pszHeader);

        if (nLineLen > nHdrLen + 1 &&
            _strnicmp(pszLine, pszHeader, nHdrLen) == 0 &&
            pszLine[nHdrLen] == ':')
        {
            const char *pszVal = pszLine + nHdrLen + 1;
            while (*pszVal == ' ') pszVal++;
            return std::string(pszVal, pszLine + nLineLen - pszVal);
        }

        while (*pszPos == '\r' || *pszPos == '\n')
            pszPos++;
    }

    return "";
}


static bool IsAuthenticated (const std::string &strPath, const char *pszRequest, const char *pszToken)
{
    if (IsNullStr(pszToken))
        return true;

    // URL parameter: ?auth=<token> (or &auth=<token>)
    std::string strUrlToken = GetQueryParam(strPath, "auth");
    if (strUrlToken == pszToken)
        return true;

    // HTTP header: Authorization: Bearer <token>
    std::string strAuthHeader = GetHeaderValue(pszRequest, "Authorization");
    if (strAuthHeader == std::string("Bearer ") + pszToken)
        return true;

    return false;
}


static const char *g_aszAllowedConsoleVerbs[] =
{
    "show",
    "tell",
    "drop",
    "load",
    "restart",
    "set",
    NULL
};


static bool IsValidConsoleCommand (const std::string &strCmd)
{
    if (strCmd.empty() || strCmd.size() > 256)
        return false;

    // Split into verb and parameters
    size_t nSpace = strCmd.find(' ');
    std::string strVerb   = (nSpace == std::string::npos) ? strCmd : strCmd.substr(0, nSpace);
    std::string strParams = (nSpace == std::string::npos) ? ""     : strCmd.substr(nSpace + 1);

    // Verb must be in the explicit whitelist (case-insensitive)
    bool bVerbAllowed = false;
    for (int i = 0; g_aszAllowedConsoleVerbs[i]; i++)
    {
        if (_strnicmp(strVerb.c_str(), g_aszAllowedConsoleVerbs[i], strVerb.size() + 1) == 0)
        {
            bVerbAllowed = true;
            break;
        }
    }

    if (!bVerbAllowed)
        return false;

    // Parameters: alphanumeric plus a limited set of safe characters
    for (char ch : strParams)
    {
        unsigned char uch = static_cast<unsigned char>(ch);

        if (!(std::isalnum(uch) ||
              ch == ' ' || ch == '_' || ch == '-' || ch == '.' ||
              ch == '=' || ch == '/' || ch == '*' || ch == '@' || ch == ','))
        {
            return false;
        }
    }

    return true;
}


static std::string UrlDecode (const std::string &strIn)
{
    std::string strOut;
    strOut.reserve (strIn.size());

    for (size_t i = 0; i < strIn.size(); i++)
    {
        if (strIn[i] == '+')
        {
            strOut += ' ';
        }
        else if (strIn[i] == '%' && i + 2 < strIn.size())
        {
            int nVal = 0;
            sscanf (strIn.c_str() + i + 1, "%02x", &nVal);
            strOut += static_cast<char>(nVal);
            i += 2;
        }
        else
        {
            strOut += strIn[i];
        }
    }

    return strOut;
}


static std::string RunServerList()
{
    STATUS  error       = NOERROR;
    DHANDLE hServerList = NULLHANDLE;
    BYTE   *pServerList = NULL;
    std::ostringstream out;

    error = NSGetServerList(NULL, &hServerList);

    if (error)
    {
        char szErrMsg[MAXSPRINTF+1] = {0};
        OSLoadString(NULLHANDLE, ERR(error), szErrMsg, sizeofstring(szErrMsg));
        out << "Error: " << szErrMsg << "\n";
        return out.str();
    }

    pServerList = (BYTE *)OSLockObject(hServerList);

    if (pServerList)
    {
        WORD  wServerCount = *(WORD *)pServerList;
        WORD *pwLengths    = (WORD *)(pServerList + sizeof(WORD));
        BYTE *pName        = pServerList + sizeof(WORD) + wServerCount * sizeof(WORD);
        char  szName[MAXPATH] = {0};

        for (WORD i = 0; i < wServerCount; i++)
        {
            WORD wLen = pwLengths[i];
            if (wLen >= (WORD)sizeof(szName))
                wLen = (WORD)sizeof(szName) - 1;

            memmove(szName, pName, wLen);
            szName[wLen] = '\0';

            out << szName << "\n";
            pName += pwLengths[i];
        }

        OSUnlockObject(hServerList);
    }

    OSMemFree(hServerList);

    return out.str();
}


static std::string RunConsole (const char *pszTarget, const char *pszCommand, const char *pszClientIP)
{
    STATUS  error    = NOERROR;
    DHANDLE hRetInfo = NULLHANDLE;
    char   *pBuffer  = NULL;
    std::string strResult;

    g_ConsoleCount++;

    AddInLogMessageText("%s: Console request: client=%s target=[%s] cmd=[%s]", 0, g_szTask, pszClientIP, pszTarget, pszCommand);

    error = NSFRemoteConsole (const_cast<char *>(pszTarget), const_cast<char *>(pszCommand), &hRetInfo);

    if (error)
    {
        char szErrMsg[MAXSPRINTF+1] = {0};
        g_ConsoleErrorCount++;
        OSLoadString (NULLHANDLE, ERR(error), szErrMsg, sizeofstring(szErrMsg));
        AddInLogMessageText("%s: Console error: client=%s target=[%s] cmd=[%s] error=%s", 0, g_szTask, pszClientIP, pszTarget, pszCommand, szErrMsg);
        return std::string (szErrMsg) + "\n";
    }

    if (hRetInfo)
    {
        pBuffer = OSLock (char, hRetInfo);

        if (pBuffer)
            strResult = pBuffer;

        OSUnlock (hRetInfo);
        OSMemFree (hRetInfo);
    }

    return strResult;
}


static void HandleClient(int hFd, const sockaddr_in &clientAddr)
{
#ifdef _WIN32
    DWORD dwTimeoutMs = 5000;
    setsockopt(hFd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&dwTimeoutMs), sizeof(dwTimeoutMs));
    setsockopt(hFd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&dwTimeoutMs), sizeof(dwTimeoutMs));
#else
    timeval tvTimeout;
    tvTimeout.tv_sec = 5;
    tvTimeout.tv_usec = 0;
    setsockopt(hFd, SOL_SOCKET, SO_RCVTIMEO, &tvTimeout, sizeof(tvTimeout));
    setsockopt(hFd, SOL_SOCKET, SO_SNDTIMEO, &tvTimeout, sizeof(tvTimeout));
#endif

    char szBuffer[4096];

    ssize_t nReceived = recv(hFd, szBuffer, sizeofstring(szBuffer), 0);

    if (nReceived <= 0)
    {
        CloseFd(hFd);
        return;
    }

    szBuffer[nReceived] = '\0';

    std::istringstream req(szBuffer);

    std::string strMethod;
    std::string strPath;
    std::string strVersion;

    req >> strMethod >> strPath >> strVersion;

    if (strMethod != "GET")
    {
        SendResponse(hFd, 405, "Method Not Allowed", "Only GET is supported\n");
        CloseFd(hFd);
        return;
    }

    if (strPath == "/health")
    {
        SendResponse(hFd, 200, "OK", "OK\n");
        CloseFd(hFd);
        return;
    }

    if (strPath == "/metrics")
    {
        std::ostringstream out;
        WriteHelpAndType (out, g_szProbPrefix, "probes_total", g_szPromTypeCounter, "Total number of probes executed since startup");
        out << g_szProbPrefix << "_probes_total " << g_ProbeCount.load() << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "probe_errors_total", g_szPromTypeCounter, "Total number of probes that did not return server_state available");
        out << g_szProbPrefix << "_probe_errors_total " << g_ProbeErrorCount.load() << "\n";

        DWORD dwSec  = (DWORD)(g_ProbeDurationMsTotal.load() / 1000);
        DWORD dwFrac = (DWORD)(g_ProbeDurationMsTotal.load() % 1000);
        char szDurationTotal[32] = {0};
        snprintf (szDurationTotal, sizeof(szDurationTotal), "%u.%03u", dwSec, dwFrac);

        WriteHelpAndType (out, g_szProbPrefix, "probe_duration_seconds_total", g_szPromTypeCounter, "Total accumulated probe duration in seconds since startup");
        out << g_szProbPrefix << "_probe_duration_seconds_total " << szDurationTotal << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "ping_requests_total", g_szPromTypeCounter, "Total NSPingServer calls");
        out << g_szProbPrefix << "_ping_requests_total " << g_PingCount.load() << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "ping_errors_total", g_szPromTypeCounter, "Total NSPingServer calls that returned an error");
        out << g_szProbPrefix << "_ping_errors_total " << g_PingErrorCount.load() << "\n";

        {
            DWORD dwSec  = (DWORD)(g_PingDurationMsTotal.load() / 1000);
            DWORD dwFrac = (DWORD)(g_PingDurationMsTotal.load() % 1000);
            char szVal[32] = {0};
            snprintf(szVal, sizeof(szVal), "%u.%03u", dwSec, dwFrac);
            WriteHelpAndType (out, g_szProbPrefix, "ping_duration_seconds_total", g_szPromTypeCounter, "Total accumulated NSPingServer call duration");
            out << g_szProbPrefix << "_ping_duration_seconds_total " << szVal << "\n";
        }

        WriteHelpAndType (out, g_szProbPrefix, "latency_requests_total", g_szPromTypeCounter, "Total NSFGetServerLatency calls");
        out << g_szProbPrefix << "_latency_requests_total " << g_LatencyCount.load() << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "latency_errors_total", g_szPromTypeCounter, "Total NSFGetServerLatency calls that returned an error");
        out << g_szProbPrefix << "_latency_errors_total " << g_LatencyErrorCount.load() << "\n";

        {
            DWORD dwSec  = (DWORD)(g_LatencyDurationMsTotal.load() / 1000);
            DWORD dwFrac = (DWORD)(g_LatencyDurationMsTotal.load() % 1000);
            char szVal[32] = {0};
            snprintf(szVal, sizeof(szVal), "%u.%03u", dwSec, dwFrac);
            WriteHelpAndType (out, g_szProbPrefix, "latency_duration_seconds_total", g_szPromTypeCounter, "Total accumulated NSFGetServerLatency call duration");
            out << g_szProbPrefix << "_latency_duration_seconds_total " << szVal << "\n";
        }

        WriteHelpAndType (out, g_szProbPrefix, "db_open_requests_total", g_szPromTypeCounter, "Total NSFDbOpen calls");
        out << g_szProbPrefix << "_db_open_requests_total " << g_DbOpenCount.load() << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "db_open_errors_total", g_szPromTypeCounter, "Total NSFDbOpen calls that returned an error");
        out << g_szProbPrefix << "_db_open_errors_total " << g_DbOpenErrorCount.load() << "\n";

        {
            DWORD dwSec  = (DWORD)(g_DbOpenDurationMsTotal.load() / 1000);
            DWORD dwFrac = (DWORD)(g_DbOpenDurationMsTotal.load() % 1000);
            char szVal[32] = {0};
            snprintf(szVal, sizeof(szVal), "%u.%03u", dwSec, dwFrac);
            WriteHelpAndType (out, g_szProbPrefix, "db_open_duration_seconds_total", g_szPromTypeCounter, "Total accumulated NSFDbOpen call duration");
            out << g_szProbPrefix << "_db_open_duration_seconds_total " << szVal << "\n";
        }

        WriteHelpAndType (out, g_szProbPrefix, "console_requests_total", g_szPromTypeCounter, "Total number of console requests executed since startup");
        out << g_szProbPrefix << "_console_requests_total " << g_ConsoleCount.load()      << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "console_errors_total", g_szPromTypeCounter, "Total number of console requests that returned an error");
        out << g_szProbPrefix << "_console_errors_total " << g_ConsoleErrorCount.load() << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "queue_rejected_total", g_szPromTypeCounter, "Total number of requests rejected because the worker queue was full");
        out << g_szProbPrefix << "_queue_rejected_total " << g_QueueRejectedCount.load() << "\n";

        WriteHelpAndType (out, g_szProbPrefix, "start_timestamp_seconds", g_szPromTypeGauge, "Unix timestamp when DomProbe started");
        out << g_szProbPrefix << "_start_timestamp_seconds " << (uint64_t)g_StartUnixTime << "\n";

        uint64_t nUptime = (g_StartTimeSec > 0) ? (GetTimeSec() - g_StartTimeSec) : 0;
        WriteHelpAndType (out, g_szProbPrefix, "uptime_seconds", g_szPromTypeGauge, "Seconds DomProbe has been running");
        out << g_szProbPrefix << "_uptime_seconds " << nUptime << "\n";

        SendResponse(hFd, 200, "OK", out.str());
        CloseFd(hFd);
        return;
    }

    if (PathMatches (strPath, "/console"))
    {
        std::string strClientIP = FormatClientIP(clientAddr);

        if (!g_wAllowConsole)
        {
            AddInLogMessageText("%s: Console rejected: console not enabled, client=%s", 0, g_szTask, strClientIP.c_str());
            SendResponse(hFd, 403, "Forbidden", "Console access not enabled (set DOMPROBE_AllowConsole=1 or 2)\n");
            CloseFd(hFd);
            return;
        }

        if (g_wAllowConsole == CONSOLE_LOOPBACK && !IsLoopbackClient(clientAddr))
        {
            AddInLogMessageText("%s: Console rejected: non-loopback client=%s", 0, g_szTask, strClientIP.c_str());
            SendResponse(hFd, 403, "Forbidden", "Console available from loopback only\n");
            CloseFd(hFd);
            return;
        }

        if (IsNullStr(g_szConsoleAuthToken))
        {
            AddInLogMessageText("%s: Console rejected: no auth token configured, client=%s", 0, g_szTask, strClientIP.c_str());
            SendResponse(hFd, 403, "Forbidden", "Console requires DOMPROBE_ConsoleAuthToken to be set\n");
            CloseFd(hFd);
            return;
        }

        if (!IsAuthenticated(strPath, szBuffer, g_szConsoleAuthToken))
        {
            AddInLogMessageText("%s: Console rejected: auth failed, client=%s", 0, g_szTask, strClientIP.c_str());
            SendResponse(hFd, 401, "Unauthorized", "Invalid or missing auth token\n");
            CloseFd(hFd);
            return;
        }

        std::string strTarget  = GetQueryParam(strPath, "target");
        std::string strCommand = UrlDecode(GetQueryParam(strPath, "cmd"));

        if (!IsValidTarget(strTarget) || !IsValidConsoleCommand(strCommand))
        {
            AddInLogMessageText("%s: Console rejected: invalid target or command, client=%s", 0, g_szTask, strClientIP.c_str());
            SendResponse(hFd, 400, "Bad Request", "Invalid or missing target or cmd\n");
            CloseFd(hFd);
            return;
        }

        std::string strBody = RunConsole(strTarget.c_str(), strCommand.c_str(), strClientIP.c_str());
        SendResponse(hFd, 200, "OK", strBody);
        CloseFd(hFd);
        return;
    }

    if (strPath == "/quit")
    {
        if (!g_bTestMode)
        {
            SendResponse(hFd, 403, "Forbidden", "Not available on server\n");
            CloseFd(hFd);
            return;
        }

        SendResponse(hFd, 200, "OK", "Shutting down\n");
        CloseFd(hFd);
        g_wShutdownPending = 1;
        return;
    }

    if (PathMatches (strPath, "/servers"))
    {
        if (!IsAuthenticated(strPath, szBuffer, g_szProbeAuthToken))
        {
            SendResponse(hFd, 401, "Unauthorized", "Invalid or missing auth token\n");
            CloseFd(hFd);
            return;
        }

        SendResponse(hFd, 200, "OK", g_strServerList);
        CloseFd(hFd);
        return;
    }

    if (!PathMatches (strPath, "/probe"))
    {
        SendResponse(hFd, 404, "Not Found", "Not found\n");
        CloseFd(hFd);
        return;
    }

    if (!IsAuthenticated(strPath, szBuffer, g_szProbeAuthToken))
    {
        SendResponse(hFd, 401, "Unauthorized", "Invalid or missing auth token\n");
        CloseFd(hFd);
        return;
    }

    std::string strTarget = GetQueryParam(strPath, "target");

    if (!IsValidTarget(strTarget))
    {
        SendResponse(hFd, 400, "Bad Request", "Invalid or missing target\n");
        CloseFd(hFd);
        return;
    }

    // Optional database parameter (e.g., db=names.nsf)
    std::string strDatabase = GetQueryParam(strPath, "db");
    std::string strBody = RunProbe((char *) strTarget.c_str(), (char *) strDatabase.c_str());
    SendResponse(hFd, 200, "OK", strBody);

    CloseFd(hFd);
}


static void WorkerLoop(SocketQueue& queue)
{
    NotesInitThread();

    while (!g_wShutdownPending)
    {
        ClientConn conn = {-1, 0};

        if (!queue.Pop(conn))
        {
            continue;
        }

        if (g_wDebug)
            AddInLogMessageText("%s: DEBUG: Worker processing client fd=%d", 0, g_szTask, conn.hFd);

        HandleClient(conn.hFd, conn.clientAddr);
    }

    if (g_wDebug)
        AddInLogMessageText("%s: DEBUG: Worker thread terminating, calling NotesTermThread", 0, g_szTask);

    NotesTermThread();
}


static int CreateListener(uint16_t wPort)
{
    int hFd = socket(AF_INET, SOCK_STREAM, 0);

    if (hFd == SOCKET_ERROR_VAL)
    {
        return -1;
    }

    int bReuseAddr = 1;
    setsockopt(hFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&bReuseAddr), sizeof(bReuseAddr));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(wPort);

    if (bind(hFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        CloseFd(hFd);
        return -1;
    }

    if (listen(hFd, 128) < 0)
    {
        CloseFd(hFd);
        return -1;
    }

    return hFd;
}


static void ListenerThread(SocketQueue& queue, int hListenFd)
{
    if (g_wDebug)
        AddInLogMessageText("%s: DEBUG: Listener thread started, fd=%d", 0, g_szTask, hListenFd);

    while (!g_wShutdownPending)
    {
        // Use select() with 100ms timeout to allow checking g_wShutdownPending
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(hListenFd, &readfds);

        timeval tvTimeout;
        tvTimeout.tv_sec = 0;
        tvTimeout.tv_usec = 100000;  // 100ms

        int nSelectResult = select(hListenFd + 1, &readfds, nullptr, nullptr, &tvTimeout);

        if (nSelectResult < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (!g_wShutdownPending)
            {
                AddInLogMessageText("%s: ERROR: select failed", 0, g_szTask);
            }

            continue;
        }

        // Timeout occurred, loop again to check g_wShutdownPending
        if (nSelectResult == 0)
        {
            continue;
        }

        sockaddr_in clientAddr;
        socklen_t nClientLen = sizeof(clientAddr);

        int hClientFd = accept(hListenFd, reinterpret_cast<sockaddr*>(&clientAddr), &nClientLen);

        if (hClientFd == SOCKET_ERROR_VAL)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (!g_wShutdownPending)
            {
                AddInLogMessageText("%s: ERROR: accept failed", 0, g_szTask);
            }

            continue;
        }

        if (g_wDebug)
            AddInLogMessageText("%s: DEBUG: Accepted client connection, fd=%d", 0, g_szTask, hClientFd);

        ClientConn conn = {hClientFd, clientAddr};

        if (!queue.Push(conn))
        {
            g_QueueRejectedCount++;
            SendResponse(hClientFd, 503, "Service Unavailable", "Queue full\n");
            CloseFd(hClientFd);
        }
    }
}


static BOOL StartThreadsAndQueue(int hListenFd, SocketQueue*& pQueue, std::vector<std::thread>*& pWorkerThreads, std::thread*& pListenerThreadObj)
{
    pQueue = new SocketQueue(g_wQueueSize);

    if (g_wDebug)
        AddInLogMessageText("%s: DEBUG: Spawning %u worker threads", 0, g_szTask, g_wNumWorkers);

    pWorkerThreads = new std::vector<std::thread>();
    for (size_t nWorker = 0; nWorker < g_wNumWorkers; ++nWorker)
    {
        pWorkerThreads->emplace_back(WorkerLoop, std::ref(*pQueue));
    }

    // Spawn listener thread
    pListenerThreadObj = new std::thread(ListenerThread, std::ref(*pQueue), hListenFd);
    return TRUE;
}


STATUS LNPUBLIC AddInMain (HMODULE hResourceModule, int argc, char *argv[])
{
    STATUS error = NOERROR;
    DHANDLE hOldStatusLine  = NULLHANDLE;
    DHANDLE hStatusLineDesc = NULLHANDLE;
    HMODULE hMod            = NULLHANDLE;
    MQHANDLE hQueue         = NULLHANDLE;
    int hListenFd = -1;
    int a = 0;
    char ch = '\0';
    BOOL bQueueCreated    = FALSE;
    BOOL bListenerStarted = FALSE;
    SocketQueue* pQueue = NULL;

    std::vector<std::thread>* pWorkerThreads = NULL;
    std::thread* pListenerThreadObj = NULL;

    AddInLogMessageText("%s: AddInMain starting (v%s)", 0, g_szTask, g_szVersion);

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        AddInLogMessageText("%s: ERROR: WSAStartup failed", 0, g_szTask);
        return NOERROR;
    }
#endif

    // Manage status line
    AddInQueryDefaults(&hMod, &hOldStatusLine);
    AddInDeleteStatusLine(hOldStatusLine);

    hStatusLineDesc = AddInCreateStatusLine(const_cast<char*>(g_szTaskLong));

    AddInSetDefaults(hMod, hStatusLineDesc);
    LoadConfig();

    g_StartTimeSec  = GetTimeSec();
    g_StartUnixTime = time(NULL);
    g_strServerList = RunServerList();

    if (g_wDebug)
        AddInLogMessageText("%s: DEBUG: Config loaded - port=%u, workers=%u, queue=%u", 0, g_szTask, g_wListenPort, g_wNumWorkers, g_wQueueSize);

    // Parse command-line arguments (to override config)
    for (a = 1; a < argc; a++)
    {
        if (strcasecmp(argv[a], "--version") == 0 || strcasecmp(argv[a], "-version") == 0)
        {
            AddInLogMessageText("%s: %s", 0, g_szTask, g_szVersion);
            goto Done;
        }
        else if (argv[a][0] == '-')
        {
            ch = argv[a][1];

            switch (ch)
            {
                case 'v':
                    g_wLogLevel++;
                    break;
                case '?':
                case 'h':
                    PrintHelp();
                    goto Done;
                default:
                    AddInLogMessageText("%s: Error invalid option '%s'", NOERROR, g_szTask, argv[a] + 1);
                    goto Done;
            }
        }
        else if (argv[a][0] == '=')
        {
            // handled by core
        }
    }

    // Create message queue for "tell task" commands
    error = MQCreate(MsgQueueName, 0, 0);
    if (error)
    {
        error = NOERROR;
        AddInLogMessageText("%s: Servertask already started", 0, g_szTask);
        goto Done;
    }

    error = MQOpen(MsgQueueName, 0, &hQueue);
    if (error)
    {
        AddInLogMessageText("%s: Cannot open message queue", error, g_szTask);
        goto Done;
    }

    hListenFd = CreateListener(g_wListenPort);
    if (hListenFd < 0)
    {
        AddInLogMessageText("%s: ERROR: cannot listen on port %u", NOERROR, g_szTask, g_wListenPort);
        goto Done;
    }

    AddInLogMessageText("%s: v%s started on port %u, workers=%u, queue=%u",
                        0, g_szTask, g_szVersion, g_wListenPort, g_wNumWorkers, g_wQueueSize);

    if (g_wAllowConsole == CONSOLE_LOOPBACK)
        AddInLogMessageText("%s: Console endpoint enabled (loopback only)", 0, g_szTask);

    else if (g_wAllowConsole == CONSOLE_ALL)
        AddInLogMessageText("%s: Console endpoint enabled (all addresses)", 0, g_szTask);

    if (!StartThreadsAndQueue(hListenFd, pQueue, pWorkerThreads, pListenerThreadObj))
    {
        AddInLogMessageText("%s: ERROR: Failed to start threads and queue", 0, g_szTask);
        goto Done;
    }

    bQueueCreated    = TRUE;
    bListenerStarted = TRUE;

    if (g_wDebug)
        AddInLogMessageText("%s: DEBUG: Threads and queue started, entering main loop", 0, g_szTask);

    while (0 == g_wShutdownPending)
    {
        if (CheckAndProcessCommand(hQueue))
        {
            g_wShutdownPending = 1;
            break;
        }

        if (AddInIdleDelay(100))
        {
            g_wShutdownPending = 1;
            break;
        }
    }

Done:

    AddInLogMessageText("%s: Shutting down", 0, g_szTask);

    // Close listening socket
    if (hListenFd >= 0)
    {
        CloseFd(hListenFd);
    }

    // Wake all workers and listener if queue was created
    if (bQueueCreated && pQueue)
    {
        pQueue->WakeAll();

        // Join listener thread
        if (bListenerStarted && pListenerThreadObj && pListenerThreadObj->joinable())
        {
            pListenerThreadObj->join();
        }

        // Join worker threads
        if (pWorkerThreads)
        {
            for (auto& workerThread : *pWorkerThreads)
            {
                if (workerThread.joinable())
                {
                    workerThread.join();
                }
            }
        }
    }

    // Clean up allocated objects
    delete pListenerThreadObj;
    delete pWorkerThreads;
    delete pQueue;

    // Close message queue
    if (hQueue != NULLHANDLE)
    {
        MQClose(hQueue, 0);
        hQueue = NULLHANDLE;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    AddInLogMessageText("%s: Terminated", 0, g_szTask);

    return error;
}

