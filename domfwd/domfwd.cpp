/*
###########################################################################
# Domino Log Forwarder                                                    #
# Version 0.9.0 09.02.2026                                                #
# (C) Copyright Daniel Nashed/Nash!Com 2026                               #
#                                                                         #
# Licensed under the Apache License, Version 2.0 (the "License");         #
# you may not use this file except in compliance with the License.        #
# You may obtain a copy of the License at                                 #
#                                                                         #
#      http://www.apache.org/licenses/LICENSE-2.0                         #
#                                                                         #
# Unless required by applicable law or agreed to in writing, software     #
# distributed under the License is distributed on an "AS IS" BASIS,       #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.#
# See the License for the specific language governing permissions and     #
# limitations under the License.                                          #
#                                                                         #
#                                                                         #
###########################################################################
*/

#define DOMFWD_COPYRIGHT  "Copyright Daniel Nashed/Nash!Com 2026"
#define DOMFWD_GITHUB_URL "https://github.com/nashcom/domino-grafana"

#define DOMFWD_VERSION "0.9.0"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>

#include <curl/curl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* Override assertions to ensure we don't terminate for logical errors */
#define RAPIDJSON_ASSERT

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "simple_wal.hpp"


using PidMap = std::unordered_map<pid_t, std::string>;

class string_fifo
{

public:

    void push (const char *pszValue)
    {
        if (NULL == pszValue)
            return;

        {
            std::lock_guard<std::mutex> lock (m_mutex);
            m_queue.push (std::move (pszValue));
        }
        m_cond.notify_one();
    }

    bool pop (std::string &out)
    {
        std::unique_lock<std::mutex> lock (m_mutex);
        m_cond.wait (lock, [&]
        {
            return !m_queue.empty() || m_bShutdown;
        });

        if (m_queue.empty())
        {
            return false; // shutdown
        }

        out = std::move (m_queue.front());
        m_queue.pop();
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock (m_mutex);
            m_bShutdown = true;
        }
        m_cond.notify_all();
    }

private:

    std::queue<std::string> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_bShutdown = false;
};


/* Globals */

char g_szHostname[1024]       = {0};
char g_szJob[1024]            = {0};
char g_szLogFilename[2048]    = {0};
char g_szPidNbfFile[2048]     = {0};
char g_szLokiPushApiURL[1024] = {0};
char g_szLokiPushToken[1024]  = {0};
char g_szInputLogFile[2048]   = {0};
char g_szOutputLogFile[2048]  = {0};

char g_szNotesDataDir[1024]   = "/local/notesdata";
char g_szNamespace[1024]      = "domino";
char g_szPod[1024]            = "newton.nashcom.de";

char g_szVersion[]            = DOMFWD_VERSION;
char g_szCopyright[]          = DOMFWD_COPYRIGHT;
char g_szGitHubURL[]          = DOMFWD_GITHUB_URL;
char g_szTask[]               = "domfwd";

size_t g_ShutdownRequested = 0;
size_t g_ReloadRequested   = 0;
size_t g_PushThreadRunning = 0;
size_t g_WalThreadRunning  = 0;

pthread_t    g_WalThreadInstance = {0};
pthread_t    g_PushThreadInstance = {0};
struct stat  g_PidNbfStat {};
string_fifo  g_LogFifo;
int          g_fdOutputLogFile = -1;
int          g_fdOut = fileno(stdout);

SimpleWAL g_Wal;


bool IsNullStr (const char *pszStr)
{
    if (NULL == pszStr)
        return true;

    if ('\0' == *pszStr)
        return true;

    return false;
}


void LogMessage (const char *pszMessage)
{
    if (IsNullStr (pszMessage))
        return;

    fprintf (stderr, "%s: %s\n", g_szTask, pszMessage);
}


void LogError (const char *pszMessage)
{
    if (IsNullStr (pszMessage))
        return;

    fprintf (stderr, "%s: Error - %s\n", g_szTask, pszMessage);
}


