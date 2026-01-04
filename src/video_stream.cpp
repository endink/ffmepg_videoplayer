// Copyright (c) 2025 Anders Xiao.
// https://github.com/endink

#include "video_stream.h"
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#include <io.h>
#define lseek64  _lseeki64
#define off64_t  __int64
typedef int ssize_t;
#else
#include <unistd.h>
#include <fcntl.h>
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
        LogDebug("File stream closed.");
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

bool VideoFileStream::Seekable()
{
    return true;
}



// =============================================================
//  VideoFileDescriptorStream - 基于 POSIX FD 的输入（兼容 Android）
// =============================================================
// =============================================================
//  VideoFileDescriptorStream - 基于 POSIX FD 的输入（兼容 Android）
// =============================================================
VideoFileDescriptorStream::VideoFileDescriptorStream(int file_descriptor)
    : fd(file_descriptor),
    file_size(-1),
    read_position(0),
    seekable(true),
    valid(false)
{
    LogInfo("Creating VideoFileDescriptorStream with fd=%d", fd);

    if (fd < 0) {
        LogError("Invalid file descriptor: %d", fd);
        return;
    }

#ifndef _WIN32
    // 1. 测试文件描述符是否有效
    // 使用 fcntl 检查 fd 状态（这是最可靠的方法）
    if (fcntl(fd, F_GETFD) < 0) {
        int err = errno;
        if (err == EBADF) {
            LogError("File descriptor %d is invalid (EBADF)", fd);
        }
        else {
            LogError("File descriptor %d check failed (errno=%d: %s)",
                fd, err, strerror(err));
        }
        return;
    }
#endif

    valid = true;
    LogInfo("FD %d is valid", fd);

    // 2. 测试是否可 seek - 改进版本
    // 不仅要能获取当前位置，还要能前后移动

    // 2.1 首先尝试获取当前位置
    off64_t original_pos = lseek64(fd, 0, SEEK_CUR);
    if (original_pos < 0) {
        // 无法获取当前位置，肯定是不可 seek 的
        if (errno == ESPIPE) {
            LogInfo("FD %d is not seekable (pipe/socket/FIFO) - cannot get current position", fd);
        }
        else {
            LogWarning("lseek64(0, SEEK_CUR) failed on FD %d with errno=%d: %s",
                fd, errno, strerror(errno));
        }
        seekable = false;

        // 对于不可 seek 的流，尝试获取文件大小（如果有其他方法）
        // 但对于管道等，通常没有大小概念
        file_size = -1;
        read_position = 0;

        LogInfo("VideoFileDescriptorStream created: fd=%d, valid=%s, seekable=%s, size=unknown",
            fd, valid ? "true" : "false", seekable ? "true" : "false");
        return;
    }

    LogDebug("FD %d initial position: %lld", fd, (long long)original_pos);

    // 2.2 测试向前移动一小段距离
    const off64_t test_offset = 1024;  // 测试1KB的移动
    off64_t forward_pos = lseek64(fd, test_offset, SEEK_CUR);
    if (forward_pos < 0) {
        // 向前移动失败，可能已到文件末尾或不可向前移动
        int err = errno;
        if (err == ESPIPE || err == EINVAL) {
            LogInfo("FD %d cannot move forward (errno=%d: %s)", fd, err, strerror(err));
            seekable = false;
        }
        else {
            LogWarning("Forward seek test failed on FD %d with errno=%d: %s",
                fd, err, strerror(err));
            // 继续测试，但不标记为可 seek
            seekable = false;
        }

        // 尝试恢复到原始位置
        off64_t restored = lseek64(fd, original_pos, SEEK_SET);
        if (restored < 0) {
            LogWarning("Failed to restore position after forward seek test on FD %d", fd);
            // 位置丢失，但仍然可以继续
            read_position = original_pos;  // 记录我们期望的位置
        }
        else {
            read_position = original_pos;
        }

        LogInfo("VideoFileDescriptorStream created: fd=%d, valid=%s, seekable=%s, size=unknown",
            fd, valid ? "true" : "false", seekable ? "true" : "false");
        return;
    }

    LogDebug("FD %d moved forward to: %lld", fd, (long long)forward_pos);

    // 2.3 测试向后移动回原始位置
    off64_t backward_pos = lseek64(fd, original_pos, SEEK_SET);
    if (backward_pos < 0) {
        // 向后移动失败，无法精确定位
        int err = errno;
        LogInfo("FD %d cannot move backward to original position (errno=%d: %s)",
            fd, err, strerror(err));
        seekable = false;

        // 尝试向前移动回 forward_pos
        off64_t current_pos = lseek64(fd, 0, SEEK_CUR);
        LogDebug("Current position after backward seek failure: %lld", (long long)current_pos);

        // 我们已经不知道准确位置了
        read_position = current_pos >= 0 ? current_pos : forward_pos;

        LogInfo("VideoFileDescriptorStream created: fd=%d, valid=%s, seekable=%s, size=unknown",
            fd, valid ? "true" : "false", seekable ? "true" : "false");
        return;
    }

    LogDebug("FD %d moved backward to original position: %lld", fd, (long long)backward_pos);

    // 2.4 测试获取文件大小（向前移动到末尾，再返回）
    // 注意：这个操作可能改变位置，但我们已经证明可以精确定位
    off64_t end_pos = lseek64(fd, 0, SEEK_END);
    if (end_pos >= 0) {
        file_size = end_pos;
        LogInfo("FD %d file size: %lld bytes", fd, (long long)file_size);

        // 返回原始位置
        off64_t final_pos = lseek64(fd, original_pos, SEEK_SET);
        if (final_pos >= 0) {
            read_position = original_pos;
            seekable = true;  // 通过了所有测试：可获取位置、可前后移动、可获取大小
            LogInfo("FD %d passed all seekability tests", fd);
        }
        else {
            LogWarning("FD %d failed to return to original position after size check", fd);
            seekable = true;  // 虽然最后一步失败，但之前都成功了
            read_position = end_pos;  // 当前位置在文件末尾
        }
    }
    else {
        // 无法获取文件大小，但仍然可能是可 seek 的
        LogInfo("FD %d cannot determine file size, but appears to be seekable", fd);

        // 尝试返回原始位置
        off64_t final_pos = lseek64(fd, original_pos, SEEK_SET);
        if (final_pos >= 0) {
            read_position = original_pos;
            seekable = true;  // 可以前后移动，只是不知道大小
        }
        else {
            // 无法精确定位，但之前的前后移动测试通过了
            off64_t current_pos = lseek64(fd, 0, SEEK_CUR);
            read_position = current_pos >= 0 ? current_pos : original_pos;
            seekable = true;  // 保守起见，标记为可 seek
            LogWarning("FD %d cannot return to exact position, but seek operations work", fd);
        }
    }

    // 2.5 额外的保守性测试：如果文件大小为0，可能不是真正的可 seek
    if (file_size == 0) {
        LogWarning("FD %d has zero file size, may not be a real seekable file", fd);
        // 对于零字节文件，seek 可能没有意义，但我们仍然标记为可 seek
    }

    LogInfo("VideoFileDescriptorStream created: fd=%d, valid=%s, seekable=%s, size=%lld, current_pos=%lld",
        fd, valid ? "true" : "false", seekable ? "true" : "false",
        (long long)file_size, (long long)read_position);
}

