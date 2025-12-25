/*
###########################################################################
# Domino Prometheus Exporter                                              #
# Version 0.9.3 24.12.2025                                                #
# (C) Copyright Daniel Nashed/Nash!Com 2024-2025                          #
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

#define DOMPROM_VERSION    "0.9.3"
#define DOMPROM_COPYRIGHT  "Copyright Daniel Nashed/Nash!Com 2024-2025"
#define DOMPROM_GITHUB_URL "https://github.com/nashcom/domino-grafana"

#define ENV_DOMPROM_LOGLEVEL        "domprom_loglevel"
#define ENV_DOMPROM_STATS_DIR       "domprom_outdir"
#define ENV_DOMPROM_OUTFILE         "domprom_outfile"
#define ENV_DOMPROM_TRANS_OUTFILE   "domprom_trans_outfile"
#define ENV_DOMPROM_INTERVAL        "domprom_interval"
#define ENV_DOMPROM_NO_PREFIX       "domprom_no_domino_prefix"
#define ENV_OS_DOMPROM_STATS_DIR    "DOMINO_PROM_STATS_DIR"
#define ENV_DOMPROM_COLLECT_TRANS   "domprom_collect_trans"
#define ENV_DOMPROM_COLLECT_IOSTAT  "domprom_collect_iostat"

#define DOMPROM_DEFAULT_INTERVAL_SEC 60
#define DOMPROM_MINIMUM_INTERVAL_SEC 10

#define DOMPROM_DEFAULT_TRANS_INTERVAL_SEC 120
#define DOMPROM_MINIMUM_TRANS_INTERVAL_SEC  60

#define DOMPROM_DEFAULT_IOSTAT_INTERVAL_SEC 600
#define DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC  60


#define DOMPROM_DISK_COMPONENT_NOTESDATA     "Notesdata"
#define DOMPROM_DISK_COMPONENT_TRANSLOG      "Translog"
#define DOMPROM_DISK_COMPONENT_DAOS          "DAOS"
#define DOMPROM_DISK_COMPONENT_NIF           "NIF"
#define DOMPROM_DISK_COMPONENT_FT            "FT"
#define DOMPROM_DISK_COMPONENT_NOTES_TEMP    "NotesTemp"
#define DOMPROM_DISK_COMPONENT_VIEW_REBUILD  "ViewRebuild"
#define DOMPROM_DISK_COMPONENT_NOTES_LOG_DIR "NotesLogDir"


/* Includes */

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <dirent.h>
  #include <sys/statvfs.h>
  #include <limits.h>
#endif

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <math.h>

#include <global.h>
#include <addin.h>
#include <intl.h>
#include <miscerr.h>
#include <nsfdb.h>
#include <osenv.h>
#include <osfile.h>
#include <osmem.h>
#include <osmisc.h>
#include <stats.h>
#include <kfm.h>
#include <oserr.h>
#include <ostime.h>


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

char  g_szVersion[]        = DOMPROM_VERSION;
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

WORD   g_wTranslogLogType     = 0;
WORD   g_wLogLevel            = 0;

int    g_StatusDAOS           = 0;
int    g_CatalogInSyncDAOS    = 0;
size_t g_TranslogMinLogExtend = 0;
size_t g_TranslogMaxLogExtend = 0;
WORD   g_wWriteDominoHealthStats   = 1;
WORD   g_wCollectDominoTransStats  = 0;
WORD   g_wCollectDominoIOStat      = 0;

TIMEDATE g_tNextTransStatsUpdate = {0};
TIMEDATE g_tNextIOStatUpdate     = {0};

#define MAX_CONFIG_VALUE_OVERRIDE 99

#ifdef _WIN32
char g_DirSep = '\\';
#else
char g_DirSep = '/';
#endif

WORD  g_ShutdownPending     = 0;
LONG  g_LogLevel            = 1;
DWORD g_dwIntervalSec       = DOMPROM_DEFAULT_INTERVAL_SEC;
DWORD g_dwTransIntervalSec  = DOMPROM_DEFAULT_TRANS_INTERVAL_SEC;
DWORD g_dwIOStatIntervalSec = DOMPROM_DEFAULT_IOSTAT_INTERVAL_SEC;


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
            perror ("Cannot create directory");
        }
