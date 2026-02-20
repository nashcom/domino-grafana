/*
###########################################################################
# Domino Prometheus Exporter                                              #
# Version 1.0.3 20.02.2026                                                #
# (C) Copyright Daniel Nashed/Nash!Com 2024-2026                          #
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


#define TASKNAME "DOMPROM"
#define MsgQueueName TASK_QUEUE_PREFIX TASKNAME

#define DOMPROM_VERSION_MAJOR 1
#define DOMPROM_VERSION_MINOR 0
#define DOMPROM_VERSION_PATCH 3

#define DOMPROM_VERSION_BUILD (DOMPROM_VERSION_MAJOR * 10000 +  DOMPROM_VERSION_MINOR * 100 + DOMPROM_VERSION_PATCH)

#define DOMPROM_COPYRIGHT  "Copyright Daniel Nashed/Nash!Com 2024-2026"
#define DOMPROM_GITHUB_URL "https://github.com/nashcom/domino-grafana"

#define ENV_DOMPROM_LOGLEVEL          "domprom_loglevel"
#define ENV_DOMPROM_STATS_DIR         "domprom_outdir"
#define ENV_DOMPROM_OUTFILE           "domprom_outfile"
#define ENV_DOMPROM_TRANS_OUTFILE     "domprom_trans_outfile"
#define ENV_DOMPROM_INTERVAL          "domprom_interval"
#define ENV_DOMPROM_NO_PREFIX         "domprom_no_domino_prefix"
#define ENV_DOMPROM_COLLECT_TRANS     "domprom_collect_trans"
#define ENV_DOMPROM_COLLECT_IOSTAT    "domprom_collect_iostat"
#define ENV_DOMPROM_INTERVAL_TRANS    "domprom_interval_trans"
#define ENV_DOMPROM_INTERVAL_IOSTAT   "domprom_interval_iostat"
#define ENV_DOMPROM_MAINTENANCE_START "domprom_maintenance_start"
#define ENV_DOMPROM_MAINTENANCE_END   "domprom_maintenance_end"

#define DOMPROM_DEFAULT_INTERVAL_SEC          60
#define DOMPROM_DEFAULT_TRANS_INTERVAL_SEC   180
#define DOMPROM_DEFAULT_IOSTAT_INTERVAL_SEC  600

#define DOMPROM_MINIMUM_INTERVAL_SEC          10
#define DOMPROM_MINIMUM_TRANS_INTERVAL_SEC    60
#define DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC   60

#define DOMPROM_DISK_COMPONENT_NOTESDATA     "Notesdata"
#define DOMPROM_DISK_COMPONENT_TRANSLOG      "Translog"
#define DOMPROM_DISK_COMPONENT_DAOS          "DAOS"
#define DOMPROM_DISK_COMPONENT_NIF           "NIF"
#define DOMPROM_DISK_COMPONENT_FT            "FT"
#define DOMPROM_DISK_COMPONENT_NOTES_TEMP    "NotesTemp"
#define DOMPROM_DISK_COMPONENT_VIEW_REBUILD  "ViewRebuild"
#define DOMPROM_DISK_COMPONENT_NOTES_LOG_DIR "NotesLogDir"

#define MAX_STAT_DESC  2048
#define MAX_STAT_NAME   120

#define sizeofstring(x) (sizeof (x) - 1)

/* Includes */

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <dirent.h>
  #include <sys/statvfs.h>
  #include <limits.h>
#endif

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>


#include <global.h>
#include <addin.h>
#include <idtable.h>
#include <intl.h>
#include <kfm.h>
#include <misc.h>
#include <miscerr.h>
#include <mq.h>
#include <nsfdb.h>
#include <nsfnote.h>
#include <nsfsearc.h>
#include <osenv.h>
#include <oserr.h>
#include <osfile.h>
#include <osmem.h>
#include <osmisc.h>
#include <ostime.h>
#include <stats.h>
#include <stdnames.h>

#include <cstdio>
#include <string>
#include <vector>
#include <list>
#include <cstdio>
#include <cinttypes>
#include <algorithm>
#include <ctime>
#include <cstdint>
#include <unordered_map>


#ifdef _WIN32
  #define strcasecmp _stricmp
#endif


/* Types */

struct CONTEXT_STRUCT_TYPE
{
    INTLFORMAT Intl;

    char   szPrefix[20];

    BOOL   bExportLong;
    BOOL   bExportNumber;
    BOOL   bExportText;
    BOOL   bExportTime;

    size_t CountAll;
    size_t CountText;
    size_t CountLong;
    size_t CountNumber;
    size_t CountTime;
    size_t CountInvalid;
    size_t CountUnknown;

    FILE *fp;
};


/* Globals */

char  g_szVersion[40]       = {0};
char  g_szCopyright[]       = DOMPROM_COPYRIGHT;
char  g_szGitHubURL[]       = DOMPROM_GITHUB_URL;
char  g_szDominoHealth[]    = "DominoHealth";
char  g_szDominoTrans[]     = "DominoTrans";
char  g_szTask[]            = "domprom";
char  g_szTaskLong[]        = "Prometheus Exporter";
char  g_szDominoProm[]      = "domino.prom";
char  g_szDominoTransProm[] = "domino_trans.prom";

char  g_szLocalUser[MAXUSERNAME+1] = {0};
char  g_szDataDir[MAXPATH+1]       = {0};
char  g_szDirTranslog[MAXPATH+1]   = {0};
char  g_szDirDAOS[MAXPATH+1]       = {0};
char  g_szDirNIF[MAXPATH+1]        = {0};
char  g_szDirFT[MAXPATH+1]         = {0};
char  g_szNotesTemp[MAXPATH+1]     = {0};
char  g_szViewRebuild[MAXPATH+1]   = {0};
char  g_szNotesLogDir[MAXPATH+1]   = {0};
char  g_szStatsFilename[2*MAXPATH+200] = {0};
char  g_szTransFilename[2*MAXPATH+200] = {0};

char  g_szPromTypeGauge[]     = "gauge";
char  g_szPromTypeCounter[]   = "counter";
char  g_szPromTypeUntyped[]   = "untyped";
char  g_szEmpty[]             = "";
char  g_szEvents4[]           = "events4.nsf";

WORD   g_wTranslogLogType     = 0;
WORD   g_wServerRestricted    = 0;
int    g_StatusDAOS           = 0;
int    g_CatalogInSyncDAOS    = 0;
size_t g_TranslogMinLogExtend = 0;
size_t g_TranslogMaxLogExtend = 0;
WORD   g_wWriteDominoHealthStats   = 1;
WORD   g_wCollectDominoTransStats  = 0;
WORD   g_wCollectDominoIOStat      = 0;

TIMEDATE g_tNextTransStatsUpdate = {0};
TIMEDATE g_tNextIOStatUpdate     = {0};
TIMEDATE g_tMaintenanceStart     = {0};
TIMEDATE g_tMaintenanceEnd       = {0};

WORD     g_wMaintenanceEnabled   = 0;
BOOL     g_bMaintenanceStartSet  = FALSE;
BOOL     g_bMaintenanceEndSet    = FALSE;

#define MAX_CONFIG_VALUE_OVERRIDE 99

#ifdef _WIN32
char g_DirSep = '\\';
#else
char g_DirSep = '/';
#endif

char  g_PromDelimChar       = ' ';
WORD  g_ShutdownPending     = 0;
WORD  g_wLogLevel           = 0;
DWORD g_dwIntervalSec       = DOMPROM_DEFAULT_INTERVAL_SEC;
DWORD g_dwTransIntervalSec  = DOMPROM_DEFAULT_TRANS_INTERVAL_SEC;
DWORD g_dwIOStatIntervalSec = DOMPROM_DEFAULT_IOSTAT_INTERVAL_SEC;

/* Helper list to process disk stats and write them separately (Totals and Free) */

std::list<std::string> g_ListDiskTotalStats;
std::list<std::string> g_ListDiskFreeStats;

/* Helper list to process transactions and write them separately (count and total) */

std::list<std::string> g_ListTransCountStats;
std::list<std::string> g_ListTransTotalMsStats;

/* Currently the value is unused. But on purpose this is a map which can later hold values as well */

static std::unordered_map<std::string, double> g_DominoStat;
static std::unordered_map<std::string, std::string> g_DominoStatDescription;


static size_t g_DominoStatCount = 0;
static size_t g_DominoStatMax   = 0;

#define DOMSTAT_NEW                 0
#define DOMSTAT_DUPLICATE_SAME      1
#define DOMSTAT_DUPLICATE_DIFFERENT 2


void BeginDominoStatCollection()
{
    g_DominoStat.clear();

    if (g_DominoStatMax > 0)
    {
        g_DominoStat.reserve (g_DominoStatMax);
    }

    g_DominoStatCount = 0;
}


int RegisterDominoStat (const char *pszName, double value)
{
    if (pszName == nullptr)
    {
        return DOMSTAT_DUPLICATE_SAME;
    }

    auto result = g_DominoStat.emplace (std::string (pszName), value);

    if (result.second)
    {
        ++g_DominoStatCount;

        if (g_DominoStatCount > g_DominoStatMax)
        {
            g_DominoStatMax = g_DominoStatCount;
        }

        return DOMSTAT_NEW;
    }

    if (result.first->second == value)
    {
        return DOMSTAT_DUPLICATE_SAME;
    }

    result.first->second = value;
    return DOMSTAT_DUPLICATE_DIFFERENT;
}


class PrefixFilter
{

public:

    void AddInclude (const std::string& p)
    {
        m_include.push_back(p);
    }

    void AddExclude (const std::string& p)
    {
        m_exclude.push_back(p);
    }

    // Sort include/exclude by len for faster matching and simple logic. the longest match wins
    void Finalize ()
    {
        auto byLengthDesc = [] (const std::string& a, const std::string& b)
        {
            return a.size() > b.size();
        };

        std::sort (m_include.begin(), m_include.end(), byLengthDesc);
        std::sort (m_exclude.begin(), m_exclude.end(), byLengthDesc);
    }

    // default include if not found

    bool ShouldInclude (const std::string& name) const
    {
        if (Matches(m_include, name))
            return true;

        if (Matches(m_exclude, name))
            return false;

        return true;
    }

    bool ShouldExclude (const std::string& name) const
    {
        if (Matches (m_include, name))
            return false;

        if (Matches (m_exclude, name))
            return true;

        return false;
    }


private:

    static bool Matches (const std::vector<std::string>& prefixes,
                         const std::string& value)
    {
        for (const auto& p : prefixes)
        {
            if (value.compare(0, p.size(), p) == 0)
                return true;
        }
        return false;
    }

    std::vector<std::string> m_include;
    std::vector<std::string> m_exclude;
};


PrefixFilter g_StatsFilter;


void TruncateAtFirstBlank (char *pszBuffer)
{
    char *p = pszBuffer;

    if (NULL == p)
        return;

    while (*p)
    {
        if (' ' == *p)
        {
            *p = '\0';
            break;
        }

        p++;
    } /* while */
}


void ReplaceChars (char *pszBuffer)
{
    char *p = pszBuffer;

    if (NULL == p)
        return;

    while (*p)
    {
        if ( ((*p >= 'a') && (*p <= 'z')) ||
             ((*p >= 'A') && (*p <= 'Z')) ||
             ((*p >= '0') && (*p <= '9')) )
        {
            /* Valid chars */
        }

        else if ('/' == *p)
        {
            /* LATER: Root disk on Linux needs special handling */
            *p = 'C';
        }
        else
        {
            *p = '_';
        }

        p++;
    } /* while */
}


bool IsNullStr (const char *pszStr)
{
    if (NULL == pszStr)
        return true;

    if ('\0' == *pszStr)
        return true;

    return false;
}