VideoFileDescriptorStream::~VideoFileDescriptorStream()
{
    //if (fd > 0) {
    //    if (close(fd) == 0) {
    //        LogDebug("File descriptor %d closed successfully.\n", fd);
    //    }
    //    else {
    //        // 根据errno判断错误类型
    //        if (errno == EBADF) {
    //            LogError("Error: File descriptor %d is invalid.\n", fd);
    //        }
    //        else if (errno == EINTR) {
    //            LogError("Error: Close was interrupted by a signal. You may retry.\n");
    //        }
    //        else {
    //            LogError("Error: Failed to close file descriptor %d.\n", fd);
    //        }
    //    }
    //    valid = false;
    //}
}

bool VideoFileDescriptorStream::Seekable()
{
    if (!valid) {
        LogDebug("VideoFileDescriptorStream::Seekable() -> false (invalid stream)");
        return false;
    }

    bool result = seekable;
    LogDebug("VideoFileDescriptorStream::Seekable() -> %s", result ? "true" : "false");
    return result;
}

int VideoFileDescriptorStream::Read(uint8_t* data, int len)
{
    if (!valid) {
        LogError("Read attempted on invalid stream (fd=%d)", fd);
        return AVERROR(EBADF);
    }

    if (!data) {
        LogError("Read attempted with null data pointer (fd=%d)", fd);
        return AVERROR(EINVAL);
    }

    if (len <= 0) {
        LogError("Read attempted with invalid length %d (fd=%d)", len, fd);
        return AVERROR(EINVAL);
    }

    LogDebug("Reading %d bytes from fd=%d (current pos=%lld)", len, fd, read_position);

    ssize_t n;
    do {
        n = ::read(fd, data, len);
    } while (n < 0 && errno == EINTR);  // 处理信号中断，自动重试

    if (n < 0) {
        int err = errno;
        LogError("read failed on fd=%d, errno=%d: %s", fd, err, strerror(err));

        // 检查是否文件描述符变为无效
        if (err == EBADF) {
            valid = false;
            LogError("File descriptor %d became invalid during read", fd);
        }
        else if (err == EAGAIN || err == EWOULDBLOCK) {
            LogWarning("read would block on fd=%d (non-blocking mode?)", fd);
        }

        return AVERROR(err);
    }

    if (n == 0) {
        LogDebug("EOF reached for fd=%d after reading %lld bytes total", fd, read_position);
    }
    else {
        LogDebug("Read %zd bytes from fd=%d", n, fd);
    }

    read_position += n;
    return static_cast<int>(n);
}

