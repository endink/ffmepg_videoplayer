#include "video_stream.h"
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#include <io.h>
#define lseek64  _lseeki64
#define off64_t  __int64
#else
#include <unistd.h>
#define lseek64  lseek
#define off64_t  off_t
#endif

// ==========================================
// VideoFileStream（C++ fstream）
// ==========================================
VideoFileStream::VideoFileStream(const std::string& video_file)
    : file(video_file)
{
    input.open(file, std::ios::binary);
    if (input.is_open()) {
        input.seekg(0, std::ios::end);
        file_size = static_cast<int64_t>(input.tellg());
        input.seekg(0, std::ios::beg);
        read_position = 0;
    }
}

VideoFileStream::~VideoFileStream()
{
    if (input.is_open()) {
        input.close();
    }
}

int VideoFileStream::Read(uint8_t* data, int len)
{
    if (!input.is_open() || !data || len <= 0)
        return -1;

    int64_t remaining = file_size - read_position;
    if (remaining <= 0)
        return 0;

    int to_read = static_cast<int>(std::min<int64_t>(len, remaining));

    input.read(reinterpret_cast<char*>(data), to_read);
    int read_bytes = static_cast<int>(input.gcount());

    read_position += read_bytes;
    return read_bytes;
}

int64_t VideoFileStream::Seek(int64_t offset, int whence)
{
    if (!input.is_open())
        return -1;

    std::ios::seekdir dir;
    switch (whence) {
    case SEEK_SET: dir = std::ios::beg; break;
    case SEEK_CUR: dir = std::ios::cur; break;
    case SEEK_END: dir = std::ios::end; break;
    default: return -1;
    }

    input.seekg(offset, dir);
    if (input.fail())
        return -1;

    read_position = static_cast<int64_t>(input.tellg());
    return read_position;
}

// ==========================================
// VideoFileDescriptorStream（FD-based）
// ==========================================
VideoFileDescriptorStream::VideoFileDescriptorStream(int file_descriptor)
    : fd(file_descriptor)
{
    if (fd >= 0) {
        off64_t cur = lseek64(fd, 0, SEEK_CUR);
        if (cur != -1) {
            off64_t end = lseek64(fd, 0, SEEK_END);
            if (end != -1) {
                file_size = end;
            }
            lseek64(fd, cur, SEEK_SET);
        }
    }
}

int VideoFileDescriptorStream::Read(uint8_t* data, int len)
{
    if (fd < 0 || !data || len <= 0)
        return -1;

    size_t n = read(fd, data, len);
    if (n < 0)
        return -1;

    read_position += n;
    return static_cast<int>(n);
}

int64_t VideoFileDescriptorStream::Seek(int64_t offset, int whence)
{
    if (fd < 0)
        return -1;

    off64_t r = lseek64(fd, offset, whence);
    if (r < 0)
        return -1;

    read_position = r;
    return read_position;
}