#else
        if (0 == mkdir (pszFilename, 0770))
        {
            bCreated = true;
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
        g_CatalogInSyncDAOS = 1;
    }
    else
    {
        g_CatalogInSyncDAOS = 0;
    }

    return error;
}


STATUS LNCALLBACK DomExportTraverse (void *pContext, char *pszFacility, char *pszStatName, WORD wValueType, void *pValue)
{
    STATUS error          = NOERROR;
    LONG   NotesLong      = 0;
    WORD   wLen           = 0;
    NFMT   NumberFormat   = {0};
    char   szMetric[1024] = {0};
    char   szValue[1024]  = {0};
    char   DelimChar      = ' ';

    CONTEXT_STRUCT_TYPE *pStats = (CONTEXT_STRUCT_TYPE*)pContext;

    if (NULL == pszFacility)
        return NOERROR;

    if (NULL == pszStatName)
        return NOERROR;

    if (NULL == pValue)
        return NOERROR;

    if (NULL == pStats)
        return ERR_MISC_INVALID_ARGS;

    if (NULL == pStats->fp)
        return ERR_MISC_INVALID_ARGS;

    pStats->CountAll++;

    NumberFormat.Digits = 2;
    NumberFormat.Format = NFMT_GENERAL;

    snprintf (szMetric, sizeof (szMetric), "%s.%s", pszFacility, pszStatName);
    ReplaceChars (szMetric);

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

    if (g_wWriteDominoHealthStats)
    {
        if (0 == strcmp (pszFacility, g_szDominoHealth))
        {
            return NOERROR;
        }
    }

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

            if (pStats->bExportText)
            {
                snprintf (szValue, sizeof (szValue), "%s", (char *)pValue);

                TruncateAtFirstBlank (szValue);

                fprintf (pStats->fp, "%s%s%c%s\n", pStats->szPrefix, szMetric, DelimChar, szValue);
            }
            break;

        case VT_LONG:

            pStats->CountLong++;

            if (pStats->bExportLong)
            {
                NotesLong = *(LONG *) pValue;
                fprintf (pStats->fp, "%s%s%c%d\n", pStats->szPrefix, szMetric, DelimChar, NotesLong);
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
                    fprintf (pStats->fp, "%s%s%c%s\n", pStats->szPrefix, szMetric, DelimChar, szValue);
                }
            }
            break;

        case VT_TIMEDATE:

            pStats->CountTime++;

            if (pStats->bExportTime)
            {
                if (GetNotesTimeDateSting ((TIMEDATE *)pValue, sizeof (szValue), szValue))
                {
                    pStats->CountInvalid++;
                }
                else
                {
                    fprintf (pStats->fp, "%s%s%c%s\n", pStats->szPrefix, szMetric, DelimChar, szValue);
                }
            }
            break;

        default:

            pStats->CountUnknown++;
            break;

    } /* switch */

    return error;
}

STATUS ProcessSingleDiskStat (FILE *fp, const char *pszPathName, const char *pszComponent)
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

    if (NULL == fp)
        return ERR_MISC_INVALID_ARGS;

    fprintf (fp, "%s_disk_total_bytes{component=\"%s\", path=\"%s\"} %" PRIu64 "\n", g_szDominoHealth, pszComponent, pszPathName, TotalBytes);
    fprintf (fp, "%s_disk_free_bytes{component=\"%s\", path=\"%s\"} %" PRIu64 "\n", g_szDominoHealth, pszComponent, pszPathName, FreeBytes);

    return NOERROR;
}