int IsDirectoryWritable (const char *pszPath)
{
    struct stat st;

    if (IsNullStr (pszPath))
        return 0;

    if (stat (pszPath, &st) != 0)
        return 0;

    if (!S_ISDIR(st.st_mode))
        return 0;

    if (access(pszPath, W_OK))
        return 0;

    return 1;
}


void sleep_ms (unsigned int ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    nanosleep (&ts, NULL);
}


bool ParsePid (const std::string& text, pid_t& pid)
{
    errno = 0;
    char* pEnd = nullptr;

    long value = std::strtol(text.c_str(), &pEnd, 10);

    if (errno != 0 || pEnd == text.c_str() || *pEnd != '\0')
        return false;

    if (value < 0)
        return false;

    pid = static_cast<pid_t>(value);
    return true;
}


bool HasFileChanged (const char *pszFile, struct stat *pFileStat)
{
    struct stat CurrentStat {};
    bool bChanged = false;

    if (IsNullStr(pszFile) || pFileStat == NULL)
        return false;

    if (stat(pszFile, &CurrentStat) != 0)
        return false;

    // First observation
    if (pFileStat->st_ino == 0)
    {
        bChanged = true;
    }
    else
    {
        // Inode change → rotation / replacement
        if (CurrentStat.st_ino != pFileStat->st_ino)
            bChanged = true;

        // Size change → append / truncate
        else if (CurrentStat.st_size != pFileStat->st_size)
            bChanged = true;

        // Content modification time bChanged
        else if (CurrentStat.st_mtim.tv_sec  != pFileStat->st_mtim.tv_sec ||
                 CurrentStat.st_mtim.tv_nsec != pFileStat->st_mtim.tv_nsec)
            bChanged = true;

        // Metadata change (rename, truncate, etc.)
        else if (CurrentStat.st_ctim.tv_sec  != pFileStat->st_ctim.tv_sec ||
                 CurrentStat.st_ctim.tv_nsec != pFileStat->st_ctim.tv_nsec)
            bChanged = true;
    }

    *pFileStat = CurrentStat;
    return bChanged;
}


bool LoadPidMap (const char * pszPidFile, PidMap& pidMap)
{
    std::ifstream file (pszPidFile);
    pid_t pid = 0;

    if (IsNullStr (pszPidFile))
        return false;

    if (false == HasFileChanged (pszPidFile, &g_PidNbfStat))
        return false;

    if (!file.is_open())
    {
        std::cerr << "failed to open pid file: " << pszPidFile << "\n";
        return false;
    }

    std::string line;
    while (std::getline (file, line))
    {
        if (line.empty())
            continue;

        std::istringstream iss (line);

        std::string running;
        std::string pidText;
        std::string ppid;
        std::string processName;

        /* We only need the first four fields */
        if (!(iss >> running >> pidText >> ppid >> processName))
            continue;

        if (!ParsePid (pidText, pid))
            continue;

        pidMap[pid] = processName;
    }

    return true;
}


pid_t ExtractProcessId (const std::string& line)
{
    if (line.empty() || line[0] != '[')
        return 0;

    constexpr std::size_t kMaxPrefixLen = 40;
    const std::size_t searchEnd = std::min (line.size(), kMaxPrefixLen);

    pid_t pid = 0;

    /* Start after '[' */
    for (std::size_t i = 1; i < searchEnd; ++i)
    {
        const char c = line[i];

        if (c == ':')
        {
            /* Success only if we saw at least one digit */
            return pid;
        }

        if (c < '0' || c > '9')
        {
            return 0;
        }

        pid = pid * 10 + (c - '0');
    }

    /* No ':' encountered */
    return 0;
}


bool GetProcessNameFromMap (PidMap& pidMap, pid_t pid, std::string& processName)
{
    const auto it = pidMap.find (pid);

    if (it == pidMap.end())
    {
        processName = "";
        return false;
    }

    processName = it->second;
    return true;
}


