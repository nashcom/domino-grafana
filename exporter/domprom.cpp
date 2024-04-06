
/* Includes */

#include <stdio.h>
#include <string.h>

#include <global.h>
#include <addin.h>
#include <osmem.h>
#include <stats.h>
#include <miscerr.h>
#include <intl.h>
#include <osfile.h>
#include <osenv.h>

#define DOMPROM_VERSION "0.9.0"

#define ENV_DOMPROM_LOGLEVEL "domprom_loglevel"
#define ENV_DOMPROM_OUTFILE  "domprom_outfile"
#define ENV_DOMPROM_INTERVAL "domprom_interval"

#define DOMPROM_DEFAULT_INTERVAL 30
#define DOMPROM_MINIMUM_INTERVAL 10


/* Types */

struct CONTEXT_STRUCT_TYPE
{
    INTLFORMAT Intl;

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

char  g_szVersion [] = DOMPROM_VERSION;
char  g_szTask[] = "domprom";
char  g_szTaskLong[] = "Prometheus Exporter";
char  g_szDataDir[MAXPATH+1] = {0};
WORD  g_ShutdownPending = 0;
LONG  g_LogLevel        = 1;
DWORD g_dwInterval      = DOMPROM_DEFAULT_INTERVAL;

void TrimAtFirstBlank (char *pszBuffer)
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

    NumberFormat.Digits = 2;
    NumberFormat.Format = NFMT_GENERAL;

    if (NULL == pStats)
        return ERR_MISC_INVALID_ARGS;

    if (NULL == pStats->fp)
        return ERR_MISC_INVALID_ARGS;

    pStats->CountAll++;

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

    switch (wValueType)
    {
        case VT_TEXT:

            pStats->CountText++;

            if (pStats->bExportText)
            {
                snprintf (szValue, sizeof (szValue), "%s", (char *)pValue);
                TrimAtFirstBlank (szValue);

                fprintf (pStats->fp, "%s%c%s\n", szMetric, DelimChar, szValue);
            }
            break;

        case VT_LONG:

            pStats->CountLong++;

            if (pStats->bExportLong)
            {
                NotesLong = *(LONG *) pValue;
                fprintf (pStats->fp, "%s%c%d\n", szMetric, DelimChar, NotesLong);
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
                    fprintf (pStats->fp, "%s%c%s\n", szMetric, DelimChar, szValue);
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
                    //fprintf (pStats->fp, "%s%c%s\n", szMetric, DelimChar, szValue);
                }
            }
            break;
    
        default:

            pStats->CountUnknown++;
            break;

    } /* switch */

    return error;
}


STATUS LNPUBLIC GetDominoStatsTraverse (const char  *pszFilename)
{
    STATUS error = NOERROR;
    char   szTempFilename[MAXPATH+1] = {0};

    CONTEXT_STRUCT_TYPE Stats = {0};

    if (NULL == pszFilename)
        return ERR_MISC_INVALID_ARGS;

    /* Define which stat types to export (Only numeric values make sense for Prometheus/Grafana) */
    Stats.bExportLong   = TRUE;
    Stats.bExportNumber = TRUE;

    snprintf (szTempFilename,  sizeof (szTempFilename),  "%s.tmp", pszFilename);

    Stats.fp = fopen (szTempFilename, "w");

    if (NULL == Stats.fp)
    {
        perror ("Cannot create stats temp file");
        goto Done;
    }

    OSGetIntlSettings(&(Stats.Intl), sizeof (Stats.Intl));
    snprintf (Stats.Intl.DecimalString,  sizeof (Stats.Intl.DecimalString), ".");
    snprintf (Stats.Intl.ThousandString, sizeof (Stats.Intl.ThousandString), ",");

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

    g_LogLevel   = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_LOGLEVEL);
    g_dwInterval = (DWORD) OSGetEnvironmentLong (ENV_DOMPROM_INTERVAL);

    if (0 == g_dwInterval)
        g_dwInterval = DOMPROM_DEFAULT_INTERVAL;

    if (g_dwInterval < DOMPROM_MINIMUM_INTERVAL)
        g_dwInterval = DOMPROM_MINIMUM_INTERVAL;

    return TRUE;
}


STATUS LNPUBLIC AddInMain (HMODULE hResourceModule, int argc, char *argv[])
{
    STATUS  error = NOERROR;

    DHANDLE hOldStatusLine  = NULLHANDLE;
    DHANDLE hStatusLineDesc = NULLHANDLE;
    HMODULE hMod            = NULLHANDLE;

    char    szStatsFilename[MAXPATH+100] = {0};
    char    szStatus [MAXSPRINTF+1]      = {0};
    char    *pszStatsFilename = szStatsFilename;

    DWORD   dwSeconds  = 0;

    AddInQueryDefaults    (&hMod, &hOldStatusLine);
    AddInDeleteStatusLine (hOldStatusLine);

    hStatusLineDesc = AddInCreateStatusLine (g_szTaskLong);
    AddInSetDefaults (hMod, hStatusLineDesc);

    OSGetDataDirectory (g_szDataDir);

    if (FALSE == OSGetEnvironmentString (ENV_DOMPROM_OUTFILE, szStatsFilename, sizeof (szStatsFilename)-1))
    {
        *szStatsFilename = '\0';
    }

    if ('\0' == *szStatsFilename)
    {
#ifdef _WIN32
        snprintf (szStatsFilename, sizeof (szStatsFilename), "%s\\domino\\domino.prom", g_szDataDir);
#else
        snprintf (szStatsFilename, sizeof (szStatsFilename), "%s/domino/domino.prom", g_szDataDir);
#endif
    }

    GetEnvironmentVars (TRUE);

    AddInLogMessageText ("%s: Domino Prometheus Exporter %s (Interval: %u seconds, File: %s)", 0, g_szTask, g_szVersion, g_dwInterval, szStatsFilename);

    AddInSetStatusText ("Ready");

    while (0 == g_ShutdownPending)
    {
        AddInSetStatusText ("Running");
        error = GetDominoStatsTraverse (pszStatsFilename);

        snprintf (szStatus, sizeof (szStatus), "Idle (Interval: %u)", g_dwInterval);
        AddInSetStatusText (szStatus);

        GetEnvironmentVars(FALSE);

        for (dwSeconds = 0;  dwSeconds < g_dwInterval; dwSeconds++)
        {
            if (AddInIdleDelay (1000))
            {
                g_ShutdownPending = 1;
                break;
            }
        }

    } /* while */

    AddInLogMessageText ("%s: Terminated", 0, g_szTask);

    return error;
}