STATUS ProcessDiskStats (FILE *fp)
{
    if (NULL == fp)
        return ERR_MISC_INVALID_ARGS;

    ProcessSingleDiskStat (fp, g_szDataDir,     DOMPROM_DISK_COMPONENT_NOTESDATA);
    ProcessSingleDiskStat (fp, g_szDirTranslog, DOMPROM_DISK_COMPONENT_TRANSLOG);
    ProcessSingleDiskStat (fp, g_szDirDAOS,     DOMPROM_DISK_COMPONENT_DAOS);
    ProcessSingleDiskStat (fp, g_szDirNIF,      DOMPROM_DISK_COMPONENT_NIF);
    ProcessSingleDiskStat (fp, g_szDirFT,       DOMPROM_DISK_COMPONENT_FT);
    ProcessSingleDiskStat (fp, g_szNotesTemp,   DOMPROM_DISK_COMPONENT_NOTES_TEMP);
    ProcessSingleDiskStat (fp, g_szViewRebuild, DOMPROM_DISK_COMPONENT_VIEW_REBUILD);
    ProcessSingleDiskStat (fp, g_szNotesLogDir, DOMPROM_DISK_COMPONENT_NOTES_LOG_DIR);

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

    fprintf (fp, "%s_daos_status %d\n", g_szDominoHealth, g_StatusDAOS);
    fprintf (fp, "%s_daos_catalog_status %d\n", g_szDominoHealth, CatalogStatus);

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

    fprintf (fp, "%s_translog_style %u\n", g_szDominoHealth, g_wTranslogLogType);

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

    fprintf (fp, "%s_translog_file_count %zu\n", g_szDominoHealth, NumTranslogFiles);

    if (0 == NumTranslogFiles)
    {
        goto Done;
    }

    fprintf (fp, "%s_translog_file_min %zu\n", g_szDominoHealth, g_TranslogMinLogExtend);
    fprintf (fp, "%s_translog_file_max %zu\n", g_szDominoHealth, g_TranslogMaxLogExtend);

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

bool ParseOneStatsRow (const char **ppsz, STAT_ROW *pRow)
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


STATUS WriteStat (FILE *fp, const char *pszTransName, const char *pszStatName, int StatsValue)
{
    STATUS error = NOERROR;

    char  szTrans[1024] = {0};

    if (NULL == fp)
        return ERR_MISC_INVALID_ARGS;

    if (IsNullStr (pszTransName))
        return ERR_MISC_INVALID_ARGS;

    if (IsNullStr (pszStatName))
        return ERR_MISC_INVALID_ARGS;

    snprintf (szTrans, sizeof (szTrans), "%s", pszTransName);
    ReplaceChars (szTrans);

    fprintf (fp, "%s{op=\"%s\",stat=\"%s\"} %d\n", g_szDominoTrans, szTrans, pszStatName, StatsValue);

    return error;
}


int ParseStatsBuffer (const char *pszBuffer, FILE *fp)
{
    int rows = 0;
    const char *psz = NULL;
    STAT_ROW stRow = {0};

    if (IsNullStr(pszBuffer))
        return 0;

    if (NULL == fp)
        return 0;

    /* find header line */
    psz = strstr (pszBuffer, "Function");

    if (NULL == psz)
        return 0;

    /* skip to next line */
    if (false == ParseSkipToNextLine (&psz))
        goto Done;

    /* skip another empty line */
    if (false == ParseSkipToNextLine (&psz))
        goto Done;

    while (ParseOneStatsRow (&psz, &stRow))
    {
        WriteStat (fp, stRow.szName, "count",    stRow.iCount);
        WriteStat (fp, stRow.szName, "total_ms", stRow.iTotal);
        WriteStat (fp, stRow.szName, "avg_ms",   stRow.iAverage);
        rows++;
    }

Done:

    return rows;
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

    TIMEDATE tNow = {0};

    OSCurrentTIMEDATE (&tNow);

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

    snprintf (szTempFilename, sizeof (szTempFilename),  "%s.tmp", pszFilename);

    fp = fopen (szTempFilename, "w");

    if (NULL == fp)
    {
        perror ("Cannot create stats temp file");
        goto Done;
    }

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

    ParseStatsBuffer ((const char *) pInfoBuffer, fp);

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


STATUS LNPUBLIC GetDominoStatsTraverse (const char *pszFilename)
{
    STATUS error = NOERROR;
    char   szTempFilename[MAXPATH+1] = {0};

    CONTEXT_STRUCT_TYPE Stats = {0};

    if (NULL == pszFilename)
        return ERR_MISC_INVALID_ARGS;

    if (0 == OSGetEnvironmentLong (ENV_DOMPROM_NO_PREFIX))
        snprintf (Stats.szPrefix, sizeof (Stats.szPrefix), "Domino_");

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

    if (g_wWriteDominoHealthStats)
    {
        StatUpdateText (g_szDominoHealth, "Intl.DecimalString", Stats.Intl.DecimalString);
        StatUpdateText (g_szDominoHealth, "Intl.ThousandString", Stats.Intl.ThousandString);
    }

    StatTraverse (NULL, NULL, DomExportTraverse, &Stats);

    if (g_LogLevel)
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

    ProcessDiskStats     (Stats.fp);
    ProcessDaosStats     (Stats.fp);
    ProcessTranslogStats (Stats.fp);

Done:

    if (Stats.fp)
    {
        fclose (Stats.fp);
        Stats.fp = NULL;

        rename (szTempFilename, pszFilename);
    }

    return error;
}

BOOL GetEnvironmentVars (BOOL bForce)
{
    static WORD SeqNo  = 0;
    WORD   wTempSeqNo  = 0;

    wTempSeqNo = OSGetEnvironmentSeqNo();

    if (FALSE == bForce)
        if (wTempSeqNo == SeqNo)
            return FALSE;

    SeqNo = wTempSeqNo;

    g_LogLevel      = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_LOGLEVEL);
    g_dwIntervalSec = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_INTERVAL);

    if (g_wCollectDominoTransStats < MAX_CONFIG_VALUE_OVERRIDE)
        g_wCollectDominoTransStats = (WORD) OSGetEnvironmentLong (ENV_DOMPROM_COLLECT_TRANS);

    if (g_wCollectDominoIOStat < MAX_CONFIG_VALUE_OVERRIDE)
        g_wCollectDominoIOStat = (WORD) OSGetEnvironmentLong (ENV_DOMPROM_COLLECT_IOSTAT);

    if (0 == g_dwIntervalSec)
        g_dwIntervalSec = DOMPROM_DEFAULT_INTERVAL_SEC;

    if (g_dwIntervalSec < DOMPROM_MINIMUM_INTERVAL_SEC)
        g_dwIntervalSec = DOMPROM_MINIMUM_INTERVAL_SEC;

    if (0 == g_dwTransIntervalSec)
        g_dwTransIntervalSec = DOMPROM_MINIMUM_TRANS_INTERVAL_SEC;

    if (g_dwTransIntervalSec < DOMPROM_MINIMUM_TRANS_INTERVAL_SEC)
        g_dwTransIntervalSec = DOMPROM_MINIMUM_TRANS_INTERVAL_SEC;

    if (0 == g_dwIOStatIntervalSec)
        g_dwIOStatIntervalSec = DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC;

    if (g_dwIOStatIntervalSec < DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC)
        g_dwIOStatIntervalSec = DOMPROM_MINIMUM_IOSTAT_INTERVAL_SEC;

    return TRUE;
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
    AddInLogMessageText ("Syntax: %s ", 0, g_szTask);
    AddInLogMessageText ("-v     Verbose logging", 0);
    AddInLogMessageText ("-t     Write transactions", 0);
}