bool GetProcessName (PidMap& pidMap, pid_t pid, std::string& processName)
{
    if (GetProcessNameFromMap (pidMap, pid, processName))
        return true;

    /* Try to reload */

    if (false == LoadPidMap (g_szPidNbfFile, pidMap))
        return false;

    if (GetProcessNameFromMap (pidMap, pid, processName))
        return true;

    return false;
}


static std::string GetEpochNanoseconds()
{
    struct timespec ts {};
    clock_gettime (CLOCK_REALTIME, &ts);

    int64_t ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return std::to_string(ns);
}


std::string BuildLokiPayload (const std::string& line, const pid_t pid, const char* pszProcess)
{
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    /* ---- stream labels (JSON object) ---- */

    rapidjson::Value streamLabels (rapidjson::kObjectType);

    streamLabels.AddMember ("job",       rapidjson::Value(g_szJob, alloc), alloc);
    streamLabels.AddMember ("host",      rapidjson::Value(g_szHostname, alloc), alloc);
    streamLabels.AddMember ("instance",  rapidjson::Value(g_szHostname, alloc), alloc);
    streamLabels.AddMember ("namespace", rapidjson::Value(g_szNamespace, alloc), alloc);
    streamLabels.AddMember ("pod",       rapidjson::Value(g_szPod, alloc), alloc);
    streamLabels.AddMember ("pid",       rapidjson::Value(std::to_string(pid).c_str(), alloc), alloc);

    if (!IsNullStr(pszProcess))
    {
        streamLabels.AddMember("process", rapidjson::Value (pszProcess, alloc), alloc);
    }

    /* ---- values ---- */
    rapidjson::Value values (rapidjson::kArrayType);

    rapidjson::Value valueEntry (rapidjson::kArrayType);

    std::string ts = GetEpochNanoseconds();

    valueEntry.PushBack (rapidjson::Value (ts.c_str(), alloc), alloc);
    valueEntry.PushBack (rapidjson::Value (line.c_str(), alloc), alloc);
    values.PushBack (valueEntry, alloc);

    /* ---- stream ---- */
    rapidjson::Value stream (rapidjson::kObjectType);
    stream.AddMember ("stream", streamLabels, alloc);
    stream.AddMember ("values", values, alloc);

    /* ---- streams ---- */
    rapidjson::Value streams (rapidjson::kArrayType);
    streams.PushBack (stream, alloc);

    doc.AddMember ("streams", streams, alloc);

    /* ---- serialize ---- */
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept (writer);

    return buffer.GetString();
}


bool GetHostname (size_t MaxSize, char * retpszHostname)
{
    if (0 != gethostname (retpszHostname, MaxSize-1))
        *retpszHostname = '\0';
    else
        retpszHostname[MaxSize - 1] = '\0';

    return true;
}


bool SendPayloadToWAL (const std::string& payload)
{
    return g_Wal.Append (payload.c_str(), payload.size());
}


