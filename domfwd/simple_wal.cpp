
#include <stdexcept>
#include <cstring>
#include <mutex>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "simple_wal.hpp"


off_t get_file_size (int fd)
{
    struct stat StatBuf;

    if (fd < 0)
        return 0;

    if (::fstat (fd, &StatBuf) < 0)
        return -1;

    return StatBuf.st_size;
}


void SimpleWAL::LogMessage (const char *pszMessage)
{
    if (NULL == pszMessage)
        return;

    if (m_LogLevel)
    {
        fprintf (stderr, "%s\n", pszMessage);
    }
}

SimpleWAL::SimpleWAL ()
{
    m_fd = -1;
    m_PendingReplay = false;
}


bool SimpleWAL::Init (const std::string& Path)
{
    SetWalFile (Path);

    m_fd = ::open (m_WalPath.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);

    if (m_fd < 0)
    {
        perror ("open wal failed");
        return false;
    }

    if (get_file_size (m_fd) > 0)
        m_PendingReplay = true;
    else
        m_PendingReplay = false;

    return true;
}


SimpleWAL::~SimpleWAL()
{
    bool bRemoveFiles = false;

    if (m_fd >= 0)
    {
        if (get_file_size (m_fd) <= 0)
        {
            bRemoveFiles = true;
        }

        m_PendingReplay = false;

        ::close (m_fd);
        m_fd = -1;
    }

    if (bRemoveFiles)
    {
        ::unlink (m_WalPath.c_str());
        ::unlink (m_CommitPath.c_str());
    }
}


/* --- Public functions --- */


bool SimpleWAL::IsReplayPending()
{
   std::lock_guard<std::mutex> lock (m_mutex);
   return m_PendingReplay;
}


bool SimpleWAL::Append (const void* pData, uint32_t Len)
{
    if (0 == Len)
        return true;

    if (m_fd < 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock (m_mutex);

        if (false == WriteAll (&Len, sizeof (Len)))
        {
            return false;
        }

        if (false == WriteAll (pData, Len))
        {
            return false;
        }

        if (m_bCommit)
        {
            ::fsync (m_fd);
        }

        m_PendingReplay = true;
    }

    return true;
}


bool SimpleWAL::ReplaySingleCommit (const std::function<bool (const std::vector<uint8_t>&)>& Consume)
{
    uint64_t Offset = 0;
    int fd = -1;

    std::lock_guard<std::mutex> lock (m_mutex);

    Offset = LoadCommit();

    fd = ::open (m_WalPath.c_str(), O_RDONLY);

    if (fd < 0)
    {
        goto Done;
    }

    if (::lseek (fd, Offset, SEEK_SET) < 0)
    {
        perror ("Cannot seek WAL file");
        goto Done;
    }

    while (true)
    {
        uint32_t Len;
        ssize_t ReadBytes = ::read (fd, &Len, sizeof (Len));

        if (ReadBytes == 0)
            break;

        if (ReadBytes != sizeof (Len))
            break;

        std::vector<uint8_t> Buffer (Len);

        ReadBytes = ::read (fd, Buffer.data(), Len);

        if (ReadBytes != static_cast<ssize_t> (Len))
        {
            break;
        }

        if (!Consume (Buffer))
        {
            goto Done;
        }

        Offset += sizeof (Len) + Len;
        StoreCommit (Offset);
    }

Done:

    if (fd >= 0)
    {
        ::close (fd);
        fd = -1;
    }

    return true;
}


bool SimpleWAL::Replay (const std::function<bool (const std::vector<uint8_t>&)>& Consume)
{
    uint64_t Offset = 0;
    uint64_t NewOffset = 0;
    int fd = -1;
    bool bDidReplay = false;
    bool bEmpty     = false;

    std::lock_guard<std::mutex> lock (m_mutex);

    Offset = LoadCommit();
    NewOffset = Offset;
    
    fd = ::open (m_WalPath.c_str(), O_RDONLY);

    if (fd < 0)
        return false;

    ::lseek (fd, Offset, SEEK_SET);

    while (true)
    {
        uint32_t Len = 0;
        ssize_t ReadBytes = ::read (fd, &Len, sizeof (Len));

        if (0 == Len)
        {
            bEmpty = true;
            break;
        }

        if (ReadBytes != sizeof (Len))
        {
            break;
        }

        std::vector<uint8_t> Buffer (Len);
        ReadBytes = ::read (fd, Buffer.data(), Len);

        if (ReadBytes != static_cast<ssize_t> (Len))
        {
            break;
        }

        if (!Consume (Buffer))
        {
            break;
        }

        NewOffset += sizeof (Len) + Len;
        bDidReplay = true;
    }

    ::close (fd);

    if (bDidReplay)
    {

        if (bEmpty)
        {
            ClearInternal();
        }
        else
        {
            StoreCommit (NewOffset);
        }
    }

    return bDidReplay;
}


bool SimpleWAL::Clear()
{
    std::lock_guard<std::mutex> lock (m_mutex);

    return ClearInternal();
}


/* --- Private functions --- */


bool SimpleWAL::ClearInternal()
{
    if (m_fd < 0)
        return true;

    if (::ftruncate (m_fd, 0) < 0)
    {
        perror ("file truncate failed");
        return false;
    }

    if (::lseek (m_fd, 0, SEEK_SET) < 0)
    {
        perror ("file seek failed");
        return false;
    }

    m_PendingReplay = false;

    ::unlink (m_CommitPath.c_str());

    LogMessage ("WAL reset");

    return true;
}


bool SimpleWAL::WriteAll (const void* pBuf, size_t Len)
{
    const uint8_t* pPtr = static_cast<const uint8_t*> (pBuf);

    if (m_fd < 0)
        return false;

    while (Len > 0)
    {
        ssize_t Written = ::write (m_fd, pPtr, Len);
        
        if (Written <= 0)
        {
            perror ("Cannot write to WAL");
            return false;
        }

        pPtr += Written;
        Len -= Written;
    }

    return true;
}

uint64_t SimpleWAL::LoadCommit()
{
    int fdCommit = ::open (m_CommitPath.c_str(), O_RDONLY);

    if (fdCommit < 0)
        return 0;

    uint64_t Offset = 0;
    ssize_t len = ::read (fdCommit, &Offset, sizeof (Offset));

    ::close (fdCommit);

    if (len >=0)
        return Offset;
    else
        return 0;
}


bool SimpleWAL::StoreCommit (uint64_t Offset)
{
    int fdCommit = ::open (m_CommitPath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);

    if (fdCommit < 0)
    {
        perror ("open commit failed");
        return false;
    }

    ssize_t len = ::write (fdCommit, &Offset, sizeof (Offset));

    if (m_bCommit)
    {
        ::fsync (fdCommit);
    }

    ::close (fdCommit);
    fdCommit = -1;

    if (len < 0)
    {
        return false;
    }

    return true;
}