int FileExists (const char *pszFilename)
{
    int ret = 0;

#ifdef _WIN32
    struct _stat Filestat = {0};
#else
    struct stat Filestat = {0};
#endif

    if (IsNullStr (pszFilename))
        return 0;

#ifdef _WIN32
        ret = _stat (pszFilename, &Filestat);
#else
        ret = stat (pszFilename, &Filestat);

#endif

    if (ret)
        return 0;

    if (S_IFDIR & Filestat.st_mode)
        return 2;
    else
        return 1;
}


int RemoveFile (const char *pszFilename, WORD wLogLevel)
{
    int ret = 0;

    if (IsNullStr (pszFilename))
        return 0;

    if (0 == FileExists (pszFilename))
    {
        if (wLogLevel > 1)
        {
            AddInLogMessageText ("%s: File does not exist when deleting: %s", 0, g_szTask, pszFilename);
        }

        return 0;
    }

    ret = remove (pszFilename);

    if (FileExists (pszFilename))
    {
        AddInLogMessageText ("%s: File cannot be deleted: %s", 0, g_szTask, pszFilename);
    }
    else
    {
        if (wLogLevel)
        {
            AddInLogMessageText ("%s: Info: File deleted %s", 0, g_szTask, pszFilename);
        }
    }

    return ret;
}


bool CreateDirIfNotExists (const char *pszFilename)
{
    int  ret = 0;
    bool bCreated = false;

    if (IsNullStr (pszFilename))
        return false;

    ret = FileExists (pszFilename);

    /* Directory already exists */
    if (2 == ret)
        return true;

    /* A file with the same name already exists */
    if (1 == ret)
    {
        AddInLogMessageText ("%s: Cannot create directory because a file with the same name exists: %s", 0, g_szTask, pszFilename);
        return false;
    }

#ifdef _WIN32
        if (TRUE == CreateDirectory (pszFilename, NULL))
        {
            bCreated = true;
        }
        else
        {
            perror ("Cannot create directory");
        }

#else
        if (0 == mkdir (pszFilename, 0770))
        {
            bCreated = true;
        }
        else
        {
            perror ("Cannot create directory");
        }

#endif

    if (bCreated)
        AddInLogMessageText ("%s: Directory created: %s", 0, g_szTask, pszFilename);
    else
        AddInLogMessageText ("%s: Cannot create directory: %s", 0, g_szTask, pszFilename);

    return bCreated;
}


BOOL GetNotesTimeDateSting (TIMEDATE *pTimedate, WORD wRetBufferSize, char *retpszBuffer)
{
    BOOL bError = TRUE;
    TIME NotesTime  = {0};

    if ((NULL == retpszBuffer) || (0 == wRetBufferSize))
        return FALSE;

     *retpszBuffer = '\0';

    if (NULL == pTimedate)
        return FALSE;

    memcpy (&(NotesTime.GM), pTimedate, sizeof (TIMEDATE));

    bError = TimeGMToLocalZone (&NotesTime);

    if (bError)
    {
        *retpszBuffer = '\0';
        goto Done;
    }

    snprintf (retpszBuffer,
              wRetBufferSize,
              "%04u-%02u-%02uT%02u:%02u:%02u:%02uZ",
              NotesTime.year,
              NotesTime.month,
              NotesTime.day,
              NotesTime.hour,
              NotesTime.minute,
              NotesTime.second,
              NotesTime.hundredth);

Done:
    return bError;
}


STATUS DumpBufferToFile (const char *pszFileName, const char *pszBuffer)
{
    STATUS error = NOERROR;
    FILE *pFile = NULL;
    pFile = fopen (pszFileName, "wb");

    if (IsNullStr (pszFileName))
        return ERR_MISC_INVALID_ARGS;

    if (IsNullStr (pszBuffer))
        return ERR_MISC_INVALID_ARGS;

    fwrite ((const char *) pszBuffer, 1, strlen (pszBuffer), pFile);

    if (pFile)
    {
        fclose (pFile);
        pFile = NULL;
    }

    return error;
}


#ifdef _WIN32
bool LmbcsToWide (const char *pszPathLMBCS, wchar_t *retpwszBuffer, DWORD dwBufferLen)
{
    if (retpwszBuffer && dwBufferLen)
        *retpwszBuffer = '\0';

    if (NULL == pszPathLMBCS)
        return false;

    OSTranslate32 (OS_TRANSLATE_LMBCS_TO_UNICODE, pszPathLMBCS, MAXDWORD, (char *) retpwszBuffer, dwBufferLen);
    return true;
}
#else
bool LmbcsToUtf8 (const char *pszPathLMBCS, char *retpszBuffer, DWORD dwBufferLen)
{
    if (retpszBuffer && dwBufferLen)
        *retpszBuffer = '\0';

    if (NULL == pszPathLMBCS)
        return false;

    OSTranslate32 (OS_TRANSLATE_LMBCS_TO_UTF8, pszPathLMBCS, MAXDWORD, (char *) retpszBuffer, dwBufferLen);
    return true;
}
#endif


bool GetDiskUsageFrom (const char *pszPathLMBCS,
                       uint64_t   *retpTotalBytes,
                       uint64_t   *retpFreeBytes)
{
#ifdef _WIN32
    wchar_t pwszPath[MAX_PATH+1] = {0};
    ULARGE_INTEGER freeAvail  = {0};
    ULARGE_INTEGER total      = {0};
    ULARGE_INTEGER freeTotal  = {0};

    if (!LmbcsToWide (pszPathLMBCS, pwszPath, sizeof (pwszPath)-1))
        return false;

    if (!GetDiskFreeSpaceExW (pwszPath, &freeAvail, &total, &freeTotal))
        return false;

    if (retpTotalBytes)
        *retpTotalBytes = (uint64_t)total.QuadPart;

    if (retpFreeBytes)
        *retpFreeBytes = (uint64_t)freeAvail.QuadPart;

    return true;

#else
    char pszPathUtf8[PATH_MAX+1] = {0};
    struct statvfs s  = {0};

    if (!LmbcsToUtf8 (pszPathLMBCS, pszPathUtf8, sizeof(pszPathUtf8)-1))
        return false;

    if (statvfs(pszPathUtf8, &s) != 0)
        return false;

    uint64_t blockSize = (s.f_frsize != 0) ? (uint64_t)s.f_frsize : (uint64_t)s.f_bsize;

    if (retpTotalBytes)
        *retpTotalBytes = (uint64_t)s.f_blocks * blockSize;

    if (retpFreeBytes)
        *retpFreeBytes = (uint64_t)s.f_bavail * blockSize;

    return true;
#endif
}


int CompareCaseInsensitive (const char *pszStr1, const char *pszStr2)
{
    if (NULL == pszStr1)
        return 0;

    if (NULL == pszStr2)
        return 0;

    return ( IntlTextCompare (pszStr1, (WORD)strlen(pszStr1), pszStr2,(WORD)strlen (pszStr2), 0));
}


uint64_t EpochFromUtcTm (struct tm* pUtcTm)
{
#ifdef _WIN32
    return static_cast<uint64_t>(_mkgmtime64(pUtcTm));
#else
    return static_cast<uint64_t>(timegm(pUtcTm));
#endif
}


uint64_t TimeDateToEpoch (const TIMEDATE* pTimeDate)
{
    if (NULL == pTimeDate)
        return 0;

    TIME t = {};
    struct tm utc_tm = {};

    t.GM = *pTimeDate;

    if (TimeGMToLocal (&t))
    {
        return 0;
    }

    if (t.year < 1900)
    {
        return 0;
    }

    utc_tm.tm_year  = t.year - 1900;
    utc_tm.tm_mon   = t.month - 1;
    utc_tm.tm_mday  = t.day;
    utc_tm.tm_hour  = t.hour;
    utc_tm.tm_min   = t.minute;
    utc_tm.tm_sec   = t.second;
    utc_tm.tm_isdst = 0;

    return EpochFromUtcTm (&utc_tm);
}


STATUS StatUpdateText (const char *pszStatPkg, const char *pszName, const char *pszText)
{
    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    if (NULL == pszText)
        return ERR_MISC_INVALID_ARGS;

    return StatUpdate (pszStatPkg, pszName, ST_UNIQUE, VT_TEXT, pszText);
}


STATUS StatUpdateLong (const char *pszStatPkg, const char *pszName, LONG lValue)
{
    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    return StatUpdate(pszStatPkg, pszName, ST_UNIQUE, VT_LONG, &lValue);
}


STATUS StatUpdateNumber (const char *pszStatPkg, const char *pszName, NUMBER *pnValue)
{
    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    if (NULL == pnValue)
    {
        StatDelete (pszStatPkg, pszName);
        return NOERROR;
    }

    return StatUpdate(pszStatPkg, pszName, ST_UNIQUE, VT_NUMBER, pnValue);
}


STATUS StatUpdateNumber (const char *pszStatPkg, const char *pszName, uint64_t Int64Value)
{
    NUMBER nNumber = {0};

    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    nNumber = (NUMBER) Int64Value;

    return StatUpdate(pszStatPkg, pszName, ST_UNIQUE, VT_NUMBER, &nNumber);
}


STATUS StatUpdateNumberBytesInMB (const char *pszStatPkg, const char *pszName, NUMBER *pnBytes)
{
    NUMBER nMB = {0};

    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    nMB = round (((*pnBytes) / 1048576.0) * 1000.0)/1000.0;

    return StatUpdateNumber (pszStatPkg, pszName, &nMB);
}


STATUS StatUpdateNumberBytesInMB (const char *pszStatPkg, const char *pszName, uint64_t Int64Value)
{
    NUMBER nMB = {0};

    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    nMB = round (((NUMBER) Int64Value / 1048576.0)*1000.0)/1000.0;

    return StatUpdateNumber (pszStatPkg, pszName, &nMB);
}


STATUS StatUpdateTimedate (const char *pszStatPkg, const char *pszName, TIMEDATE *ptdTimedateValue)
{
    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    if (NULL == ptdTimedateValue)
    {
        StatDelete (pszStatPkg, pszName);
        return NOERROR;
    }

    return StatUpdate (pszStatPkg, pszName, ST_UNIQUE, VT_TIMEDATE, ptdTimedateValue);
}


STATUS DeleteStat (const char *pszStatPkg, const char *pszName)
{
    if (IsNullStr (pszStatPkg) || IsNullStr (pszName))
        return ERR_MISC_INVALID_ARGS;

    return NOERROR;
}

STATUS DeleteAllStatsForPackage (const char *pszStatPkg)
{
    if (IsNullStr (pszStatPkg))
        return ERR_MISC_INVALID_ARGS;

    StatDelete (pszStatPkg, "");

    return NOERROR;
}


STATUS UpdateStatusDAOS (const char *pszValue)
{
    STATUS error = NOERROR;

    if (NULL == pszValue)
        return ERR_MISC_INVALID_ARGS;

    if (0 == CompareCaseInsensitive (pszValue, "Enabled"))
    {
        g_StatusDAOS = 1;
    }
    else
    {
        g_StatusDAOS = 0;
    }

    return error;
}


STATUS UpdateCatalogDAOS (const char *pszValue)
{
    STATUS error = NOERROR;

    if (NULL == pszValue)
        return ERR_MISC_INVALID_ARGS;

    if (0 == CompareCaseInsensitive (pszValue, "Synchronized"))
    {
        g_CatalogInSyncDAOS = 0;
    }
    else
    {
        g_CatalogInSyncDAOS = 1;
    }

    return error;
}


void UpdateIdleStatus()
{
    char szStatus [MAXSPRINTF+1] = {0};

    if (g_wCollectDominoTransStats)

        snprintf (szStatus, sizeof (szStatus), "Idle (collect int: %u sec / trans int: %u sec)", g_dwIntervalSec, g_dwTransIntervalSec);
    else
        snprintf (szStatus, sizeof (szStatus), "Idle (collect interval: %u sec)", g_dwIntervalSec);

    AddInSetStatusText (szStatus);
}