int64_t VideoFileDescriptorStream::Seek(int64_t offset, int whence)
{
    if (!valid) {
        LogError("Seek attempted on invalid stream (fd=%d)", fd);
        return AVERROR(EBADF);
    }

    // 请求文件大小 - 总是处理这个请求，即使不可 seek
    if (whence == AVSEEK_SIZE) {
        LogDebug("AVSEEK_SIZE request for fd=%d -> %lld", fd, file_size);
        return file_size;  // 可能是 -1，表示未知
    }

    // 若不可 seek，告诉 FFmpeg 不支持 seek 操作
    if (!seekable) {
        LogDebug("Seek request on non-seekable fd=%d (offset=%lld, whence=%d) -> ENOSYS",
            fd, offset, whence);
        return AVERROR(ENOSYS);  // 表示操作不被支持
    }

    // 记录原始的 whence 名称用于日志
    const char* whence_str = "UNKNOWN";
    switch (whence) {
    case SEEK_SET: whence_str = "SEEK_SET"; break;
    case SEEK_CUR: whence_str = "SEEK_CUR"; break;
    case SEEK_END: whence_str = "SEEK_END"; break;
    }

    LogDebug("Seeking fd=%d to offset=%lld, whence=%s (current=%lld)",
        fd, offset, whence_str, read_position);

    off64_t new_pos;
    switch (whence) {
    case SEEK_SET:
        new_pos = lseek64(fd, offset, SEEK_SET);
        break;
    case SEEK_CUR:
        new_pos = lseek64(fd, offset, SEEK_CUR);
        break;
    case SEEK_END:
        new_pos = lseek64(fd, offset, SEEK_END);
        break;
    default:
        LogError("Invalid whence value: %d", whence);
        return AVERROR(EINVAL);
    }

    if (new_pos < 0) {
        int err = errno;
        LogError("lseek64 failed on fd=%d, errno=%d: %s", fd, err, strerror(err));

        if (err == EBADF) {
            valid = false;
            LogError("File descriptor %d became invalid during seek", fd);
        }
        else if (err == EINVAL) {
            LogError("Invalid seek parameters for fd=%d (offset=%lld, whence=%d)",
                fd, offset, whence);
        }

        return AVERROR(err);
    }

    read_position = new_pos;
    LogDebug("Seek successful, new position=%lld", read_position);

    return read_position;
}