STATUS LNPUBLIC AddInMain (HMODULE hResourceModule, int argc, char *argv[])
{
    STATUS  error = NOERROR;

    DHANDLE hOldStatusLine  = NULLHANDLE;
    DHANDLE hStatusLineDesc = NULLHANDLE;
    HMODULE hMod            = NULLHANDLE;

    DWORD   dwSeconds = 0;

    char    szStatsFilename[2*MAXPATH+200] = {0};
    char    szTransFilename[2*MAXPATH+200] = {0};
    char    szStatsDirName[MAXPATH+100]    = {0};
    char    szStatus [MAXSPRINTF+1]        = {0};
    char    *pEnv = NULL;
    int     a = 0;
    char    ch = '\0';
    char    *pParam = NULL;

    AddInQueryDefaults    (&hMod, &hOldStatusLine);
    AddInDeleteStatusLine (hOldStatusLine);

    hStatusLineDesc = AddInCreateStatusLine (g_szTaskLong);
    AddInSetDefaults (hMod, hStatusLineDesc);

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
        if (*(argv[a]) == '-' )
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


    error = NSFGetTransLogStyle (&g_wTranslogLogType);

    if (error)
    {
        AddInLogMessageText ("%s: Cannot get transaction log type", error, g_szTask);
        g_wTranslogLogType = 99;
    }

    GetDiskStatNamesFromNotesIni();

    /* First check if an outfile is defined */
    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_OUTFILE, szStatsFilename, sizeof (szStatsFilename)-1))
    {
        *szStatsFilename = '\0';
    }

    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_TRANS_OUTFILE, szTransFilename, sizeof (szTransFilename)-1))
    {
        *szTransFilename = '\0';
    }

    /* Else check for Domino defined stats directory */
    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_STATS_DIR, szStatsDirName, sizeof (szStatsDirName)-1))
    {
        *szStatsDirName = '\0';
    }

    /* Else check for OS level defined stats directory */
    if ('\0' == *szStatsDirName)
    {
        pEnv = getenv (ENV_OS_DOMPROM_STATS_DIR);

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

    if (IsNullStr (szStatsFilename))
        snprintf (szStatsFilename, sizeof (szStatsFilename), "%s%c%s", szStatsDirName, g_DirSep, g_szDominoProm);

    if (IsNullStr (szTransFilename))
        snprintf (szTransFilename, sizeof (szTransFilename), "%s%c%s", szStatsDirName, g_DirSep, g_szDominoTransProm);

    GetEnvironmentVars (TRUE);

    AddInLogMessageText ("%s: Domino Prometheus Exporter %s", 0, g_szTask, g_szVersion);

    AddInLogMessageText ("%s: Statistics Interval: %u seconds, File: %s,", 0, g_szTask, g_dwIntervalSec, szStatsFilename);

    if (g_wCollectDominoTransStats)
    {
        AddInLogMessageText ("%s: Statistic Transactions Interval: %u, File: %s", 0, g_szTask, g_dwTransIntervalSec, szTransFilename);
    }

    if (g_wCollectDominoIOStat)
    {
        AddInLogMessageText ("%s: Domino I/O Statistic Interval: %u", 0, g_szTask, g_dwIOStatIntervalSec);
    }

    AddInLogMessageText ("%s: %s (%s)", 0, g_szTask, g_szCopyright, g_szGitHubURL);

    AddInSetStatusText ("Ready");

    while (0 == g_ShutdownPending)
    {
        AddInSetStatusText ("Collecting Stats");
        error = GetDominoStatsTraverse (szStatsFilename);

        if (g_wCollectDominoTransStats)
            ProcessTransStats (szTransFilename, g_dwTransIntervalSec);

        if (g_wCollectDominoIOStat)
            ProcessIOStat (g_dwIOStatIntervalSec);

        snprintf (szStatus, sizeof (szStatus), "Idle (Interval: %u)", g_dwIntervalSec);
        AddInSetStatusText (szStatus);

        GetEnvironmentVars(FALSE);

        for (dwSeconds = 0;  dwSeconds < g_dwIntervalSec; dwSeconds++)
        {
            if (AddInIdleDelay (1000))
            {
                g_ShutdownPending = 1;
                break;
            }
        }

    } /* while */

    DeleteAllStatsForPackage (g_szDominoHealth);

    goto Done;

invalid_syntax:
    AddInLogMessageText ("%s: Invalid syntax", 0, g_szTask);

Done:

    AddInLogMessageText ("%s: Terminated", 0, g_szTask);

    return error;
}