size_t GetTranslogExtendNumber (const char *pszFilename)
{
    size_t num   = 0;
    size_t digit = 0;
    const char *p = pszFilename;

    if (NULL == p)
        return 0;

    if ('S' != *p)
        return 0;

    p++;

    while (*p)
    {
        if ((*p >= '0') && (*p <= '9'))
        {
            digit = *p - '0';
            num = num * 10 + digit;
        }
        else if ('.' == *p)
        {
            break;
        }
        else
        {
            return 0;
        }

        p++;
    }

    return num;
}


bool WriteHelpAndType (FILE *fp, const char *pszPrefix, const char *pszStatName, const char *pszType, const char *pszDescription)
{
    if (NULL == fp)
        return false;

    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    if (IsNullStr (pszType))
        pszType = g_szPromTypeGauge;

    if (NULL == pszDescription)
        pszDescription = g_szEmpty;

    fprintf (fp, "# HELP %s_%s %s\n", pszPrefix, pszStatName, pszDescription);
    fprintf (fp, "# TYPE %s_%s %s\n", pszPrefix, pszStatName, pszType);

    return true;
}


bool WriteStatsEntryToFile (FILE *fp, const char *pszPrefix, const char *pszStatName, const char *pszDescription, uint64_t ValueNum)
{
    if (NULL == fp)
        return false;

    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    if (false == WriteHelpAndType (fp, pszPrefix, pszStatName, NULL, pszDescription))
        return false;

    fprintf (fp, "%s_%s %zu\n", pszPrefix, pszStatName, ValueNum);

    return true;
}


bool WriteStatsEntryToFile (FILE *fp, const char *pszPrefix, const char *pszStatName, const char *pszDescription, const char *pszValueString)
{
    if (NULL == fp)
        return false;

    if (NULL == pszPrefix)
        return false;

    if (NULL == pszStatName)
        return false;

    if (NULL == pszValueString)
        return false;

    if (false == WriteHelpAndType (fp, pszPrefix, pszStatName, NULL, pszDescription))
        return false;

    fprintf (fp, "%s_%s %s\n", pszPrefix, pszStatName, pszValueString);

    return true;
}


bool WriteTimedateStat (FILE *fp, const char *pszStatName, const char *pszDescription, void *pValue)
{
    uint64_t EpochTime = 0;


    if (NULL == fp)
        return false;

    if (NULL == pszStatName)
        return false;

    if (NULL == pValue)
        return false;

    EpochTime = TimeDateToEpoch ((TIMEDATE *) pValue);

    if (0 == EpochTime)
        return false;

    WriteStatsEntryToFile (fp, g_szDominoHealth, pszStatName, pszDescription, EpochTime);

    return true;
}


STATUS AddIDUnique (void far *phNoteIDTable, SEARCH_MATCH far *pSearchInfo, ITEM_TABLE far *pSummaryInfo)
{
    DHANDLE       hNoteIDTable = NULLHANDLE;
    STATUS        error        = NOERROR;
    BOOL          bFlagOK      = FALSE;
    SEARCH_MATCH  SearchMatch  = {0};

    if (NULL == pSearchInfo)
        return ERR_MISC_INVALID_ARGS;

    memcpy((char*)(&SearchMatch), (char *)pSearchInfo, sizeof (SEARCH_MATCH));

    if (!(SearchMatch.SERetFlags & SE_FMATCH)) return(NOERROR);

    if (phNoteIDTable)
    {
        hNoteIDTable = *((DHANDLE far *)phNoteIDTable);
        if (hNoteIDTable)
        {
            error = IDInsert(hNoteIDTable, SearchMatch.ID.NoteID, &bFlagOK);
        }
    }

    return ERR(error);
}


DWORD GetDocsByFormula (DBHANDLE hDb, char *pszFormulaText, char *pszLookupDisplay, DHANDLE *retphNoteIDTable)
{
    STATUS error = NOERROR;
    WORD   wdc         = 0;
    WORD   wFormulaLen = 0;
    DWORD  dwEntries   = 0;

    FORMULAHANDLE hFormula = NULLHANDLE;

    if (NULLHANDLE == *retphNoteIDTable)
    {
        error = IDCreateTable(sizeof (NOTEID), retphNoteIDTable);

        if (error)
        {
            goto Done;
        }
    }

    error = NSFFormulaCompile( NULL,
                               0,
                               pszFormulaText,
                               (WORD) strlen (pszFormulaText),
                               &hFormula,
                               &wFormulaLen,
                               &wdc,
                               &wdc,
                               &wdc,
                               &wdc,
                               &wdc);

    if (error)
    {
        AddInLogMessageText ("%s: Error compiling search formula", error, g_szTask);
        hFormula = NULLHANDLE;
        goto Done;
    }

    error = NSFSearch(hDb,                /* database handle */
                      hFormula,           /* selection formula */
                      NULL,               /* title of view in selection formula */
                      0,                  /* search flags */
                      NOTE_CLASS_DOCUMENT,/* note class to find */
                      NULL,               /* starting date(unused) */
                      AddIDUnique,        /* call for each note found */
                      retphNoteIDTable,   /* argument to AddIDUnique */
                      NULL);              /* returned ending date(unused) */

    if (error)
    {
        goto Done;
    }

    dwEntries = IDEntries (*retphNoteIDTable);

Done:

    if (hFormula)
    {
        OSMemFree(hFormula);
        hFormula = NULLHANDLE;
    }

    return dwEntries;
}



void SanitizeHelpString(char *pszText)
{
    char *pchRead  = NULL;
    char *pchWrite = NULL;
    unsigned char ch = 0;
    bool fLastWasSpace = true;   // treat start as "space" to trim leading spaces

    if (!pszText)
        return;

    pchRead  = pszText;
    pchWrite = pszText;

    for (; *pchRead; ++pchRead)
    {
        ch = (unsigned char)*pchRead;

        // Skip all control characters
        if (ch < 32)
            continue;

        if (ch == ' ')
        {
            if (fLastWasSpace)
                continue;  // collapse multiple / leading spaces

            fLastWasSpace = true;
            *pchWrite++ = ' ';
            continue;
        }

        *pchWrite++ = *pchRead;
        fLastWasSpace = false;
    }

    // Trim trailing space
    if (pchWrite > pszText && pchWrite[-1] == ' ')
        --pchWrite;

    *pchWrite = '\0';
}


STATUS AddStatDescriptionToTable (const char *pszStatName, char *pszDescription)
{
    STATUS error = NOERROR;
    char szStatsNameLower[MAX_STAT_NAME+1] = {0};

    if (IsNullStr (pszStatName))
        return ERR_MISC_INVALID_ARGS;

    if (IsNullStr (pszDescription))
        return ERR_MISC_INVALID_ARGS;

    OSTranslate32 (OS_TRANSLATE_UPPER_TO_LOWER, pszStatName, MAXDWORD, szStatsNameLower, sizeof (szStatsNameLower));

    SanitizeHelpString (pszDescription);

    g_DominoStatDescription.emplace (std::string (szStatsNameLower), std::string (pszDescription));

    return error;
}


const char *GetStatDescriptionFromTable (const char *pszStatNameLower)
{
    const char* pszDesc = NULL;

    if (IsNullStr (pszStatNameLower))
        return NULL;

    auto it = g_DominoStatDescription.find (pszStatNameLower);
    if (it != g_DominoStatDescription.end())
    {
        pszDesc = it->second.c_str();
    }

    return pszDesc;
}


STATUS ProcessStatDocument (DBHANDLE hDb, NOTEID NoteID)
{
    STATUS error = NOERROR;
    NOTEHANDLE hNote = NULLHANDLE;

    char szStatName[MAX_STAT_NAME+1]    = {0};
    char szUnits[40]                    = {0};
    char szBuffer[MAX_STAT_DESC*2]      = {0};
    char szDescription[MAX_STAT_DESC+1] = {0};

    if (NULLHANDLE == hDb)
        return ERR_MISC_INVALID_ARGS;

    if (0 == NoteID)
        return ERR_MISC_INVALID_ARGS;

    error = NSFNoteOpen (hDb, NoteID, 0, &hNote);

    if (error)
    {
        AddInLogMessageText ("%s: Cannot open Statistics note 0x%x in %s", error, g_szTask, NoteID, g_szEvents4);
        goto Done;
    }

    NSFItemGetText (hNote, "StatName",    szStatName,    sizeofstring (szStatName));
    NSFItemGetText (hNote, "Description", szDescription, sizeofstring (szDescription));
    NSFItemGetText (hNote, "Units",       szUnits,       sizeofstring (szUnits));

    if (*szUnits)
    {
        snprintf (szBuffer, sizeof (szBuffer), "%s [%s]", szDescription, szUnits);
        AddStatDescriptionToTable (szStatName, szBuffer);
    }
    else
    {
        AddStatDescriptionToTable (szStatName, szDescription);
    }

Done:

    if (hNote)
    {
        NSFNoteClose (hNote);
        hNote = NULLHANDLE;
    }

    return error;

}


STATUS ReadStatisticsInfoFromEvents4()
{
    STATUS   error        = NOERROR;
    DBHANDLE hDb          = NULLHANDLE;
    DHANDLE  hNoteIDTable = NULLHANDLE;

    NOTEID   NoteID    = 0;
    DWORD    dwEntries = 0;
    BOOL     bFound    = FALSE;

    error = NSFDbOpen (g_szEvents4, &hDb);

    if (error)
    {
        AddInLogMessageText ("%s: Cannot open %s", error, g_szTask, g_szEvents4);
        goto Done;
    }

    dwEntries = GetDocsByFormula (hDb, "+(Form = {Statistic})", "Search statistics descriptions in events4.nsf", &hNoteIDTable);

    if (g_wLogLevel)
        AddInLogMessageText ("%s: Statistics descriptions found in %s: %u", 0, g_szTask, g_szEvents4, dwEntries);

    if (0 == dwEntries)
    {
        goto Done;
    }

    bFound = IDScan (hNoteIDTable, TRUE, &NoteID);

    while (bFound)
    {
        error = ProcessStatDocument (hDb, NoteID);
        bFound = IDScan (hNoteIDTable, FALSE, &NoteID);
    }

Done:

    if (hNoteIDTable)
    {
        IDDestroyTable(hNoteIDTable);
        hNoteIDTable = NULLHANDLE;
    }

    if (hDb)
    {
        NSFDbClose (hDb);
        hDb = NULLHANDLE;
    }

    return error;
}



