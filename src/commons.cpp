// Copyright (c) 2025 Anders Xiao. All rights reserved.
// https://github.com/endink

#include "commons.h"

#include <cstdio>      // printf, fprintf, sprintf, vsnprintf
#include <cstdarg>     // va_list, va_start, va_end
#include <ctime>       // struct tm
#include <cstddef>     // nullptr
#include <vector>
extern "C" {
    #include <libavutil/error.h>
}


static VideoPlayerLogCallback g_log_callback = nullptr; 
inline void GetLocalTime(time_t t, struct tm* outTm)
{
#if PLATFORM_WINDOWS
    // Windows 下使用 localtime_s
    localtime_s(outTm, &t);
#else
    // Linux / Android / POSIX 使用 localtime_r
    localtime_r(&t, outTm);
#endif
}

    VP_API const char* GetLibraryVersion()
    {
        // __DATE__ result: Apr 23 2020, Apr232020 is 9 chars
        // 20200423\0 is 9 chars
        static char date_origin_format_buf[9] = { 0 };   // store __DATE__ chars
        int month = 0;               // store month number
        int i = 0;
        if (date_origin_format_buf[0] == 0) {          // can delete
            static const char* static_month_buf[] = {
                "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
            const char* cp_date = __DATE__;

            for (i = 0; i < 4; i++) {
                date_origin_format_buf[i] = *(cp_date + 7 + i);
            }

            for (i = 0; i < 12; i++) { //
                // When _month[i] and cp_date store the same value in memory, month is i+1.
                if ((static_month_buf[i][0] == (cp_date[0])) &&
                    (static_month_buf[i][1] == (cp_date[1])) &&
                    (static_month_buf[i][2] == (cp_date[2]))) {
                    month = i + 1;
                    break;
                }
            }
            date_origin_format_buf[4] = month / 10 % 10 + '0';
            date_origin_format_buf[5] = month % 10 + '0';
            // judge day is little than 10
            if (cp_date[4] == ' ') {
                date_origin_format_buf[6] = '0';
            }
            else {
                date_origin_format_buf[6] = cp_date[4];
            }
            date_origin_format_buf[7] = cp_date[5];
        }
        return date_origin_format_buf;
    }

    VP_API void SetVideoPlayerLogCallback(VideoPlayerLogCallback logger)
    {
        g_log_callback = logger;
    }


void SimpleLog(VideoPlayerLogLevel level, const char* format, ...)
{
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmTime;
#if defined(_WIN32)
    localtime_s(&tmTime, &t);
#else
    localtime_r(&t, &tmTime);
#endif

    char timeBuffer[64];
    snprintf(timeBuffer, sizeof(timeBuffer),
        "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
        tmTime.tm_year + 1900,
        tmTime.tm_mon + 1,
        tmTime.tm_mday,
        tmTime.tm_hour,
        tmTime.tm_min,
        tmTime.tm_sec,
        static_cast<long long>(millis.count()));

    // 格式化日志内容
    char stackBuffer[256];
    va_list args1;
    va_start(args1, format);
    int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), format, args1);
    va_end(args1);

    std::vector<char> buffer;
    if (needed < (int)sizeof(stackBuffer)) {
        buffer.assign(stackBuffer, stackBuffer + needed);
        buffer.push_back(0);
    }
    else {
        buffer.resize(needed + 1);
        va_list args2;
        va_start(args2, format);
        vsnprintf(buffer.data(), buffer.size(), format, args2);
        va_end(args2);
    }

    std::string finalMsg = "[" + std::string(timeBuffer) + "] " + std::string(buffer.data());

    // 回调或默认输出
    if (g_log_callback) {
        g_log_callback(level, finalMsg.c_str());
        return;
    }

    if (level == VIDEO_PLAYER_LOG_WARNING) {
        printf("[Warning] %s\n", finalMsg.c_str());
    }
    else if (level >= VIDEO_PLAYER_LOG_ERROR) {
        fprintf(stderr, "%s\n", finalMsg.c_str());
    }
    else {
        printf("%s\n", finalMsg.c_str());
    }
}


const char* getAvError(int error_code)
{
    static char buf[1024];
    if (av_strerror(error_code, buf, 1024) == 0)
    {
        return buf;
    }
    sprintf(buf, "%d", error_code);
    return buf;
}

VideoPlayerErrorCode CopyRgbaDataRotated(AVFrame* frame, uint8_t* distBuffer, int outWidth, int outHeight, int rotate)
{
    if (!frame || !frame->data[0] || !distBuffer)
        return VideoPlayerErrorCode::kErrorCode_Invalid_Param;

    const uint8_t* src = frame->data[0];
    int srcWidth = frame->width;
    int srcHeight = frame->height;
    int srcStride = frame->linesize[0];
    const int channels = 4;

    for (int y = 0; y < srcHeight; ++y) {
        for (int x = 0; x < srcWidth; ++x) {
            const uint8_t* px = src + y * srcStride + x * channels;

            int dstX, dstY;

            switch (rotate) {
            case 0:
                dstX = x;
                dstY = y;
                break;
            case 90:
                dstX = srcHeight - 1 - y;
                dstY = x;
                break;
            case 180:
                dstX = srcWidth - 1 - x;
                dstY = srcHeight - 1 - y;
                break;
            case 270:
                dstX = y;
                dstY = srcWidth - 1 - x;
                break;
            default:
                return VideoPlayerErrorCode::kErrorCode_Invalid_Param;
            }

            uint8_t* dst = distBuffer + (dstY * outWidth + dstX) * channels;
            memcpy(dst, px, channels);
        }
    }

    return VideoPlayerErrorCode::kErrorCode_Success;
}
