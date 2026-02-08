
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include "simple_wal.hpp"

size_t g_ShutdownRequested = 0;
size_t g_WalThreadRunning  = 0;
size_t g_Count             = 0;

pthread_t g_WalThreadInstance = {0};
SimpleWAL g_Wal;


void push (const char* pszData)
{
    g_Wal.Append (pszData, strlen (pszData));
}


uint64_t get_time_ms ()
{
    struct timespec Ts;
    clock_gettime (CLOCK_MONOTONIC, &Ts);

    return (uint64_t)Ts.tv_sec * 1000ULL + (uint64_t)Ts.tv_nsec / 1000000ULL;
}

void push_now ()
{
    struct timespec Ts;
    ::clock_gettime (CLOCK_REALTIME, &Ts);

    char Buffer[64];
    int Len = ::snprintf (
        Buffer,
        sizeof (Buffer),
        "%lld.%09ld\n",
        static_cast<long long> (Ts.tv_sec),
        Ts.tv_nsec
    );

    if (Len > 0)
        g_Wal.Append (Buffer, static_cast<uint32_t> (Len));
}


void pop ()
{
    if (false == g_Wal.IsReplayPending())
        return;

    g_Wal.Replay ([] (const std::vector<uint8_t>& Record)
    {
        ssize_t len = 0;

        if (1)
            len = ::write (1, Record.data (), Record.size ());
        else
            len = Record.size ();

        if (len)
        {
            g_Count++;
            return true;
        }
        else
        {
            return false;
        }
    });
}


void sleep_ms (unsigned int ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    nanosleep (&ts, NULL);
}


void *WalThread (void *arg)
{
    (void)arg;
    g_WalThreadRunning = 1;

    while (0 == g_ShutdownRequested)
    {
        if (g_Wal.IsReplayPending())
        {
            pop();
            fprintf (stderr, "\nCount: %lu\n\n", g_Count);
        }

        sleep_ms (2);
    }

    g_WalThreadRunning = 0;

    return NULL;
}


int main (int argc, char *argv[])
{
    size_t count = 0;
    size_t Max   = 10;
    size_t Loops = 10;

    uint64_t tStart = get_time_ms();;
    uint64_t ms = 0;

    if (argc > 1)
        Max = atoi (argv[1]);

    g_Wal.Init ("test.wal");

    if (0 != pthread_create (&g_WalThreadInstance, NULL, WalThread, NULL))
    {
        perror ("pthread_create");
        return EXIT_FAILURE;
    }

    while (Loops--)
    {
        count = Max;
        while (count--)
            push_now();

        sleep_ms (10);
    }

    fprintf (stderr, "Done adding entries\n");

    while (g_Wal.IsReplayPending())
    {
        sleep_ms (10);
    }

    fprintf (stderr, "No entries pending\n");

    g_ShutdownRequested = 1;

    while (g_WalThreadRunning)
    {
        sleep_ms (10);
    }

    ms = get_time_ms() - tStart;

    if (g_Count)
    {
        fprintf (stderr, "Runtime: %lu ms, Requests: %lu, Time per request: %1.2f ms\n", ms, g_Count, (double) ms / (double)g_Count);
    }

    fprintf (stderr, "Terminated\n");

}