STATUS LNCALLBACK DomExportTraverse (void *pContext, char *pszFacility, char *pszStatName, WORD wValueType, void *pValue)
{
    STATUS error          = NOERROR;
    WORD   wLen           = 0;
    NFMT   NumberFormat   = {0};

    char   szDescription[MAX_STAT_DESC+1] = {0};
    char   szMetric[1024]      = {0};
    char   szMetricLower[1024] = {0};
    char   szValue[1024]       = {0};

    const char *pszDescription = NULL;

    CONTEXT_STRUCT_TYPE *pStats = (CONTEXT_STRUCT_TYPE*)pContext;

    if (NULL == pStats)
        return ERR_MISC_INVALID_ARGS;

    if (NULL == pStats->fp)
        return ERR_MISC_INVALID_ARGS;

    pStats->CountAll++;

    if (NULL == pszFacility)
    {
        pStats->CountInvalid++;
        return ERR_MISC_INVALID_ARGS;
    }

    if (NULL == pszStatName)
    {
        pStats->CountInvalid++;
        return ERR_MISC_INVALID_ARGS;
    }

    if (NULL == pValue)
        return NOERROR;

    /* Exclude Domino Health stats because they are maintained in this application */
    if (g_wWriteDominoHealthStats)
    {
        if (0 == CompareCaseInsensitive (pszFacility, g_szDominoHealth))
        {
            return NOERROR;
        }
    }

    NumberFormat.Digits = 2;
    NumberFormat.Format = NFMT_GENERAL;

    snprintf (szMetric, sizeof (szMetric), "%s.%s", pszFacility, pszStatName);

    OSTranslate32 (OS_TRANSLATE_UPPER_TO_LOWER, szMetric, MAXDWORD, szMetricLower, sizeof (szMetricLower));

    if (g_StatsFilter.ShouldExclude (szMetricLower))
    {
        return NOERROR;
    }

    /* Compare if the statistic case insensitive was written before and log case sensitive */
    if (RegisterDominoStat (szMetricLower, 0))
    {
        if (g_wLogLevel)
        {
            AddInLogMessageText ("%s: Duplicate Domino statistic found for: %s", 0, g_szTask, szMetric);
        }
        return NOERROR;
    }

    /* Use the combined and converted metric for statistic name conversion */
    ReplaceChars (szMetric);

    pszDescription = GetStatDescriptionFromTable (szMetricLower);

    if (pszDescription)
        snprintf (szDescription, sizeof (szDescription), "%s", pszDescription);
    else
        snprintf (szDescription, sizeof (szDescription), "Domino Stat - %s.%s", pszFacility, pszStatName);

    switch (wValueType)
    {
        case VT_TEXT:

            pStats->CountText++;

            if (0 == CompareCaseInsensitive (szMetric, "DAOS_Engine_Status"))
            {
                UpdateStatusDAOS ((const char *)pValue);
            }
            else if (0 == CompareCaseInsensitive (szMetric, "DAOS_Engine_Catalog"))
            {
                UpdateCatalogDAOS ((const char *)pValue);
            }
            else if (0 == CompareCaseInsensitive (szMetric, "DominoBackup_LastBackup_DB_Status"))
            {
                WriteStatsEntryToFile (pStats->fp, g_szDominoHealth, "LastBackup_DB_Status", szDescription, CompareCaseInsensitive ((const char *)pValue, "Successful") ? "1" : "0");
            }
            else if (0 == CompareCaseInsensitive (szMetric, "DominoBackup_LastBackup_TL_Status"))
            {
                WriteStatsEntryToFile (pStats->fp, g_szDominoHealth, "LastBackup_TL_Status", szDescription, CompareCaseInsensitive ((const char *)pValue, "Successful") ? "1" : "0");
            }
            else if (0 == CompareCaseInsensitive (szMetric, "DominoBackup_LastBackup_TL_LastLogExtend"))
            {
                WriteStatsEntryToFile (pStats->fp, g_szDominoHealth, "LastBackup_TL_LastLogExtendNumber", szDescription, CompareCaseInsensitive ((const char *)pValue, "Successful") ? "1" : "0");
            }

            if (pStats->bExportText)
            {
                snprintf (szValue, sizeof (szValue), "%s", (char *)pValue);

                TruncateAtFirstBlank (szValue);
                WriteStatsEntryToFile (pStats->fp, pStats->szPrefix, szMetric, szDescription, szValue);
            }
            break;

        case VT_LONG:

            pStats->CountLong++;

            if (pStats->bExportLong)
            {
                WriteStatsEntryToFile (pStats->fp, pStats->szPrefix, szMetric, szDescription, *(LONG *) pValue);
            }
            break;

        case VT_NUMBER:

            pStats->CountNumber++;

            if (pStats->bExportLong)
            {
                error = ConvertFLOATToText (&(pStats->Intl), &NumberFormat, (NUMBER *)pValue, szValue, sizeof (szValue)-1, &wLen);
                if (error)
                {
                    pStats->CountInvalid++;
                }
                else
                {
                    szValue[wLen] = '\0';
                    WriteStatsEntryToFile (pStats->fp, pStats->szPrefix, szMetric, szDescription, szValue);
                }
            }
            break;

        case VT_TIMEDATE:

            pStats->CountTime++;

            if (0 == CompareCaseInsensitive (szMetric, "DominoBackup_LastBackup_DB_Time"))
            {
                WriteTimedateStat (pStats->fp, "LastBackup_DB_Time", szDescription, pValue);
            }

            else if (0 == CompareCaseInsensitive (szMetric, "DominoBackup_LastBackup_TL_Time"))
            {
                WriteTimedateStat (pStats->fp, "LastBackup_TL_Time", szDescription, pValue);
            }

            else if (0 == CompareCaseInsensitive (szMetric, "Server_Time_Start"))
            {
                WriteTimedateStat (pStats->fp, "Server_Time_Start", szDescription, pValue);
            }

            if (pStats->bExportTime)
            {
                if (GetNotesTimeDateSting ((TIMEDATE *)pValue, sizeof (szValue), szValue))
                {
                    pStats->CountInvalid++;
                }
                else
                {
                    WriteStatsEntryToFile (pStats->fp, pStats->szPrefix, szMetric, szDescription, szValue);
                }
            }
            break;

        default:

            pStats->CountUnknown++;
            break;

    } /* switch */

    return error;
}


void AddDiskStats (const char *pszHealthPrefix, const char *pszComponent, const char *pszPathName, uint64_t TotalBytes, uint64_t FreeBytes)
{
    char szBuffer[1024] = {0};

    // total bytes
    snprintf(
        szBuffer,
        sizeof (szBuffer),
        "%s_disk_total_bytes{component=\"%s\", path=\"%s\"} %" PRIu64,
        pszHealthPrefix,
        pszComponent,
        pszPathName,
        TotalBytes);

    g_ListDiskTotalStats.emplace_back (szBuffer);

    // free bytes
    snprintf(
        szBuffer,
        sizeof(szBuffer),
        "%s_disk_free_bytes{component=\"%s\", path=\"%s\"} %" PRIu64,
        pszHealthPrefix,
        pszComponent,
        pszPathName,
        FreeBytes);

    g_ListDiskFreeStats.emplace_back (szBuffer);
}

STATUS ProcessSingleDiskStat (const char *pszPathName, const char *pszComponent)
{
    bool     bFound     = false;
    uint64_t TotalBytes = 0;
    uint64_t FreeBytes  = 0;
    char     szMetric[1024] = {0};

    if (IsNullStr (pszPathName))
        return ERR_MISC_INVALID_ARGS;

    if (IsNullStr (pszComponent))
        return ERR_MISC_INVALID_ARGS;

    bFound = GetDiskUsageFrom (pszPathName, &TotalBytes, &FreeBytes);

    if (g_wWriteDominoHealthStats)
    {
        snprintf (szMetric, sizeof (szMetric), "Disk.%s.Total_MB", pszComponent);
        StatUpdateNumberBytesInMB (g_szDominoHealth, szMetric, TotalBytes);

        snprintf (szMetric, sizeof (szMetric), "Disk.%s.Free_MB", pszComponent);
        StatUpdateNumberBytesInMB (g_szDominoHealth, szMetric, FreeBytes);

        snprintf (szMetric, sizeof (szMetric), "Disk.%s.Path", pszComponent);
        StatUpdateText (g_szDominoHealth, szMetric, pszPathName);
    }

    if (false == bFound)
    {
        return NOERROR;
    }

    AddDiskStats (g_szDominoHealth, pszComponent, pszPathName, TotalBytes, FreeBytes);

    return NOERROR;
}


STATUS ProcessDiskStats (FILE *fp)
{
    if (NULL == fp)
        return ERR_MISC_INVALID_ARGS;

    ProcessSingleDiskStat (g_szDataDir,     DOMPROM_DISK_COMPONENT_NOTESDATA);
    ProcessSingleDiskStat (g_szDirTranslog, DOMPROM_DISK_COMPONENT_TRANSLOG);
    ProcessSingleDiskStat (g_szDirDAOS,     DOMPROM_DISK_COMPONENT_DAOS);
    ProcessSingleDiskStat (g_szDirNIF,      DOMPROM_DISK_COMPONENT_NIF);
    ProcessSingleDiskStat (g_szDirFT,       DOMPROM_DISK_COMPONENT_FT);
    ProcessSingleDiskStat (g_szNotesTemp,   DOMPROM_DISK_COMPONENT_NOTES_TEMP);
    ProcessSingleDiskStat (g_szViewRebuild, DOMPROM_DISK_COMPONENT_VIEW_REBUILD);
    ProcessSingleDiskStat (g_szNotesLogDir, DOMPROM_DISK_COMPONENT_NOTES_LOG_DIR);

    WriteHelpAndType (fp, g_szDominoHealth, "disk_total_bytes", NULL, "Total disk size in bytes");

    for (const auto &pszLine : g_ListDiskTotalStats)
    {
        fprintf (fp, "%s\n", pszLine.c_str());
    }

    g_ListDiskTotalStats.clear();

    WriteHelpAndType (fp, g_szDominoHealth, "disk_free_bytes", NULL, "Free disk space in bytes");

    for (const auto &pszLine : g_ListDiskFreeStats)
    {
        fprintf (fp, "%s\n", pszLine.c_str());
    }

    g_ListDiskFreeStats.clear();

    return NOERROR;
}

STATUS ProcessDaosStats (FILE *fp)
{
    int CatalogStatus = 1;

    if (NULL == fp)
        return ERR_MISC_INVALID_ARGS;

    if (0 == g_StatusDAOS)
        CatalogStatus = -1;
    else if (g_CatalogInSyncDAOS)
        CatalogStatus = 0;
    else
        CatalogStatus = 1;

    if (g_wWriteDominoHealthStats)
    {
        StatUpdateNumber (g_szDominoHealth, "DAOS.Status", g_StatusDAOS);
        StatUpdateNumber (g_szDominoHealth, "DAOS.Catalog.Status", CatalogStatus);
    }

    WriteStatsEntryToFile (fp, g_szDominoHealth, "daos_status",         "Domino DAOS enabled", g_StatusDAOS);
    WriteStatsEntryToFile (fp, g_szDominoHealth, "daos_catalog_status", "Domino DAOS Catalog status (0 = in sync)", g_CatalogInSyncDAOS);

    return NOERROR;
}


int HasFileExtension (const char *pszFilename, const char *pszExt)
{
    size_t len_f = strlen (pszFilename);
    size_t len_e = strlen (pszExt);

    if (len_f < len_e)
        return 0;

    return strcmp (pszFilename + len_f - len_e, pszExt) == 0;
}


STATUS CheckTransLogExtend (const char *pszTranslogExtend)
{
    size_t num = 0;

    if (NULL == pszTranslogExtend)
        return ERR_MISC_INVALID_ARGS;

    num = GetTranslogExtendNumber (pszTranslogExtend);

    if (0 == g_TranslogMinLogExtend)
    {
        g_TranslogMinLogExtend = num;
    }
    else
    {
        if (num < g_TranslogMinLogExtend)
            g_TranslogMinLogExtend = num;
    }

    if (0 == g_TranslogMaxLogExtend)
    {
        g_TranslogMaxLogExtend = num;
    }
    else
    {
        if (num > g_TranslogMaxLogExtend)
            g_TranslogMaxLogExtend = num;
    }

    return NOERROR;
}


#ifdef _WIN32

size_t CountFilesWithExtension (const char *pszPath, const char *pszExt)
{
    char szPattern[MAX_PATH] = {0};
    size_t count = 0;
    WIN32_FIND_DATAA FindData;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    snprintf (szPattern, sizeof (szPattern), "%s\\*%s", pszPath, pszExt);

    hFile = FindFirstFileA (szPattern, &FindData);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (!(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (HasFileExtension (FindData.cFileName, pszExt))
            {
                CheckTransLogExtend (FindData.cFileName);
                count++;
            }
        }
    } while (FindNextFileA(hFile, &FindData));

    FindClose(hFile);
    return count;
}

#else

