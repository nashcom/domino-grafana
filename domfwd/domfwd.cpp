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

#define DOMFWD_VERSION_MAJOR 0
#define DOMFWD_VERSION_MINOR 9
#define DOMFWD_VERSION_PATCH 1

#define DOMFWD_VERSION_BUILD (DOMFWD_VERSION_MAJOR * 10000 +  DOMFWD_VERSION_MINOR * 100 + DOMFWD_VERSION_PATCH)


#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>


#include <stdint.h>
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

/* Environment Variables */

char g_szEnvLokiPushApiUrl[]   = "LOKI_PUSH_API_URL";
char g_szEnvLokiPushToken[]    = "LOKI_PUSH_TOKEN";
char g_szEnvLokiCaFile[]       = "LOKI_CA_FILE";
char g_szEnvLokiJob[]          = "LOKI_JOB";
char g_szEnvDominoDataPath[]   = "DOMINO_DATA_PATH";
char g_szEnvDominoOutputLog[]  = "DOMINO_OUTPUT_LOG";
char g_szEnvDominoInputFile[]  = "DOMINO_INPUT_FILE";
char g_szEnvLogLevel[]         = "DOMFWD_LOGLEVEL";
char g_szEnvMirrorToStdout[]   = "DOMFWD_MIRROR_STDOUT";
char g_szEnvAnnotateToStdout[] = "DOMFWD_ANNOTATE_STDOUT";
char g_szEnvAnnotateLogfile[]  = "DOMFWD_ANNOATED_LOG";
char g_szEnvPromFile[]         = "DOMFWD_PROM_FILE";
char g_szEnvHostname[]         = "DOMFWD_HOSTNAME";

/* Globals */

char g_szHostname[1024]        = {0};
char g_szJob[1024]             = {0};
char g_szPidNbfFile[2048]      = {0};
char g_szLokiPushApiURL[1024]  = {0};
char g_szLokiPushToken[1024]   = {0};
char g_szLokiCaFile[1024]      = {0};
char g_szWalFile[2048]         = {0};
char g_szInputLogFile[2048]    = {0};
char g_szOutputLogFile[2048]   = {0};
char g_szAnnotateLogfile[2048] = {0};
char g_szMetricsFileName[2048] = {0};

char g_szNotesDataDir[1024]    = "/local/notesdata";
char g_szNamespace[1024]       = "domino";
char g_szPod[1024]             = "newton.nashcom.de";

char g_szVersion[40]           = {0};
char g_szCopyright[]           = DOMFWD_COPYRIGHT;
char g_szGitHubURL[]           = DOMFWD_GITHUB_URL;
char g_szTask[]                = "domfwd";

char  g_szPromTypeGauge[]      = "gauge";
char  g_szPromTypeCounter[]    = "counter";
char  g_szPromTypeUntyped[]    = "untyped";
char  g_szPromPrefix[]         = "domfwd";
char  g_szEmpty[]              = "";
char  g_szProcessEmpty[]       = "unknown";

size_t g_ShutdownRequested     = 0;
size_t g_ReloadRequested       = 0;
size_t g_PushThreadRunning     = 0;
size_t g_WalThreadRunning      = 0;
size_t g_MetricsThreadRunning  = 0;
size_t g_LogLevel              = 0;
size_t g_Mirror2Stdout         = 0;
size_t g_Annotate2Stdout       = 0;
size_t g_DumpEnvironment       = 1;
size_t g_ShutdownMaxWaitSec    = 60;

pthread_t g_WalThreadInstance     = {0};
pthread_t g_PushThreadInstance    = {0};
pthread_t g_MetricsThreadInstance = {0};

time_t g_tStartTime = time (NULL);

int g_fdOutputLogFile     = -1;
int g_fdAnnotationLogFile = -1;
int g_fdStdOut = fileno (stdout);

struct stat g_PidNbfStat {};
string_fifo g_LogFifo;

SimpleWAL g_Wal;

