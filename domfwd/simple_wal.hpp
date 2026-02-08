#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <mutex>


class SimpleWAL
{

public:

    SimpleWAL ();
    ~SimpleWAL();

    bool Init (const std::string& Path);

    // Append a binary record
    bool Append (const void* pData, uint32_t Len);

    // Replay from last committed offset
    // Consume() must return true on success, false on failure
    bool Replay (const std::function<bool (const std::vector<uint8_t>&)>& Consume);

    bool ReplaySingleCommit (const std::function<bool (const std::vector<uint8_t>&)>& Consume);

    // Clear WAL and start again
    bool Clear();

    bool IsReplayPending();

    bool GetCommit() {return m_bCommit; };
    void SetCommit (bool bEnable) {m_bCommit = bEnable; };

    void SetWalFile (const std::string& Path) { m_WalPath = Path; }


private:

    std::mutex m_mutex;
    std::string m_WalPath;
    std::string m_CommitPath;
    int m_fd;
    bool m_bCommit;

    bool m_PendingReplay;

    bool WriteAll (const void* pBuf, size_t Len);
    uint64_t LoadCommit();
    bool StoreCommit (uint64_t Offset);
    // Clear WAL and start again
    bool ClearInternal();
};