size_t CountFilesWithExtension (const char *pszPath, const char *pszExt)
{
    struct dirent *pEntry = NULL;
    size_t count = 0;

    DIR *pDir = opendir (pszPath);

    if (NULL == pDir)
    {
        perror("opendir");
        goto Done;
    }

    while ((pEntry = readdir(pDir)) != NULL)
    {
        if (pEntry->d_type == DT_DIR)
            continue;

        if (HasFileExtension (pEntry->d_name, pszExt))
        {
            CheckTransLogExtend (pEntry->d_name);
            count++;
        }
    }

Done:

    if (pDir)
    {
        closedir (pDir);
        pDir = NULL;
    }

    return count;
}

#endif


STATUS ProcessTranslogStats (FILE *fp)
{
    size_t NumTranslogFiles = 0;

    g_TranslogMinLogExtend = 0;
    g_TranslogMaxLogExtend = 0;

    if (NULL == fp)
        return ERR_MISC_INVALID_ARGS;

    WriteStatsEntryToFile (fp, g_szDominoHealth, "translog_style", "Domino Statistic Database.RM.Sys.Log.Type", g_wTranslogLogType);

    if (g_wWriteDominoHealthStats)
    {
        StatUpdateNumber (g_szDominoHealth, "Translog.Style", g_wTranslogLogType);
    }

    if (g_wTranslogLogType != TRANSLOG_STYLE_ARCHIVE)
    {
        goto Done;
    }

    if (IsNullStr (g_szDirTranslog))
    {
        goto Done;
    }

    NumTranslogFiles = CountFilesWithExtension (g_szDirTranslog, ".TXN");

    if (g_wWriteDominoHealthStats)
    {
        StatUpdateNumber (g_szDominoHealth, "Translog.File.Count", NumTranslogFiles);
        StatUpdateNumber (g_szDominoHealth, "Translog.File.Min",   g_TranslogMinLogExtend);
        StatUpdateNumber (g_szDominoHealth, "Translog.File.Max",   g_TranslogMaxLogExtend);
    }

    WriteStatsEntryToFile (fp, g_szDominoHealth, "translog_file_count", "Number of current Transaction Log Extends in Translog directory", NumTranslogFiles);

    if (0 == NumTranslogFiles)
    {
        goto Done;
    }

    WriteStatsEntryToFile (fp, g_szDominoHealth, "translog_file_min", "Lowest Transaction Log Extend Number in Translog directory", g_TranslogMinLogExtend);
    WriteStatsEntryToFile (fp, g_szDominoHealth, "translog_file_max", "Highest Transaction Log Extend Number in Translog directory", g_TranslogMaxLogExtend);

Done:

    return NOERROR;
}


bool ParseSkipToNextLine (const char **ppsz)
{
    const char *psz = NULL;

    if (NULL == ppsz)
        return false;

    psz = *ppsz;

    if (NULL == psz)
        return false;

    if ('\0' == *psz)
        return false;

    /* find line break */
    while (*psz)
    {
        if (*psz < 32)
            break;
        psz++;
    }

    /* skip over line breaks */
    while (*psz)
    {
        if (*psz >= 32)
            break;
        psz++;
    }

    *ppsz = psz;
    return true;
}


bool ParseGetString (const char **ppsz, char *pszOut, size_t szOut)
{
    const char *psz = NULL;
    const char *pszEnd = NULL;

    if (NULL == ppsz)
        return false;

    psz = *ppsz;

    if (NULL == psz)
        return false;

    if ('\0' == *psz)
        return false;

    while (*psz)
    {
        if (psz[0] == ' ' && psz[1] == ' ')
        {
            pszEnd = psz;
            break;
        }

        psz++;
    }

    if (!pszEnd)
        return false;

    size_t szLen = (size_t)(pszEnd - *ppsz);
    if (szLen >= szOut)
        szLen = szOut - 1;

    memcpy(pszOut, *ppsz, szLen);
    pszOut[szLen] = '\0';

    psz = pszEnd;
    while (*psz == ' ')
        psz++;

    *ppsz = psz;
    return true;
}

bool ParseGetNumber (const char **ppsz, int *piOut)
{
    const char *psz = NULL;
    char *pszEnd = NULL;

    if (NULL == ppsz)
        return false;

    psz = *ppsz;

    if (NULL == psz)
        return false;

    if ('\0' == *psz)
        return false;

    while (*psz == ' ')
        psz++;

    if (!isdigit((unsigned char)*psz) && *psz != '-')
    {
        return false;
    }

    *piOut = (int)strtol(psz, &pszEnd, 10);
    *ppsz = pszEnd;
    return true;
}


/* ---------- data ---------- */

typedef struct _STAT_ROW {
    char szName[64];
    int  iCount;
    int  iMin;
    int  iMax;
    int  iTotal;
    int  iAverage;
} STAT_ROW;

/* ---------- parser ---------- */

bool ParseOneTransStatsRow (const char **ppsz, STAT_ROW *pRow)
{
    if (false == ParseGetString (ppsz, pRow->szName, sizeof(pRow->szName)))
        return false;

     if (false == ParseGetNumber (ppsz, &pRow->iCount))
         return false;

     if (false == ParseGetNumber (ppsz, &pRow->iMin))
         return false;

     if (false == ParseGetNumber (ppsz, &pRow->iMax))
         return false;

     if (false == ParseGetNumber (ppsz, &pRow->iTotal))
         return false;

     if (false == ParseGetNumber (ppsz, &pRow->iAverage))
         return false;

     ParseSkipToNextLine (ppsz);
     return true;
}


static bool AddUnique (std::list<std::string> &list, const char *pszValue)
{
    for (const auto &entry : list)
    {
        if (entry == pszValue)
        {
            return false; // already present
        }
    }

    list.emplace_back (pszValue);

    return true;
}


bool AddTransactionStats (const char *pszMetricPrefix, const char *pszOp, int Count, int TotalMs)
{
    char szBuffer[1024] = {0};

    if (IsNullStr (pszMetricPrefix))
        return false;

    if (IsNullStr (pszOp))
        return false;

    // count
    snprintf(
        szBuffer,
        sizeof(szBuffer),
        "%s_count{op=\"%s\"} %d",
        pszMetricPrefix,
        pszOp,
        Count);

    AddUnique (g_ListTransCountStats, szBuffer);

    // total_ms
    snprintf(
        szBuffer,
        sizeof(szBuffer),
        "%s_total_ms{op=\"%s\"} %d",
        pszMetricPrefix,
        pszOp,
        TotalMs);

    AddUnique (g_ListTransTotalMsStats, szBuffer);

    return true;
}


void PrintAndClearTransStats (FILE *fp)
{
    if (NULL == fp)
        return;

    WriteHelpAndType (fp, g_szDominoTrans, "count", "counter", "Transaction count");

    for (const auto &pszLine : g_ListTransCountStats)
    {
        fprintf(fp, "%s\n", pszLine.c_str());
    }

    WriteHelpAndType (fp, g_szDominoTrans, "total_ms", "counter", "Total transaction time in milliseconds");

    for (const auto &pszLine : g_ListTransTotalMsStats)
    {
        fprintf(fp, "%s\n", pszLine.c_str());
    }

    g_ListTransCountStats.clear();
    g_ListTransTotalMsStats.clear();
}


DWORD ParseTransStatsBuffer (const char *pszBuffer)
{
    DWORD dwStatsCount = 0;
    const char *pszLine = NULL;
    STAT_ROW stRow = {0};

    if (IsNullStr (pszBuffer))
        return 0;

    /* find header line */
    pszLine = strstr (pszBuffer, "Function");

    if (NULL == pszLine)
        return 0;

    /* skip to next line */
    if (false == ParseSkipToNextLine (&pszLine))
        goto Done;

    /* skip another empty line */
    if (false == ParseSkipToNextLine (&pszLine))
        goto Done;

    while (ParseOneTransStatsRow (&pszLine, &stRow))
    {
        /* Replace blanks and other invalid chars for transaction names */
        ReplaceChars (stRow.szName);

        if (AddTransactionStats (g_szDominoTrans, stRow.szName, stRow.iCount, stRow.iTotal))
        {
            dwStatsCount++;
        }
    }

Done:

    return dwStatsCount;
}


STATUS ProcessIOStat ( DWORD dwIntervalSeconds)
{
    STATUS   error = NOERROR;
    TIMEDATE tNow  = {0};

    OSCurrentTIMEDATE (&tNow);

    if (TimeDateCompare (&tNow, &g_tNextIOStatUpdate) < 0)
    {
        return NOERROR;
    }

    OSCurrentTIMEDATE (&g_tNextIOStatUpdate);
    TimeDateAdjust(&g_tNextIOStatUpdate, dwIntervalSeconds, 0, 0, 0, 0, 0);

    error = NSFRemoteConsole (g_szLocalUser, "!show iostat", NULL);

    if (error)
    {
        AddInLogMessageText ("%s: Remote console command failed: show iostat", error, g_szTask);
        goto Done;
    }

Done:

    return error;
}


STATUS ProcessTransStats (const char *pszFilename, DWORD dwIntervalSeconds)
{
    STATUS  error        = NOERROR;
    DHANDLE hRetInfo     = NULLHANDLE;
    BYTE    *pInfoBuffer = NULL;
    FILE    *fp          = NULL;
    char    szTempFilename[MAXPATH+1] = {0};

    DWORD dwStatsCount = 0;

    TIMEDATE tNow = {0};

    OSCurrentTIMEDATE (&tNow);

    uint64_t EpochSec = (uint64_t) time (NULL);

    if (IsNullStr (pszFilename))
    {
        return ERR_MISC_INVALID_ARGS;
    }

    if (TimeDateCompare (&tNow, &g_tNextTransStatsUpdate) < 0)
    {
        return NOERROR;
    }

    OSCurrentTIMEDATE (&g_tNextTransStatsUpdate);
    TimeDateAdjust(&g_tNextTransStatsUpdate, dwIntervalSeconds, 0, 0, 0, 0, 0);

    error = NSFRemoteConsole (g_szLocalUser, "!show trans", &hRetInfo);

    if (error)
    {
        AddInLogMessageText ("%s: Remote console command failed: show trans", error, g_szTask);
        goto Done;
    }

    if (NULLHANDLE == hRetInfo)
    {
        error = ERR_MEMORY;
        goto Done;
    }

    pInfoBuffer = OSLock (BYTE, hRetInfo);

    if (NULL == pInfoBuffer)
    {
        error = ERR_MEMORY;
        goto Done;
    }

    snprintf (szTempFilename, sizeof (szTempFilename),  "%s.tmp", pszFilename);

    fp = fopen (szTempFilename, "w");

    if (NULL == fp)
    {
        AddInLogMessageText ("%s: Cannot create transaction file: %s", 0, g_szTask, pszFilename);
        perror ("Cannot create stats temp file");
        goto Done;
    }

    dwStatsCount = ParseTransStatsBuffer ((const char *) pInfoBuffer);

    if (dwStatsCount)
    {
        PrintAndClearTransStats (fp);
    }

    WriteStatsEntryToFile (fp, g_szDominoTrans, "stat_transactions_update_timestamp", "Domino Transactions last update epoch time", EpochSec);

Done:

    if (pInfoBuffer)
    {
        OSUnlock (hRetInfo);
        pInfoBuffer = NULL;
    }

    if (hRetInfo)
    {
        OSMemFree (hRetInfo);
        hRetInfo = NULLHANDLE;
    }

    if (fp)
    {
        fclose (fp);
        fp = NULL;

        rename (szTempFilename, pszFilename);
    }

    return NOERROR;
}


WORD IsInMaintenanceMode()
{
    TIMEDATE tNow = {0};

    if (g_wMaintenanceEnabled)
        return 1;

    if (false == g_bMaintenanceStartSet)
        return 0;

    OSCurrentTIMEDATE (&tNow);

    if (TimeDateCompare (&tNow, &g_tMaintenanceStart) < 0)
        return 0;

    if (false == g_bMaintenanceEndSet)
        return 3;

    if (TimeDateCompare (&tNow, &g_tMaintenanceEnd) > 0)
        return 0;

    return 3;
}


