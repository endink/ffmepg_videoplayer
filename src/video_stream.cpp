// Copyright (c) 2025 Anders Xiao.
// https://github.com/endink

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

#include <libavformat/avio.h>
#include "commons.h"

// =============================================================
//  VideoFileStream - 基于 std::ifstream 的文件输入
// =============================================================
VideoFileStream::VideoFileStream(const std::string& video_file)
    : file(video_file)
{
#ifdef _WIN32
    // UTF-8 转宽字符
    int wlen = MultiByteToWideChar(CP_UTF8, 0, video_file.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        LogError("Failed to convert UTF-8 path to wide string: %s", video_file.c_str());
        return;
    }

    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, video_file.c_str(), -1, &wpath[0], wlen);

    input.open(wpath, std::ios::binary);
#else
    // 非 Windows 使用原来的 std::ifstream
    input.open(video_file, std::ios::binary);
#endif
    if (!input.is_open()) {
        LogError("Failed to open file: %s", video_file.c_str());
        return;
    }

    input.clear(); // 清除 failbit/eofbit
    input.seekg(0, std::ios::end);
    std::streampos end_pos = input.tellg();
    if (end_pos >= 0)
        file_size = static_cast<int64_t>(end_pos);
    else
        LogError("Failed to get file size: %s", video_file.c_str());

    input.seekg(0, std::ios::beg);

    LogInfo("Video file stream size: %lld bytes .", file_size);
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

    if (input.eof())
        return 0;

    input.read(reinterpret_cast<char*>(data), len);
    std::streamsize read_bytes = input.gcount();

    if (read_bytes < 0)
        return -1;

    return static_cast<int>(read_bytes); // EOF → 0
}

int64_t VideoFileStream::Seek(int64_t offset, int whence)
{
    if (!input.is_open())
        return -1;

    if (whence == AVSEEK_SIZE)
        return file_size;

    input.clear(); // 必须清除 EOF 状态

    switch (whence) {
    case SEEK_SET:
        input.seekg(offset, std::ios::beg);
        break;
    case SEEK_CUR:
        input.seekg(offset, std::ios::cur);
        break;
    case SEEK_END:
        input.seekg(offset, std::ios::end);
        break;
    default:
        return -1;
    }

    if (input.fail())
        return -1;

    return static_cast<int64_t>(input.tellg());
}



// =============================================================
//  VideoFileDescriptorStream - 基于 POSIX FD 的输入（兼容 Android）
// =============================================================
VideoFileDescriptorStream::VideoFileDescriptorStream(int file_descriptor)
    : fd(file_descriptor),
    file_size(-1),
    read_position(0)
{
    if (fd >= 0) {
        // 保存现有偏移
        off64_t cur = lseek64(fd, 0, SEEK_CUR);
        if (cur >= 0) {

            // 尝试获取文件大小（Android FD 若不可 seek，结果会是 -1）
            off64_t end = lseek64(fd, 0, SEEK_END);
            if (end >= 0)
                file_size = end;

            // restore
            off64_t back = lseek64(fd, cur, SEEK_SET);
            if (back >= 0)
                read_position = cur;
        }
    }
}

int VideoFileDescriptorStream::Read(uint8_t* data, int len)
{
    if (fd < 0 || !data || len <= 0)
        return -1;

    auto n = ::read(fd, data, len);
    if (n < 0)
        return -1;

    read_position += n;
    return static_cast<int>(n);
}

int64_t VideoFileDescriptorStream::Seek(int64_t offset, int whence)
{
    if (fd < 0)
        return -1;

    // FFmpeg: 请求获取文件大小
    if (whence == AVSEEK_SIZE)
        return file_size;

    off64_t new_pos = lseek64(fd, offset, whence);
    if (new_pos < 0)
        return -1;

    read_position = new_pos;
    return read_position;
}