bool SendAlloyPayload (CURL* pCurl, const char *pszURL, const char *pg_szLokiPushToken, const char* pszBuffer, size_t BufferLen)
{
    char szErrorBuffer[CURL_ERROR_SIZE+10] = {0};
    CURLcode rc = CURLE_OK;
    struct curl_slist* pHeaders = nullptr;

    if (IsNullStr (pszBuffer))
        return false;

    if (nullptr == pCurl)
    {
        LogError ("No curl handle specified");
        goto Done;
    }

    if (IsNullStr (pszURL))
    {
        LogError ("No Push URL specified");
        goto Done;
    }

    curl_easy_reset (pCurl);

    pHeaders = curl_slist_append (pHeaders, "Content-Type: application/json");
    curl_easy_setopt (pCurl, CURLOPT_HTTPHEADER, pHeaders);
    curl_easy_setopt (pCurl, CURLOPT_POST, 1L);
    curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt (pCurl, CURLOPT_FAILONERROR, 1L);

    curl_easy_setopt (pCurl, CURLOPT_ERRORBUFFER, szErrorBuffer);
    curl_easy_setopt (pCurl, CURLOPT_URL, pszURL);
    curl_easy_setopt (pCurl, CURLOPT_POSTFIELDS, pszBuffer);
    curl_easy_setopt (pCurl, CURLOPT_POSTFIELDSIZE, BufferLen);

    if (false == IsNullStr (pg_szLokiPushToken))
    {
        curl_easy_setopt (pCurl, CURLOPT_XOAUTH2_BEARER, pg_szLokiPushToken);
        curl_easy_setopt (pCurl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    }

    rc = curl_easy_perform (pCurl);

    if (CURLE_OK != rc)
    {
        LogError ("Curl operation failed");
        std::cerr << "curl error: " << curl_easy_strerror (rc) << ": " << szErrorBuffer << "\n";
        goto Done;
    }

Done:

    if (pHeaders)
    {
        curl_slist_free_all (pHeaders);
        pHeaders = nullptr;
    }

    return true;
}


void *PushThread (void *arg)
{
    (void)arg;
    CURL* pCurl = nullptr;
    std::string line;
    PidMap pidMap;
    pid_t  pid = 0;

    g_PushThreadRunning = 1;

    if (*g_szLokiPushApiURL)
    {
        pCurl = curl_easy_init();
        if (!pCurl)
        {
            std::cerr << "curl_easy_init failed\n";
            goto Done;
        }
    }

    while (0 == g_ShutdownRequested)
    {
        while (g_LogFifo.pop (line))
        {
            std::string processName;

            pid = ExtractProcessId (line);

            GetProcessName (pidMap, pid, processName);
            const std::string payload = BuildLokiPayload (line, pid, processName.c_str());

            /* WAL-TESTING can be used to test WAL logic */
            if (payload.find ("WAL-TESTING") != std::string::npos)
            {
                SendPayloadToWAL (payload);
            }
            else
            {
                if (*g_szLokiPushApiURL)
                {
                    if (false == SendAlloyPayload (pCurl, g_szLokiPushApiURL, g_szLokiPushToken, payload.c_str(), payload.size()))
                    {
                        SendPayloadToWAL (payload);
                    }
                }
            }
        }
    }

Done:

    if (pCurl)
    {
        curl_easy_cleanup (pCurl);
        pCurl = nullptr;
    }

    g_PushThreadRunning = 0;

    return NULL;
}


bool PushWalEntries ()
{
    CURL* pCurl = nullptr;

    if (false == g_Wal.IsReplayPending())
        return true;

    pCurl = curl_easy_init();

    if (!pCurl)
    {
        std::cerr << "curl_easy_init failed\n";
        return false;
    }

    g_Wal.Replay ([pCurl] (const std::vector<uint8_t>& Record)
    {
        return SendAlloyPayload (pCurl, g_szLokiPushApiURL, g_szLokiPushToken, (const char *) Record.data(), Record.size());
    });

    if (pCurl)
    {
        curl_easy_cleanup (pCurl);
        pCurl = nullptr;
    }

    return true;
}


void *WalThread (void *arg)
{
    (void)arg;
    g_WalThreadRunning = 1;

    while (0 == g_ShutdownRequested)
    {
        if (g_Wal.IsReplayPending())
        {
            PushWalEntries();
        }

        sleep_ms (2);
    }

    g_WalThreadRunning = 0;

    return NULL;
}


void handle_signal (int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        LogMessage ("Shutdown requested");
        g_ShutdownRequested = 1;
    }

    else if (sig == SIGHUP)
    {
        LogMessage ("Configuration reload requested");
        g_ReloadRequested = 1;
    }
}