void WriteExporterCommonStats (FILE *fp)
{
    char szTmp[MAXSPRINTF+1] = {0};
    uint64_t EpochSec = (uint64_t) time (NULL);

    if (NULL == fp)
        return;

    snprintf (szTmp, sizeof (szTmp), "Domino Prometheus Exporter build version %s", g_szVersion);
    WriteStatsEntryToFile (fp, g_szDominoHealth, "Exporter_Build", szTmp, DOMPROM_VERSION_BUILD);

    WriteStatsEntryToFile (fp, g_szDominoHealth, "stat_update_timestamp", "Domino Statistic last update epoch time", EpochSec);

    if (g_bMaintenanceStartSet)
    {
        AddInFormatErrorText(szTmp, "Start of maintenance window in epoch time (%z)", &g_tMaintenanceStart);
        WriteTimedateStat (fp, "maintenance_start_timestamp", szTmp, &g_tMaintenanceStart);
    }

    if (g_bMaintenanceEndSet)
    {
        AddInFormatErrorText(szTmp, "End of maintenance window in epoch time (%z)", &g_tMaintenanceEnd);
        WriteTimedateStat (fp, "maintenance_end_timestamp", szTmp, &g_tMaintenanceEnd);
    }

    WriteStatsEntryToFile (fp, g_szDominoHealth, "maintenance_status", "Domino maintenance status", IsInMaintenanceMode());
    WriteStatsEntryToFile (fp, g_szDominoHealth, "server_restricted_status", "Domino server restricted status", g_wServerRestricted);
}


STATUS ProcessDominoStatistics (const char *pszFilename, bool bWriteShutdownStats = false)
{
    STATUS   error = NOERROR;
    char     szTempFilename[MAXPATH+1] = {0};

    CONTEXT_STRUCT_TYPE Stats = {0};

    if (NULL == pszFilename)
        return ERR_MISC_INVALID_ARGS;

    if (0 == OSGetEnvironmentLong (ENV_DOMPROM_NO_PREFIX))
        snprintf (Stats.szPrefix, sizeof (Stats.szPrefix), "Domino");

    /* Define which stat types to export (Only numeric values make sense for Prometheus/Grafana) */
    Stats.bExportLong   = TRUE;
    Stats.bExportNumber = TRUE;

    snprintf (szTempFilename, sizeof (szTempFilename),  "%s.tmp", pszFilename);

    Stats.fp = fopen (szTempFilename, "w");

    if (NULL == Stats.fp)
    {
        perror ("Cannot create stats temp file");
        goto Done;
    }

    OSGetIntlSettings (&(Stats.Intl), sizeof (Stats.Intl));
    snprintf (Stats.Intl.DecimalString,  sizeof (Stats.Intl.DecimalString), ".");
    snprintf (Stats.Intl.ThousandString, sizeof (Stats.Intl.ThousandString), ",");

    WriteExporterCommonStats (Stats.fp);

    if (g_wWriteDominoHealthStats)
    {
        StatUpdateText (g_szDominoHealth, "Intl.DecimalString", Stats.Intl.DecimalString);
        StatUpdateText (g_szDominoHealth, "Intl.ThousandString", Stats.Intl.ThousandString);
    }

    if (bWriteShutdownStats)
    {
        TIMEDATE tNow  = {0};
        OSCurrentTIMEDATE (&tNow);
        WriteTimedateStat (Stats.fp, "stat_shutdown_timestamp", "Domino statistic shutdown epoch time", &tNow);
        goto Done;
    }

    /* Reset Domino statistics buffer for making sure we don't get a stat more than once */
    BeginDominoStatCollection();

    StatTraverse (NULL, NULL, DomExportTraverse, &Stats);

    if (g_wLogLevel)
    {
        if (Stats.CountInvalid || Stats.CountUnknown)
        {
            AddInLogMessageText ("%s: Text: %lu, Long: %lu, Number: %lu, Time: %lu, Invalid: %lu, Unknown: %lu, All: %lu",
                                  0,
                                  g_szTask,
                                  Stats.CountText,
                                  Stats.CountLong,
                                  Stats.CountNumber,
                                  Stats.CountTime,
                                  Stats.CountInvalid,
                                  Stats.CountUnknown,
                                  Stats.CountAll);
        }
        else
        {
            AddInLogMessageText ("%s: Text: %lu, Long: %lu, Number: %lu, Time: %lu, All: %lu",
                                  0,
                                  g_szTask,
                                  Stats.CountText,
                                  Stats.CountLong,
                                  Stats.CountNumber,
                                  Stats.CountTime,
                                  Stats.CountAll);
        }
    }

    ProcessDaosStats     (Stats.fp);
    ProcessTranslogStats (Stats.fp);
    ProcessDiskStats     (Stats.fp);

Done:

    if (Stats.fp)
    {
        fclose (Stats.fp);
        Stats.fp = NULL;

        rename (szTempFilename, pszFilename);
    }

    return error;
}


BOOL GetEnvironmentVars (BOOL bFirstTime)
{
    static WORD SeqNo  = 0;
    WORD   wTempSeqNo  = 0;
    DWORD  dwInterval  = 0;
    WORD   wValue      = 0;
    BOOL   bUpdated    = FALSE; /* Return true if config got updated and set status in this case */
    wTempSeqNo = OSGetEnvironmentSeqNo();

    if (FALSE == bFirstTime)
        if (wTempSeqNo == SeqNo)
            return FALSE;

    SeqNo = wTempSeqNo;

    wValue = (WORD) OSGetEnvironmentLong (ENV_DOMPROM_LOGLEVEL);

    if (g_wLogLevel != wValue)
    {
        g_wLogLevel = wValue;
    }

    /* Check for enabled config */

    if (g_wCollectDominoTransStats < MAX_CONFIG_VALUE_OVERRIDE)
    {
        wValue = (WORD) OSGetEnvironmentLong (ENV_DOMPROM_COLLECT_TRANS);

        if (g_wCollectDominoTransStats != wValue)
        {
            AddInLogMessageText ("%s: Domino transactions statistics collection: %s", 0, g_szTask, wValue ? "enabled":"disabled");
            g_wCollectDominoTransStats = wValue;
            bUpdated = TRUE;

            if (0 == wValue)
            {
                RemoveFile (g_szTransFilename, 1);
            }
        }
    }

    if (g_wCollectDominoIOStat < MAX_CONFIG_VALUE_OVERRIDE)
    {
        wValue = (WORD) OSGetEnvironmentLong (ENV_DOMPROM_COLLECT_IOSTAT);

        if (g_wCollectDominoIOStat != wValue)
        {
            AddInLogMessageText ("%s: Domino I/O statistics collection: %s", 0, g_szTask, wValue ? "enabled":"disabled");
            g_wCollectDominoIOStat = wValue;
            bUpdated = TRUE;
        }
    }

    /* Check if intervals changed, check against boundaries, set and report changes */

    dwInterval = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_INTERVAL);

    if (0 == dwInterval)
        dwInterval = DOMPROM_DEFAULT_INTERVAL_SEC;

    if (dwInterval < DOMPROM_MINIMUM_INTERVAL_SEC)
        dwInterval = DOMPROM_MINIMUM_INTERVAL_SEC;

    if (g_dwIntervalSec != dwInterval)
    {
        if (false == bFirstTime)
        {
            AddInLogMessageText ("%s: Changed %s from %u to %u", 0, g_szTask, ENV_DOMPROM_INTERVAL, g_dwIntervalSec, dwInterval);
        }

        g_dwIntervalSec = dwInterval;
        bUpdated = TRUE;
    }

    dwInterval = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_INTERVAL_TRANS);

    if (0 == dwInterval)
    {
        dwInterval = DOMPROM_DEFAULT_TRANS_INTERVAL_SEC;
    }

    if (dwInterval < DOMPROM_MINIMUM_TRANS_INTERVAL_SEC)
        dwInterval = DOMPROM_MINIMUM_TRANS_INTERVAL_SEC;

    if (g_dwTransIntervalSec != dwInterval)
    {
        if (false == bFirstTime)
        {
            AddInLogMessageText ("%s: Changed %s from %u to %u", 0, g_szTask, ENV_DOMPROM_INTERVAL_TRANS, g_dwTransIntervalSec, dwInterval);
        }

        g_dwTransIntervalSec = dwInterval;
        bUpdated = TRUE;
    }

    dwInterval = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_INTERVAL_IOSTAT);

    if (0 == dwInterval)
    {
        dwInterval = DOMPROM_DEFAULT_TRANS_INTERVAL_SEC;
    }

    if (dwInterval < DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC)
        dwInterval = DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC;

    if (g_dwIOStatIntervalSec != dwInterval)
    {
        if (false == bFirstTime)
        {
            AddInLogMessageText ("%s: Changed %s from %u to %u", 0, g_szTask, ENV_DOMPROM_INTERVAL_TRANS, g_dwIOStatIntervalSec, dwInterval);
        }

        g_dwIOStatIntervalSec = dwInterval;
        bUpdated = TRUE;
    }

    if (bUpdated)
    {
        /* Update idle message */
        UpdateIdleStatus();
    }

    g_bMaintenanceStartSet = OSGetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_START, &g_tMaintenanceStart);
    g_bMaintenanceEndSet   = OSGetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_END,   &g_tMaintenanceEnd);
    g_wServerRestricted    = (WORD) OSGetEnvironmentLong ("SERVER_RESTRICTED");

    return bUpdated;
}


bool IsAbsolutePath (const char *pszTarget)
{
    const char *t = pszTarget;

    if (IsNullStr (t))
        return false;

#ifdef _WIN32
#else
    /* On Unix it's simple */
    if(*t == '/')
        return true;

    return false;
#endif

  if(0 == isalpha(*t))
      return false;

  t++;
  if(*t != ':')
      return false;

  t++;
  if(*t == '\0')
      return false;

  if(*t == '/')
      return true;

  if(*t == '\\')
      return true;

  return false;
}


int ConvertToHumanReadableTime (double seconds, size_t MaxStrLen, char *retpszTime)
{
    if (seconds >= 86400.0)
    {
        return snprintf (retpszTime, MaxStrLen, "%.2f days", seconds / 86400.0);
    }
    else if (seconds >= 3600.0)
    {
        return snprintf (retpszTime, MaxStrLen, "%.2f hours", seconds / 3600.0);
    }
    else if (seconds >= 60.0)
    {
        return snprintf(retpszTime, MaxStrLen, "%.2f minutes", seconds / 60.0);
    }

    return snprintf (retpszTime, MaxStrLen, "%.2f seconds", seconds);
}


bool GetDiskPathFromNotesIni (const char *pszVariable, DWORD dwRetSize, char *retpszPath)
{
    char szEnvValue [MAXSPRINTF+1] = {0};

    if (retpszPath && dwRetSize)
        *retpszPath = '\0';
    else
        return false;

    if (IsNullStr (pszVariable))
        return false;

    if (FALSE == OSGetEnvironmentString (pszVariable, szEnvValue, sizeof (szEnvValue)-1))
    {
        return false;
    }

    if (IsAbsolutePath(szEnvValue))
        snprintf (retpszPath, dwRetSize, "%s", szEnvValue);
    else
        snprintf (retpszPath, dwRetSize, "%s%c%s", g_szDataDir, g_DirSep, szEnvValue);

    return true;
}