std::atomic<std::int64_t> g_Metric_LogLines         {0};
std::atomic<std::int64_t> g_Metric_PushSuccess      {0};
std::atomic<std::int64_t> g_Metric_PushErrors       {0};
std::atomic<std::int64_t> g_Metric_PushRetrySuccess {0};
std::atomic<std::int64_t> g_Metric_PushRetryErrors  {0};


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
    if (NULL == pszMessage)
        return;

    fprintf (stderr, "%s: %s\n", g_szTask, pszMessage);
}

void LogInfo (const char *pszMessage)
{
    if (NULL == pszMessage)
        return;

    fprintf (stderr, "%s\n", pszMessage);
}

void LogError (const char *pszMessage)
{
    if (IsNullStr (pszMessage))
        return;

    fprintf (stderr, "%s: Error - %s\n", g_szTask, pszMessage);
}

void LogError (const char *pszMessage, const char *pszErrorText)
{
    if (IsNullStr (pszMessage))
        return;

    if (IsNullStr (pszErrorText))
    {
        LogError (pszMessage);
    }
    else
    {
        fprintf (stderr, "%s: Error - %s: %s\n", g_szTask, pszMessage, pszErrorText);
    }
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


bool MakeDirectoryTree (const char *pszDirectory)
{
    std::error_code ec;

    if (IsNullStr (pszDirectory))
        return false;

    if (std::filesystem::exists(pszDirectory))
        return true;

    std::filesystem::create_directories(pszDirectory, ec);

    if (ec)
        return false;

    return true;
}


bool MakeDirectoryTreeFromFileName (const char *pszFile)
{

    if (std::filesystem::exists (pszFile))
        return true;

    {
        std::filesystem::path file = pszFile;
        std::filesystem::path dir = file.parent_path();

        MakeDirectoryTree (dir.c_str());
    }

    return true;
}


void sleep_ms (unsigned int ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    nanosleep (&ts, NULL);
}


bool IdleDelay (size_t seconds)
{
    while (seconds)
    {
        if (g_ShutdownRequested)
            return true;

        sleep (1);
        seconds--;
    }

    if (g_ShutdownRequested)
        return true;

    return false;
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


static int64_t GetEpochNanoseconds()
{
    struct timespec ts {};
    clock_gettime (CLOCK_REALTIME, &ts);

    return ((int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec);
}


static std::string GetEpochNanosecondsText()
{
    struct timespec ts {};
    clock_gettime (CLOCK_REALTIME, &ts);

    int64_t ns = GetEpochNanoseconds();
    return std::to_string (ns);
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

    if (IsNullStr(pszProcess))
    {
        streamLabels.AddMember("process", rapidjson::Value (g_szProcessEmpty, alloc), alloc);
    }
    else
    {
        streamLabels.AddMember("process", rapidjson::Value (pszProcess, alloc), alloc);
    }

    /* ---- values ---- */
    rapidjson::Value values (rapidjson::kArrayType);

    rapidjson::Value valueEntry (rapidjson::kArrayType);

    std::string ts = GetEpochNanosecondsText();

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


std::string BuildLogPayload (const std::string& line, const pid_t pid, const char* pszProcess)
{
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    /* ---- timestamp (unix nanoseconds) ---- */
    uint64_t ts = GetEpochNanoseconds();
    doc.AddMember("ts", ts, alloc);

    /* ---- pid ---- */
    doc.AddMember("pid", static_cast<uint64_t>(pid), alloc);

    /* ---- process ---- */
    if (IsNullStr(pszProcess))
    {
        doc.AddMember ("process", rapidjson::Value(g_szProcessEmpty, alloc), alloc);
    }
    else
    {
        doc.AddMember ("process", rapidjson::Value(pszProcess, alloc), alloc);
    }

    /* ---- log line ---- */
    doc.AddMember ("line", rapidjson::Value(line.c_str(), alloc), alloc);

    /* ---- serialize ---- */
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept (writer);

    /* ---- newline-delimited JSON ---- */
    buffer.Put ('\n');

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


bool SendLokiPayload (CURL* pCurl, const char *pszURL, const char *pg_szLokiPushToken, const char* pszBuffer, size_t BufferLen)
{
    bool bSuccess = false;
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

    if (*g_szLokiCaFile)
    {
        curl_easy_setopt (pCurl, CURLOPT_CAINFO, g_szLokiCaFile);
    }

    if (false == IsNullStr (pg_szLokiPushToken))
    {
        curl_easy_setopt (pCurl, CURLOPT_XOAUTH2_BEARER, pg_szLokiPushToken);
        curl_easy_setopt (pCurl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    }

    rc = curl_easy_perform (pCurl);

    if (CURLE_OK != rc)
    {
        LogError ("Curl operation failed", szErrorBuffer);
        goto Done;
    }

    bSuccess = true;

Done:

    if (pHeaders)
    {
        curl_slist_free_all (pHeaders);
        pHeaders = nullptr;
    }

    return bSuccess;
}


void *PushThread (void *arg)
{
    (void)arg;
    CURL* pCurl = nullptr;
    std::string line;
    PidMap pidMap;
    pid_t  pid = 0;

    ssize_t nread    = 0;
    ssize_t nwritten = 0;

    g_PushThreadRunning = 1;

    if (g_LogLevel)
        LogMessage ("Push Thread started");

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
            g_Metric_LogLines.fetch_add (1, std::memory_order_relaxed);

            std::string processName;
            pid = ExtractProcessId (line);

            GetProcessName (pidMap, pid, processName);

            if (*g_szLokiPushApiURL)
            {
                std::string jLokiPayload = BuildLokiPayload (line, pid, processName.c_str());

                /* WAL-TESTING can be used to test WAL logic */
                if (jLokiPayload.find ("WAL-TESTING") != std::string::npos)
                {
                    LogMessage ("WAL-TESTING string received");
                    SendPayloadToWAL (jLokiPayload);
                }
                else
                {
                    if (false == SendLokiPayload (pCurl, g_szLokiPushApiURL, g_szLokiPushToken, jLokiPayload.c_str(), jLokiPayload.size()))
                    {
                        g_Metric_PushErrors.fetch_add (1, std::memory_order_relaxed);;
                        SendPayloadToWAL (jLokiPayload);
                    }
                    else
                    {
                        g_Metric_PushSuccess.fetch_add (1, std::memory_order_relaxed);;
                    }
                }
            }

            /* Write to annotation file and/or stdout */
            if ( g_Annotate2Stdout || (g_fdAnnotationLogFile >= 0) )
            {
                std::string jLogPayload = BuildLogPayload (line, pid, processName.c_str());
                nread = jLogPayload.size();

                if (g_Annotate2Stdout)
                {
                    nwritten = write (g_fdStdOut, jLogPayload.data(), nread);
                    if (nread != nwritten)
                    {
                        /* LATER: Should we log this error and where ... ? */
                    }
                }

                if (g_fdAnnotationLogFile >= 0)
                {
                    nwritten = write (g_fdAnnotationLogFile, jLogPayload.data(), nread);
                    if (nread != nwritten)
                    {
                        /* LATER: Should we log this error and where ... ? */
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

    if (g_LogLevel)
        LogMessage ("Push Thread ended");

    return NULL;
}


bool PushWalEntries()
{
    bool bSuccess = false;
    CURL* pCurl = nullptr;

    if (false == g_Wal.IsReplayPending())
        return true;

    pCurl = curl_easy_init();

    if (!pCurl)
    {
        std::cerr << "curl_easy_init failed\n";
        return false;
    }

    bSuccess = g_Wal.Replay ([pCurl] (const std::vector<uint8_t>& Record)
    {
        if (SendLokiPayload (pCurl, g_szLokiPushApiURL, g_szLokiPushToken, (const char *) Record.data(), Record.size()))
        {
            g_Metric_PushRetrySuccess.fetch_add (1, std::memory_order_relaxed);;
            return true;
        }
        else
        {
            g_Metric_PushRetryErrors.fetch_add (1, std::memory_order_relaxed);;
            return false;
        }
    });

    if (pCurl)
    {
        curl_easy_cleanup (pCurl);
        pCurl = nullptr;
    }

    return bSuccess;
}


void *WalThread (void *arg)
{
    (void)arg;
    g_WalThreadRunning = 1;

    if (g_LogLevel)
        LogMessage ("WAL Thread started");

    while (0 == g_ShutdownRequested)
    {
        if (g_Wal.IsReplayPending())
        {
            if (g_ShutdownRequested)
                break;

            if (false == PushWalEntries())
            {
                LogError ("PushWalEntries failed - Waiting 120 seconds");
                if (IdleDelay (120))
                {
                    break;
                }
            }
        }

        sleep (1);
    }

    g_WalThreadRunning = 0;

    if (g_LogLevel)
        LogMessage ("WAL Thread ended");

    return NULL;
}


bool WriteHelpAndType (FILE *fp, const char *pszStatName, const char *pszType, const char *pszDescription)
{
    if (NULL == fp)
        return false;

    if (IsNullStr (pszStatName))
        return false;

    if (IsNullStr (pszType))
        pszType = g_szPromTypeGauge;

    if (NULL == pszDescription)
        pszDescription = g_szEmpty;

    fprintf (fp, "# HELP %s_%s %s\n", g_szPromPrefix, pszStatName, pszDescription);
    fprintf (fp, "# TYPE %s_%s %s\n", g_szPromPrefix, pszStatName, pszType);

    return true;
}


bool WriteStatsEntryToFile (FILE *fp, uint64_t ValueNum, const char *pszStatName)
{
    if (NULL == fp)
        return false;

    if (NULL == pszStatName)
        return false;

    fprintf (fp, "%s_%s %zu\n", g_szPromPrefix, pszStatName, ValueNum);

    return true;
}

bool WriteStatsEntryToFileWithHelp (FILE *fp, uint64_t ValueNum, const char *pszStatName, const char *pszType, const char *pszDescription)
{
    if (NULL == fp)
        return false;

    if (NULL == pszStatName)
        return false;

    WriteHelpAndType (fp, pszStatName, pszType, pszDescription);
    fprintf (fp, "%s_%s %zu\n", g_szPromPrefix, pszStatName, ValueNum);

    return true;
}

bool WriteMetrics (bool bShutdown = false)
{
    char    szTempFilename[2200] = {0};
    char    szTmp[1024]          = {0};
    FILE    *fp = NULL;
    time_t  tNow = time(NULL);

    if (IsNullStr (g_szMetricsFileName))
        return false;

    snprintf (szTempFilename, sizeof (szTempFilename), "%s.tmp", g_szMetricsFileName);

    fp = fopen (szTempFilename, "w");

    if (NULL == fp)
    {
        return false;
    }

    snprintf (szTmp, sizeof (szTmp), "Domino Log Forwarder Build Version %s", g_szVersion);
    WriteStatsEntryToFileWithHelp (fp, DOMFWD_VERSION_BUILD, "build_number", g_szPromTypeGauge, szTmp);

    WriteStatsEntryToFileWithHelp (fp, g_tStartTime, "started_timestamp_seconds", g_szPromTypeGauge, "Unix timestamp when forwarder was started");
    WriteStatsEntryToFileWithHelp (fp, tNow - g_tStartTime, "uptime_seconds", g_szPromTypeGauge, "Uptime in seconds");

    WriteStatsEntryToFileWithHelp (fp, time(NULL), "lastupdate_timestamp_seconds", g_szPromTypeGauge, "Unix timestamp for last metrics update");
    WriteStatsEntryToFileWithHelp (fp, g_Metric_LogLines.load (std::memory_order_relaxed), "lines_received_total", g_szPromTypeGauge, "Total number of log lines received by forwarder");

    WriteHelpAndType      (fp, "push_total", g_szPromTypeCounter, "Total number of log lines pushed to the destination, labeled by result");
    WriteStatsEntryToFile (fp, g_Metric_PushSuccess.load (std::memory_order_relaxed),      "push_total{result=\"success\"}");
    WriteStatsEntryToFile (fp, g_Metric_PushErrors.load (std::memory_order_relaxed),       "push_total{result=\"error\"}");

    WriteHelpAndType      (fp, "push_retry_total", g_szPromTypeCounter, "Total number of log lines pushed to the destination, labeled by result");
    WriteStatsEntryToFile (fp, g_Metric_PushRetrySuccess.load (std::memory_order_relaxed), "push_retry_total{result=\"success\"}");
    WriteStatsEntryToFile (fp, g_Metric_PushRetryErrors.load (std::memory_order_relaxed),  "push_retry_total{result=\"error\"}");

    if (bShutdown)
    {
        WriteStatsEntryToFileWithHelp (fp, time(NULL), "shutdown_timestamp_seconds", g_szPromTypeGauge, "Unix timestamp when forwarder was shutdown");
    }

    if (fp)
    {
        fclose (fp);
        fp = NULL;

        rename (szTempFilename, g_szMetricsFileName);
    }

    return true;
}

void *MetricsThread (void *arg)
{
    (void)arg;

    g_MetricsThreadRunning = 1;

    if (g_LogLevel)
        LogMessage ("Metrics Thread started");

    while (true)
    {
        if (IdleDelay (10))
            break;

        WriteMetrics();
    }

    g_MetricsThreadRunning = 0;

    if (g_LogLevel)
        LogMessage ("Metrics Thread ended");

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

size_t GetEnvironmentValue (const char *pszEnvironmentName)
{
    char *p = NULL;

    if (IsNullStr (pszEnvironmentName))
        return 0;

    p = getenv (pszEnvironmentName);

    if (NULL == p)
        return 0;

    return atoi (p);
}


void PrintBanner()
{
    fprintf (stderr, "\nDomino Log Forwarder %s - %s %s\n\n", g_szVersion, g_szCopyright, g_szGitHubURL);
}


void LogHelpEnv (const char *pszParameter, const char *pszDescription)
{
    if (IsNullStr (pszParameter))
        return;

    if (IsNullStr (pszDescription))
        return;

    printf ("%-24s  %s\n", pszParameter, pszDescription);
}

void LogHelpCmd (const char *pszCmd, const char *pszDescription)
{
    if (IsNullStr (pszCmd))
        return;

    if (IsNullStr (pszDescription))
        return;

    printf ("%-24s  %s\n", pszCmd, pszDescription);
}


void PrintHelp ()
{
    printf ("\n");
    printf ("Domino Log Forwarder\n");
    printf ("--------------------\n");
    printf ("\n");
    printf ("Environment Variables:\n");
    printf ("\n");

    LogHelpEnv (g_szEnvLokiPushApiUrl,   "Loki Push URL for Loki Server (example: https://loki.example.com:3101/loki/api/v1/push");
    LogHelpEnv (g_szEnvLokiPushToken,    "Loki Push Token for Loki Server");
    LogHelpEnv (g_szEnvLokiCaFile,       "Loki Trusted Root CA File");
    LogHelpEnv (g_szEnvLokiJob,          "Loki Job Name (default: hostname)");
    LogHelpEnv (g_szEnvDominoOutputLog,  "Domino Output log file name");
    LogHelpEnv (g_szEnvLogLevel,         "Log level for stdout logging");
    LogHelpEnv (g_szEnvMirrorToStdout,   "Mirror stdin to stdout");
    LogHelpEnv (g_szEnvAnnotateToStdout, "Write annotated logs to stdout");
    LogHelpEnv (g_szEnvAnnotateLogfile,  "Write annotated logs to log file");
    LogHelpEnv (g_szEnvPromFile,         "Prom File for Metrics output (default: <notesdata>/domino/stats/domfwd.prom)");
    LogHelpEnv (g_szEnvHostname,         "Hostname to use (default: hostname read from OS)");

    printf ("\n");
    printf ("Command Line :\n");
    printf ("\n");
    LogHelpCmd ("-cfg",        "Print configuration");
    LogHelpCmd ("-env",        "Print environment variable config");
    LogHelpCmd ("-help/-h/-?", "Print print help");
    printf ("\n");
}


void LogCfgText (bool bShowEnvVars, const char *pszDescription, const char *pszValue, const char *pszParameter = "")
{

    if (IsNullStr (pszDescription))
        return;

    if (NULL == (pszValue))
        return;

    if (bShowEnvVars)
    {
        if (IsNullStr (pszParameter))
            return;

        printf ("%-24s :  %s\n", pszParameter, pszValue);
    }
    else
    {
        printf ("%-20s :  %s\n", pszDescription, pszValue);
    }
}


void LogCfgNum (bool bShowEnvVars, const char *pszDescription, size_t Value, const char *pszParameter = "")
{
    if (IsNullStr (pszDescription))
        return;

    if (bShowEnvVars)
    {
        if (IsNullStr (pszParameter))
            return;

        printf ("%-24s :  %lu\n", pszParameter, Value);
    }
    else
    {
        printf ("%-20s :  %lu\n", pszDescription, Value);
    }
}


void DumpConfig (bool bShowEnvVars = false)
{
    printf ("\n");
    printf ("Domino Log Forwarder Configuration\n");
    printf ("----------------------------------\n");
    printf ("\n");

    LogCfgNum  (bShowEnvVars, "LogLevel",            g_LogLevel,           g_szEnvLogLevel);
    LogCfgNum  (bShowEnvVars, "Mirror to stout",     g_Mirror2Stdout,      g_szEnvMirrorToStdout);
    LogCfgNum  (bShowEnvVars, "Annotate to stdout",  g_Annotate2Stdout,    g_szEnvAnnotateToStdout);
    LogCfgText (bShowEnvVars, "Annotate Log file",   g_szAnnotateLogfile,  g_szEnvAnnotateLogfile);
    LogCfgText (bShowEnvVars, "Notes Data Dir",      g_szNotesDataDir,     g_szEnvDominoDataPath);
    LogCfgText (bShowEnvVars, "Domino Output log",   g_szOutputLogFile,    g_szEnvDominoOutputLog);
    LogCfgText (bShowEnvVars, "Metrics File",        g_szMetricsFileName,  g_szEnvPromFile);
    LogCfgText (bShowEnvVars, "Hostname",            g_szHostname,         g_szEnvHostname);
    LogCfgText (bShowEnvVars, "Loki Job",            g_szJob,              g_szEnvLokiJob);
    LogCfgText (bShowEnvVars, "Loki Push API URL",   g_szLokiPushApiURL,   g_szEnvLokiPushApiUrl);
    LogCfgText (bShowEnvVars, "Loki Push Token",     g_szLokiPushToken,    g_szEnvLokiPushToken);
    LogCfgText (bShowEnvVars, "Loki CA File",        g_szLokiCaFile,       g_szEnvLokiCaFile);
    LogCfgText (bShowEnvVars, "WAL File",            g_szWalFile);

    printf ("\n");
}


void WriteEnvironment (int fd, const char* pszHeader)
{
    char** ppEnv = environ;

    if (fd < 0)
        return;

    if (NULL == ppEnv)
    {
        return;
    }

    if (!IsNullStr (pszHeader))
        dprintf (fd, "\n-----%s-----\n", pszHeader);

    while (*ppEnv)
    {
        dprintf (fd, "%s\n", *ppEnv);
        ppEnv++;
    }

    if (!IsNullStr(pszHeader))
        dprintf (fd, "-----%s-----\n\n", pszHeader);

}


int main (int argc, char *argv[])
{
    int a   = 0;
    char *p = NULL;
    char *pParam = NULL;

    char    *pLine       = NULL;
    size_t  CountSeconds = 0;
    size_t  len          = 0;
    size_t  seconds      = 0;
    ssize_t nread        = 0;
    ssize_t nwritten     = 0;

    struct sigaction sa {};

    sa.sa_handler = handle_signal;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction (SIGINT,  &sa, NULL);
    sigaction (SIGTERM, &sa, NULL);
    sigaction (SIGHUP,  &sa, NULL);

    snprintf (g_szVersion, sizeof (g_szVersion), "%d.%d.%d", DOMFWD_VERSION_MAJOR, DOMFWD_VERSION_MINOR, DOMFWD_VERSION_PATCH);

    /* Read configuration */

    g_LogLevel        = GetEnvironmentValue (g_szEnvLogLevel);
    g_Mirror2Stdout   = GetEnvironmentValue (g_szEnvMirrorToStdout);
    g_Annotate2Stdout = GetEnvironmentValue (g_szEnvAnnotateToStdout);

    p = getenv (g_szEnvDominoDataPath);
    if (p)
        snprintf (g_szNotesDataDir, sizeof (g_szNotesDataDir), "%s", p);

    if (0 == IsDirectoryWritable (g_szNotesDataDir))
    {
        LogError ("Cannot write int server's data directory");
        goto Done;
    }

    snprintf (g_szWalFile, sizeof (g_szWalFile), "%s/%s.wal", g_szNotesDataDir, g_szTask);

    p = getenv (g_szEnvAnnotateLogfile);
    if (p)
        snprintf (g_szAnnotateLogfile, sizeof (g_szAnnotateLogfile), "%s", p);

    p = getenv (g_szEnvPromFile);
    if (p)
        snprintf (g_szMetricsFileName, sizeof (g_szMetricsFileName), "%s", p);
    else
        snprintf (g_szMetricsFileName, sizeof (g_szMetricsFileName), "%s/domino/stats/domfwd.prom", g_szNotesDataDir);

    p = getenv (g_szEnvHostname);
    if (p)
        snprintf (g_szHostname, sizeof (g_szHostname), "%s", p);
    else
        GetHostname (sizeof (g_szHostname), g_szHostname);

    p = getenv (g_szEnvLokiJob);
    if (p)
        snprintf (g_szJob, sizeof (g_szJob), "%s", p);
    else
        snprintf (g_szJob, sizeof (g_szJob), "%s", g_szHostname);

    snprintf (g_szPidNbfFile,  sizeof (g_szPidNbfFile),  "%s/pid.nbf",   g_szNotesDataDir);

    p = getenv (g_szEnvLokiPushToken);
    if (p)
        snprintf (g_szLokiPushToken, sizeof (g_szLokiPushToken), "%s", p);

    p = getenv (g_szEnvLokiPushApiUrl);
    if (p)
        snprintf (g_szLokiPushApiURL, sizeof (g_szLokiPushApiURL), "%s", p);

    p = getenv (g_szEnvLokiCaFile);
    if (p)
        snprintf (g_szLokiCaFile, sizeof (g_szLokiCaFile), "%s", p);

    p = getenv (g_szEnvDominoInputFile);
    if (p)
        snprintf (g_szInputLogFile, sizeof (g_szInputLogFile), "%s", p);

    p = getenv (g_szEnvDominoOutputLog);
    if (p)
        snprintf (g_szOutputLogFile, sizeof (g_szOutputLogFile), "%s", p);

    for (a=1; a<argc; a++)
    {
        pParam = argv[a];

        if ( (0 == strcasecmp (pParam, "--version")) ||
             (0 == strcasecmp (pParam, "-version")) )
        {
            printf ("%s", g_szVersion);
            goto Done;
        }

        else if ( (0 == strcasecmp (pParam, "-help")) ||
             (0 == strcasecmp (pParam, "-h")) ||
             (0 == strcasecmp (pParam, "-?")) )
        {
            PrintHelp();
            return 0;
        }

        else if ( (0 == strcasecmp (pParam, "-config")) ||
             (0 == strcasecmp (pParam, "-cfg")) )
        {
            DumpConfig();
            return 0;
        }

        else if (0 == strcasecmp (pParam, "-env"))
        {
            DumpConfig (true);
            return 0;
        }

        else
        {
            LogError ("Invalid parameter", pParam);
            return 1;
        }
    }

    PrintBanner();

    /* --- No operations before this point because the parameter read loop exits for some parameters */

    MakeDirectoryTreeFromFileName (g_szMetricsFileName);
    g_Wal.Init (g_szWalFile);
    curl_global_init (CURL_GLOBAL_DEFAULT);

    if (false == IsNullStr (g_szOutputLogFile))
    {
        g_fdOutputLogFile = open (g_szOutputLogFile, O_CREAT | O_APPEND | O_WRONLY, 0644);
    }

    if (false == IsNullStr (g_szAnnotateLogfile))
        g_fdAnnotationLogFile = open (g_szAnnotateLogfile, O_CREAT | O_TRUNC | O_WRONLY, 0644);

    /* Create threads */

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

    if (0 != pthread_create (&g_MetricsThreadInstance, NULL, MetricsThread, NULL))
    {
        perror ("pthread_create");
        return EXIT_FAILURE;
    }

    if (g_Mirror2Stdout)
    {
        if (g_DumpEnvironment)
            WriteEnvironment (g_fdStdOut, "Environment");
    }

    /* Read from stdin and process the log line (annotating it, writing it to a log, pushing it to Loki, ...) */

    while ((nread = getline (&pLine, &len, stdin)) != -1)
    {
        if (nread == 0)
            continue;

        /* Mirror unmodified to a log file */
        if (g_fdOutputLogFile >= 0)
        {
            nwritten = write (g_fdOutputLogFile, pLine, nread);
            if (nread != nwritten)
            {
                /* LATER: Should we log this error and where ... ? */
            }
        }

        /* Mirror it unmodified to stdout */
        if (g_Mirror2Stdout)
        {
            nwritten = write (g_fdStdOut, pLine, nread);
            if (nread != nwritten)
            {
                /* LATER: Should we log this error and where ... ? */
            }
        }

        /* Remove new line */
        if ('\n' == pLine[nread-1])
            pLine[nread-1] = '\0';

        g_LogFifo.push (pLine);

    } /* while */

    sleep (2);

    LogMessage ("Waiting for shutdown to complete");

    seconds = 0;
    while (g_Wal.IsReplayPending())
    {
        sleep (1);

        if (g_ShutdownRequested)
            break;

        if (seconds++ > g_ShutdownMaxWaitSec)
        {
            LogMessage ("Shutdown timeout reached");
            break;
        }
    }

    g_ShutdownRequested = 1;
    g_LogFifo.shutdown();

    while (g_PushThreadRunning || g_WalThreadRunning || g_MetricsThreadRunning)
    {
        CountSeconds++;
        if (0 == (CountSeconds %10))
        {
            if (g_LogLevel)
                fprintf (stderr, "Waiting %lu seconds for threads to terminate (Push: %lu, Wal: %lu, Metrics: %lu)\n", CountSeconds, g_PushThreadRunning, g_WalThreadRunning, g_MetricsThreadRunning);
        }

        sleep (1);

        if (CountSeconds > 300)
        {
            fprintf (stderr, "Failed to terminate all threads (Push: %lu, Wal: %lu, Metrics: %lu)\n", g_PushThreadRunning, g_WalThreadRunning, g_MetricsThreadRunning);
            break;
        }
    }

Done:

    curl_global_cleanup();

    if (g_fdOutputLogFile >= 0)
    {
        close (g_fdOutputLogFile);
        g_fdOutputLogFile = -1;
    }

    if (g_fdAnnotationLogFile >= 0)
    {
        close (g_fdAnnotationLogFile);
        g_fdAnnotationLogFile = -1;
    }

    if (pLine)
    {
        free (pLine);
        pLine = NULL;
    }

    WriteMetrics (true);
    LogMessage ("Shutdown completed");

    return 0;
}