int main()
{
    char szWalFile[2048] = {0};
    char *p = NULL;

    char    *pLine   = NULL;
    size_t  len      = 0;
    ssize_t nread    = 0;
    ssize_t nwritten = 0;

    struct sigaction sa {};

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction (SIGINT,  &sa, NULL);
    sigaction (SIGTERM, &sa, NULL);
    sigaction (SIGHUP,  &sa, NULL);

    p = getenv ("DOMINO_DATA_PATH");
    if (p)
    {
        snprintf (g_szNotesDataDir, sizeof (g_szNotesDataDir), "%s", p);
    }

    if (0 == IsDirectoryWritable (g_szNotesDataDir))
    {
        LogError ("Cannot write int server's data directory");
        goto Done;
    }

    snprintf (szWalFile, sizeof (szWalFile), "%s/nshlog.wal", g_szNotesDataDir);

    g_Wal.Init (szWalFile);

    GetHostname (sizeof (g_szHostname), g_szHostname);

    snprintf (g_szJob,         sizeof (g_szJob),         "%s",           g_szHostname);
    snprintf (g_szPidNbfFile,  sizeof (g_szPidNbfFile),  "%s/pid.nbf",   g_szNotesDataDir);
    snprintf (g_szLogFilename, sizeof (g_szLogFilename), "%s/notes.log", g_szNotesDataDir);

    curl_global_init (CURL_GLOBAL_DEFAULT);

    p = getenv ("LOKI_PUSH_TOKEN");
    if (p)
    {
        snprintf (g_szLokiPushToken, sizeof (g_szLokiPushToken), "%s", p);
    }

    p = getenv ("LOKI_PUSH_API_URL");
    if (p)
    {
        snprintf (g_szLokiPushApiURL, sizeof (g_szLokiPushApiURL), "%s", p);
    }

    p = getenv ("DOMINO_INPUT_FILE");
    if (p)
    {
        snprintf (g_szInputLogFile, sizeof (g_szInputLogFile), "%s", p);
    }

    p = getenv ("DOMINO_OUTPUT_LOG");
    if (p)
    {
        snprintf (g_szOutputLogFile, sizeof (g_szOutputLogFile), "%s", p);
    }

    if (false == IsNullStr (g_szOutputLogFile))
    {
        g_fdOutputLogFile = open (g_szOutputLogFile, O_CREAT | O_TRUNC | O_WRONLY, 0644);

        if (g_fdOutputLogFile >= 0)
            g_fdOut = g_fdOutputLogFile;
    }

    fprintf (stderr, "\nDomino Log Forwarder %s - %s %s\n\n", g_szVersion, g_szCopyright, g_szGitHubURL);

    if (0 != pthread_create (&g_PushThreadInstance, NULL, PushThread, NULL))
    {
        perror ("pthread_create");
        return EXIT_FAILURE;
    }

    if (0 != pthread_create (&g_WalThreadInstance, NULL, WalThread, NULL))
    {
        perror ("pthread_create");
        return EXIT_FAILURE;
    }

    while ((nread = getline (&pLine, &len, stdin)) != -1)
    {
        if (nread == 0)
            continue;

        nwritten = write (g_fdOut, pLine, nread);
        if (nread != nwritten)
        {
            /* Should we log this and where ... ? */
        }

        /* Remove new line */
        if ('\n' == pLine[nread-1])
            pLine[nread-1] = '\0';

        g_LogFifo.push (pLine);

    } /* while */

    sleep (2);

    LogMessage ("Terminating");

    while (g_Wal.IsReplayPending())
    {
        sleep_ms (10);
    }

    g_ShutdownRequested = 1;
    g_LogFifo.shutdown();

    while (g_PushThreadRunning)
    {
        sleep_ms (10);
    }

    while (g_WalThreadRunning)
    {
        sleep_ms (10);
    }

Done:

    LogMessage ("Thread Terminated");

    curl_global_cleanup();

    if (g_fdOutputLogFile >= 0)
    {
        close (g_fdOutputLogFile);
        g_fdOutputLogFile = -1;
    }

    if (pLine)
    {
        free (pLine);
        pLine = NULL;
    }

    return 0;
}