void GetDiskStatNamesFromNotesIni()
{
    GetDiskPathFromNotesIni ("TRANSLOG_Path",    sizeof (g_szDirTranslog), g_szDirTranslog);
    GetDiskPathFromNotesIni ("DAOSBasePath",     sizeof (g_szDirDAOS),     g_szDirDAOS);
    GetDiskPathFromNotesIni ("NIFBasePath",      sizeof (g_szDirNIF),      g_szDirNIF);
    GetDiskPathFromNotesIni ("FTBASEPATH",       sizeof (g_szDirFT),       g_szDirFT);
    GetDiskPathFromNotesIni ("notes_tempdir",    sizeof (g_szNotesTemp),   g_szNotesTemp);
    GetDiskPathFromNotesIni ("view_rebuild_dir", sizeof (g_szViewRebuild), g_szViewRebuild);
    GetDiskPathFromNotesIni ("logfile_dir",      sizeof (g_szNotesLogDir), g_szNotesLogDir);
}


void PrintHelp()
{
    AddInLogMessageText ("Domino Prometheus Exporter %s", 0, g_szVersion);
    AddInLogMessageText ("", 0);
    AddInLogMessageText ("Syntax: %s ", 0, g_szTask);
    AddInLogMessageText ("", 0);
    AddInLogMessageText ("Parameters", 0);
    AddInLogMessageText ("---------------------", 0);

    AddInLogMessageText ("-v        Verbose logging", 0);
    AddInLogMessageText ("-t        Write transactions via 'show trans'", 0);
    AddInLogMessageText ("-i        Collect Domino IOSTAT via 'show iostat'", 0);
    AddInLogMessageText ("-version  Print version", 0);
    AddInLogMessageText ("--version Print version Linux style", 0);

    AddInLogMessageText ("", 0);
    AddInLogMessageText ("Environment variables", 0);
    AddInLogMessageText ("---------------------", 0);

    AddInLogMessageText ("domprom_loglevel          Verbose logging", 0);
    AddInLogMessageText ("domprom_collect_trans     Enable collecting transactions via 'show trans' output", 0);
    AddInLogMessageText ("domprom_collect_iostat    Enable collecting Domino IOSTAT data via 'show iostat'", 0);
    AddInLogMessageText ("domprom_interval          Interval to collect stats in seconds (default: %u)", 0, DOMPROM_DEFAULT_INTERVAL_SEC);
    AddInLogMessageText ("domprom_interval_trans    Interval to collect transactions in seconds (default: %u)", 0, DOMPROM_DEFAULT_TRANS_INTERVAL_SEC);
    AddInLogMessageText ("domprom_interval_iostat   Interval to collect Domino IOSTAT data in seconds (default: %u)", 0, DOMPROM_DEFAULT_IOSTAT_INTERVAL_SEC);
    AddInLogMessageText ("domprom_outdir            Statistics directory for *.prom files (default: <notesdata>/domino/stats", 0);
    AddInLogMessageText ("domprom_outfile           Override Domino Stats file (default: %s)", 0, g_szDominoProm);
    AddInLogMessageText ("domprom_trans_outfile     Override Domino Transactions Stats file (default: %s)", 0, g_szDominoTransProm);
    AddInLogMessageText ("domprom_no_domino_prefix  Disable the new 'Domino_' prefix ", 0);
    AddInLogMessageText ("", 0);
}


void PrintConfig()
{
    char szBuffer[MAXSPRINTF+1] = {0};
    char szTime[40]      = {0};
    TIMEDATE tNow        = {0};
    LONG lSecondsStart   = 0;
    LONG lSecondsEnd     = 0;
    LONG lSeconds        = 0;
    BOOL bIsMaintanance  = IsInMaintenanceMode();

    OSCurrentTIMEDATE (&tNow);
    AddInLogMessageText ("", 0, szBuffer);

    snprintf (szBuffer, sizeof (szBuffer), "Collection  Interval :  %3u seconds)", g_dwIntervalSec);
    AddInLogMessageText ("%s", 0, szBuffer);

    if (g_wCollectDominoTransStats)
        snprintf (szBuffer, sizeof (szBuffer), "Transaction Interval :  %3u seconds)", g_dwTransIntervalSec);
    else
        snprintf (szBuffer, sizeof (szBuffer), "Transaction Interval :  -Disabled-)");
    AddInLogMessageText ("%s", 0, szBuffer);

    if (g_wCollectDominoIOStat)
        snprintf (szBuffer, sizeof (szBuffer), "I/O Stats   Interval :  %3u seconds)", g_dwIOStatIntervalSec);
    else
        snprintf (szBuffer, sizeof (szBuffer), "I/O Stats   Interval :  -Disabled-)");
    AddInLogMessageText ("%s", 0, szBuffer);

    AddInLogMessageText ("Statistics File      :  %s", 0, g_szStatsFilename);

    if (g_wCollectDominoTransStats)
        AddInLogMessageText ("Transactions File    :  %s", 0, g_szTransFilename);

    if (g_wMaintenanceEnabled)
    {
        AddInLogMessageText ("Maintenance mode     :  %s", 0, &g_tMaintenanceStart, "Until restart");
    }

    if (g_bMaintenanceStartSet)
    {
        lSecondsStart = TimeDateDifference (&g_tMaintenanceStart, &tNow);

        ConvertToHumanReadableTime (abs (lSecondsStart), sizeof (szTime), szTime);

        if (lSecondsStart > 0)
            snprintf (szBuffer, sizeof (szBuffer), "(will start in %s)", szTime);
        else if (bIsMaintanance)
            snprintf (szBuffer, sizeof (szBuffer), "(since %s)", szTime);
        else
            *szBuffer = '\0';

        AddInLogMessageText ("Maintenance start    :  %z %s", 0, &g_tMaintenanceStart, szBuffer);
    }

    if (g_bMaintenanceEndSet)
    {
        lSecondsEnd = TimeDateDifference (&g_tMaintenanceEnd ,&tNow);

        ConvertToHumanReadableTime (lSecondsEnd, sizeof (szTime), szTime);

        if (bIsMaintanance && (lSecondsEnd > 0))
            snprintf (szBuffer, sizeof (szBuffer), "(will end in %s)", szTime);
        else
            *szBuffer = '\0';

        AddInLogMessageText ("Maintenance end      :  %z %s", 0, &g_tMaintenanceEnd, szBuffer);
    }

    if (g_bMaintenanceStartSet && g_bMaintenanceEndSet)
    {
        lSeconds = TimeDateDifference (&g_tMaintenanceEnd, &g_tMaintenanceStart);

        if (lSeconds < 0)
        {
            AddInLogMessageText ("Warning: Maintenance end time is earlier than start time", 0);
        }
    }
}


const char *GetStringAfterPrefix (const char *pszString, const char *pszPrefix)
{
    size_t cchPrefix;

    if (!pszString || !pszPrefix)
        return NULL;

    cchPrefix = strlen(pszPrefix);

    if (strncmp(pszString, pszPrefix, cchPrefix) != 0)
        return NULL;

    return pszString + cchPrefix;
}


STATUS ConvertTimeStringToTimedate (const char *pszTimeString, TIMEDATE *retpTimeDate)
{
    STATUS error = NOERROR;
    char *p = (char *) &pszTimeString[0];

    if (NULL == p)
        return ERR_MISC_INVALID_ARGS;

    error = ConvertTextToTIMEDATE (NULL, NULL, &p, (WORD) strlen (pszTimeString), retpTimeDate);

    return error;
}


BOOL CheckMaintenanceEnd()
{
    LONG lSeconds = 0;

    if (g_bMaintenanceEndSet)
    {
        lSeconds = TimeDateDifference (&g_tMaintenanceEnd, &g_tMaintenanceStart);
        if (lSeconds < 0)
        {
            g_bMaintenanceEndSet = FALSE;

            OSSetEnvironmentVariable (ENV_DOMPROM_MAINTENANCE_END, "");
            AddInLogMessageText ("Info: Resetting earlier maintenance end time", 0);
            return TRUE;
        }
    }

    return FALSE;
}


BOOL UpdateMaintenance (const char *pszCmd)
{
    STATUS error    = NOERROR;
    DWORD  dwMaintenanceMinutes = 0;
    const  char *pszSub = NULL;

    if ( (0 == strcasecmp (pszCmd, "off")) ||
         (0 == strcasecmp (pszCmd, "clear")) )
    {
        g_bMaintenanceStartSet = FALSE;
        g_bMaintenanceEndSet   = FALSE;

        OSSetEnvironmentVariable (ENV_DOMPROM_MAINTENANCE_START, "");
        OSSetEnvironmentVariable (ENV_DOMPROM_MAINTENANCE_END, "");

        g_wMaintenanceEnabled = 0;

        AddInLogMessageText ("%s: Maintenance disabled", 0, g_szTask);
        goto Done;
    }

    pszSub = GetStringAfterPrefix (pszCmd, "on");
    if (pszSub)
    {
        if (*pszSub)
        {
            dwMaintenanceMinutes = atoi (pszSub);
        }

        if (0 == dwMaintenanceMinutes)
        {
            g_wMaintenanceEnabled = 1;
            AddInLogMessageText ("%s: Maintenance enabled until restart", 0, g_szTask);
            goto Done;
        }

        OSCurrentTIMEDATE (&g_tMaintenanceStart);
        OSCurrentTIMEDATE (&g_tMaintenanceEnd);
        TimeDateAdjust (&g_tMaintenanceEnd, 0, dwMaintenanceMinutes, 0, 0, 0, 0);

        OSSetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_START, &g_tMaintenanceStart);
        OSSetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_END,   &g_tMaintenanceEnd);

        g_bMaintenanceStartSet = TRUE;
        g_bMaintenanceEndSet   = TRUE;

        AddInLogMessageText ("%s: Maintenance set for %u minutes (from %z to %z)", 0, g_szTask, dwMaintenanceMinutes, &g_tMaintenanceStart, &g_tMaintenanceEnd);
        goto Done;
    }

    pszSub = GetStringAfterPrefix (pszCmd, "start +");
    if (pszSub)
    {
        if (*pszSub)
            dwMaintenanceMinutes = atoi (pszSub);

        if (0 == dwMaintenanceMinutes)
        {
            AddInLogMessageText ("%s: Invalid maintenance start option: %s", 0, g_szTask, pszCmd);
            goto Done;
        }

        OSCurrentTIMEDATE (&g_tMaintenanceStart);
        TimeDateAdjust (&g_tMaintenanceStart, 0, dwMaintenanceMinutes, 0, 0, 0, 0);

        OSSetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_START, &g_tMaintenanceStart);
        g_bMaintenanceStartSet = TRUE;
        AddInLogMessageText ("%s: Maintenance start set: %z", 0, g_szTask, &g_tMaintenanceStart);

        CheckMaintenanceEnd();
        goto Done;
    }

    pszSub = GetStringAfterPrefix (pszCmd, "start ");
    if (pszSub)
    {
        error = ConvertTimeStringToTimedate (pszSub, &g_tMaintenanceStart);

        if (error)
        {
            AddInLogMessageText ("%s: Cannot convert maintenance start date", error, g_szTask);
            goto Done;
        }

        OSSetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_START, &g_tMaintenanceStart);
        g_bMaintenanceStartSet = TRUE;
        AddInLogMessageText ("%s: Maintenance start set: %z", 0, g_szTask, &g_tMaintenanceStart);

        CheckMaintenanceEnd();
        goto Done;
    }

    pszSub = GetStringAfterPrefix (pszCmd, "end +");
    if (pszSub)
    {
        if (*pszSub)
            dwMaintenanceMinutes = atoi (pszSub);

        if (0 == dwMaintenanceMinutes)
        {
            AddInLogMessageText ("%s: Invalid maintenance end option: %s", 0, g_szTask, pszCmd);
            goto Done;
        }

        OSCurrentTIMEDATE (&g_tMaintenanceEnd);
        TimeDateAdjust (&g_tMaintenanceEnd, 0, dwMaintenanceMinutes, 0, 0, 0, 0);

        OSSetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_END,   &g_tMaintenanceEnd);
        g_bMaintenanceEndSet   = TRUE;
        AddInLogMessageText ("%s: Maintenance end set: %z", 0, g_szTask, &g_tMaintenanceEnd);
        goto Done;
    }

    pszSub = GetStringAfterPrefix (pszCmd, "end ");
    if (pszSub)
    {
        error = ConvertTimeStringToTimedate (pszSub, &g_tMaintenanceEnd);

        if (error)
        {
            AddInLogMessageText ("%s: Cannot convert maintenance end date", error, g_szTask);
            goto Done;
        }

        OSSetEnvironmentTIMEDATE (ENV_DOMPROM_MAINTENANCE_END, &g_tMaintenanceEnd);
        g_bMaintenanceEndSet = TRUE;
        AddInLogMessageText ("%s: Maintenance end set: %z", 0, g_szTask, &g_tMaintenanceEnd);
        goto Done;
    }

    AddInLogMessageText ("%s: Invalid maintenance command: %s", 0, g_szTask, pszCmd);
    return FALSE;

Done:

    return TRUE;

}


void ProcessCommand (const char *pszCmdBuffer)
{
    const char *pszCommand = NULL;

    if (IsNullStr (pszCmdBuffer))
    {
        return;
    }

    if (0 == strcasecmp (pszCmdBuffer, "help"))
    {
        PrintHelp();
    }

    else if ( (0 == strcasecmp (pszCmdBuffer, "config")) ||
              (0 == strcasecmp (pszCmdBuffer, "status")) )
    {
        PrintConfig();
    }

    else if ((pszCommand = GetStringAfterPrefix (pszCmdBuffer, "maintenance ")))
    {
        UpdateMaintenance (pszCommand);
    }

    else if ((pszCommand = GetStringAfterPrefix (pszCmdBuffer, "maint ")))
    {
        UpdateMaintenance (pszCommand);
    }

    else
    {
        AddInLogMessageText ("%s: Invalid command: %s", 0, g_szTask, pszCmdBuffer);
    }
}


BOOL CheckAndProcessCommand (MQHANDLE hQueue)
{
    STATUS error   = NOERROR;
    WORD   wMsgLen = 0;
    char   szMsgBuffer[MQ_MAX_MSGSIZE+1] = {0};

    if (NULLHANDLE == hQueue)
        return FALSE;

    if (MQIsQuitPending (hQueue))
        return TRUE;

    error = MQGet (hQueue, szMsgBuffer, MQ_MAX_MSGSIZE, 0, 0, &wMsgLen);

    if (NOERROR == error)
    {
        if (wMsgLen < MQ_MAX_MSGSIZE)
        {
            szMsgBuffer[wMsgLen] = '\0';
            ProcessCommand (szMsgBuffer);
        }
    }

    return FALSE;
}


STATUS LNPUBLIC AddInMain (HMODULE hResourceModule, int argc, char *argv[])
{
    STATUS  error = NOERROR;

    DHANDLE hOldStatusLine  = NULLHANDLE;
    DHANDLE hStatusLineDesc = NULLHANDLE;
    HMODULE hMod            = NULLHANDLE;

    DWORD   dwSeconds = 0;

    char    szStatsDirName[MAXPATH+100]    = {0};
    char    *pEnv = NULL;
    int     a = 0;
    char    ch = '\0';
    char    *pParam = NULL;

    MQHANDLE hQueue = NULLHANDLE;

    AddInQueryDefaults    (&hMod, &hOldStatusLine);
    AddInDeleteStatusLine (hOldStatusLine);

    hStatusLineDesc = AddInCreateStatusLine (g_szTaskLong);
    AddInSetDefaults (hMod, hStatusLineDesc);

    snprintf (g_szVersion, sizeof (g_szVersion), "%d.%d.%d", DOMPROM_VERSION_MAJOR, DOMPROM_VERSION_MINOR, DOMPROM_VERSION_PATCH);

    error = SECKFMGetUserName (g_szLocalUser);

    if (error)
    {
        AddInLogMessageText ("%s: Cannot get local server name", error, g_szTask);
        goto Done;
    }

    OSCurrentTIMEDATE (&g_tNextTransStatsUpdate);
    OSCurrentTIMEDATE (&g_tNextIOStatUpdate);
    OSGetDataDirectory (g_szDataDir);

    for (a=1; a<argc; a++)
    {
        if (0 == strcasecmp (argv[a], "--version"))
        {
            printf ("%s", g_szVersion);
            return NOERROR;
        }
        else if (0 == strcasecmp (argv[a], "-version"))
        {
            AddInLogMessageText ("%s: Domino Prometheus Exporter %s", 0, g_szTask, g_szVersion);
            return NOERROR;
        }
        else if (*(argv[a]) == '-' )
        {
            /* parse switches */
            ch = *(argv[a]+1);
            pParam = argv[a]+2;

            switch (ch)
            {
                case 'v':
                    g_wLogLevel++;
                    break;

                case 't':
                    g_wCollectDominoTransStats = MAX_CONFIG_VALUE_OVERRIDE;
                    break;

                case 'i':
                    g_wCollectDominoIOStat = MAX_CONFIG_VALUE_OVERRIDE;
                    break;

                case 'x':
                    if (*pParam == '\0')
                    {
                        if (a < (argc-1))
                        {
                            a++;
                            pParam = argv[a];
                            if (*pParam == '-')
                                goto invalid_syntax;
                        }
                    }

                    break;

                case '?':
                case 'h':
                    PrintHelp();
                    goto Done;
                    break;

                default:
                    AddInLogMessageText ("%s: Error invalid option '%s'", error, g_szTask, argv[a]+1);
                    goto Done;
            } /* switch */

        } /* if */
        else if (*argv[a] == '=')
        {
            // handled by core
        }
        else
        {
            goto invalid_syntax;
        }

    } /* for */

    error = MQCreate (MsgQueueName, 0, 0);

    if (error)
    {
        error = NOERROR;
        AddInLogMessageText ("%s: Servertask already started", 0, g_szTask);
        goto Done;
    }

    error = MQOpen (MsgQueueName, 0, &hQueue);

    if (error)
    {
        AddInLogMessageText ("%s: Cannot open message queue", error, g_szTask);
        goto Done;
    }

    // Add Include/Exclude rules - Names must be specified all LOWERCASE!

    // Exclude platform stats in general and just include relevant stats to not pollute stats
    g_StatsFilter.AddExclude ("platform.");
    g_StatsFilter.AddInclude ("platform.network.total.");

    g_StatsFilter.AddExclude ("disk.");

    // Exclude sensitive stats containing names
    g_StatsFilter.AddExclude ("replica.cluster.currency.");
    g_StatsFilter.AddExclude ("server.cluster.member.");
    g_StatsFilter.AddExclude ("net.log.");

    // Exclude statistics which contain PIDs which change too often and don't fit for monitoring
    g_StatsFilter.AddExclude ("mem.pid.");
    g_StatsFilter.AddExclude ("mem.local.max.used.");

    // Exclude Traveler stats in general and just include relevant stats to not pollute stats
    g_StatsFilter.AddExclude ("traveler.");

    g_StatsFilter.AddInclude ("traveler.memory.");
    g_StatsFilter.AddInclude ("traveler.status.state.severity");
    g_StatsFilter.AddInclude ("traveler.push.");
    g_StatsFilter.AddInclude ("traveler.primesync.count");
    g_StatsFilter.AddInclude ("traveler.constrained.");
    g_StatsFilter.AddInclude ("traveler.http.status.");
    g_StatsFilter.AddInclude ("traveler.monitor.users");

    g_StatsFilter.Finalize();

    error = NSFGetTransLogStyle (&g_wTranslogLogType);

    if (error)
    {
        AddInLogMessageText ("%s: Cannot get transaction log type", error, g_szTask);
        g_wTranslogLogType = 99;
    }

    GetDiskStatNamesFromNotesIni();

    /* First check if an outfile is defined */
    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_OUTFILE, g_szStatsFilename, sizeof (g_szStatsFilename)-1))
    {
        *g_szStatsFilename = '\0';
    }

    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_TRANS_OUTFILE, g_szTransFilename, sizeof (g_szTransFilename)-1))
    {
        *g_szTransFilename = '\0';
    }

    /* Else check for Domino defined stats directory */
    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_STATS_DIR, szStatsDirName, sizeof (szStatsDirName)-1))
    {
        *szStatsDirName = '\0';
    }

    /* Else check for OS level defined stats directory */
    if ('\0' == *szStatsDirName)
    {
        pEnv = getenv (ENV_DOMPROM_STATS_DIR);

        if (pEnv)
        {
            snprintf (szStatsDirName, sizeof (szStatsDirName), "%s", pEnv);
        }
    }

    /* If no stats dir set the default is notesdata/domino/stats */
    if ('\0' == *szStatsDirName)
    {
        snprintf (szStatsDirName, sizeof (szStatsDirName), "%s%c%s%c%s", g_szDataDir, g_DirSep, "domino", g_DirSep, "stats");
    }

    CreateDirIfNotExists (szStatsDirName);

    if (IsNullStr (g_szStatsFilename))
        snprintf (g_szStatsFilename, sizeof (g_szStatsFilename), "%s%c%s", szStatsDirName, g_DirSep, g_szDominoProm);

    if (IsNullStr (g_szTransFilename))
        snprintf (g_szTransFilename, sizeof (g_szTransFilename), "%s%c%s", szStatsDirName, g_DirSep, g_szDominoTransProm);

    GetEnvironmentVars (TRUE);

    AddInLogMessageText ("%s: Domino Prometheus Exporter %s", 0, g_szTask, g_szVersion);

    AddInLogMessageText ("%s: Statistics Interval: %u seconds, File: %s", 0, g_szTask, g_dwIntervalSec, g_szStatsFilename);

    if (g_wCollectDominoTransStats)
    {
        AddInLogMessageText ("%s: Statistic Transactions Interval: %u, File: %s", 0, g_szTask, g_dwTransIntervalSec, g_szTransFilename);
    }

    if (g_wCollectDominoIOStat)
    {
        AddInLogMessageText ("%s: Domino I/O Statistic Interval: %u", 0, g_szTask, g_dwIOStatIntervalSec);
    }

    AddInLogMessageText ("%s: %s (%s)", 0, g_szTask, g_szCopyright, g_szGitHubURL);

    ReadStatisticsInfoFromEvents4();

    AddInSetStatusText ("Ready");

    while (0 == g_ShutdownPending)
    {
        AddInSetStatusText ("Collecting Stats");
        error = ProcessDominoStatistics (g_szStatsFilename);

        if (g_wCollectDominoTransStats)
            ProcessTransStats (g_szTransFilename, g_dwTransIntervalSec);

        if (g_wCollectDominoIOStat)
            ProcessIOStat (g_dwIOStatIntervalSec);

        UpdateIdleStatus();

        for (dwSeconds = 0; dwSeconds < g_dwIntervalSec; dwSeconds++)
        {
            if (CheckAndProcessCommand (hQueue))
            {
                g_ShutdownPending = 1;
                break;
            }

            /* Don't check environment vars too often */
            if ( 0 == (dwSeconds % 10))
                GetEnvironmentVars (FALSE);

            if (AddInIdleDelay (1000))
            {
                g_ShutdownPending = 1;
                break;
            }
        }

    } /* while */

    goto Done;

invalid_syntax:

    AddInLogMessageText ("%s: Invalid syntax", 0, g_szTask);

Done:

    ProcessDominoStatistics (g_szStatsFilename, true);

    /* Remove Transaction Domino stats file if present */
    RemoveFile (g_szTransFilename, 1);

    /* Remove stats files on shutdown to not leave any stale *.prom stats files */
    DeleteAllStatsForPackage (g_szDominoHealth);

    if (hQueue)
    {
        MQClose (hQueue, 0);
        hQueue = NULLHANDLE;
    }

    AddInLogMessageText ("%s: Terminated", 0, g_szTask);

    return error;
}
