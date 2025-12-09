// Copyright (c) 2025 Anders Xiao. All rights reserved.
// https://github.com/endink

#pragma once
#include "videoplayer_c_api.h"
#include <string>
#include <chrono>

// 平台检测宏
#ifdef _WIN32
#include <windows.h>
#define PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#include <android/log.h>
#define PLATFORM_ANDROID 1
#else
#define PLATFORM_UNKNOWN 1
#endif

extern "C" {
	#include <libavutil/frame.h>
}


enum class VideoPlayerErrorCode {
	kErrorCode_Success = 0,
	kErrorCode_Invalid_Param,
	kErrorCode_Invalid_State,
	kErrorCode_Invalid_Data,
	kErrorCode_Invalid_Format,
	kErrorCode_Decoder_Already_Existed,
	kErrorCode_NULL_Pointer,
	kErrorCode_Open_File_Error,
	kErrorCode_Eof,
	kErrorCode_FFmpeg_Error,
	kErrorCode_Old_Frame,
	Cancelled,
};

inline bool IsError(VideoPlayerErrorCode code) {
	return code != VideoPlayerErrorCode::Cancelled &&
		code != VideoPlayerErrorCode::kErrorCode_Success &&
		code != VideoPlayerErrorCode::kErrorCode_Eof &&
		code != VideoPlayerErrorCode::kErrorCode_Old_Frame;
}

void SimpleLog(VideoPlayerLogLevel level, const char* format, ...);

template<typename... Args>
inline void LogError(const char* format, Args... args)
{
    SimpleLog(VIDEO_PLAYER_LOG_ERROR, format, args...);
}

template<typename... Args>
inline void LogWarning(const char* format, Args... args)
{
    SimpleLog(VIDEO_PLAYER_LOG_WARNING, format, args...);
}

template<typename... Args>
inline void LogInfo(const char* format, Args... args)
{
    SimpleLog(VIDEO_PLAYER_LOG_INFO, format, args...);
}

template<typename... Args>
inline void LogDebug(const char* format, Args... args)
{
    SimpleLog(VIDEO_PLAYER_LOG_DEBUG, format, args...);
}

#define CALL_DEBUG(Var, Result, VerboseCondtion, VerboseMessage) \
Var = (Result); \
if(Var VerboseCondtion) \
{ \
    LogDebug(VerboseMessage); \
} 

#define CALL_DEBUG_IF_ZERO(Var, Result, VerboseMessage) \
CALL_DEBUG(Var, Result, == 0, VerboseMessage)

#define CALL_DEBUG_IF_POSITIVE(Var, Result, VerboseMessage) \
CALL_DEBUG(Var, Result, >= 0, VerboseMessage)


#define CALL_DEBUG_IF_TRUE(Var, Result, VerboseMessage) \
CALL_DEBUG(Var, Result, == true, VerboseMessage)

const char* getAvError(int error_code);

#define RETURN_IF_ERROR(Result, Condition, ReturnValue, ErrorMessage, VerboseMessage) \
do \
{ \
	auto __c = (Result); \
    if(__c Condition) \
    { \
        LogError("%s (%s, error: %s).",  ErrorMessage, __FUNCTION__, getAvError(__c)); \
        return ReturnValue; \
    } \
	auto __verbose = VerboseMessage; \
	if(__verbose != nullptr) \
	{ \
		LogDebug(__verbose); \
	} \
} while (0)

#define RTN_NULL_IF_UNZERO(Result, ErrorMessage) RETURN_IF_ERROR(Result, != 0, nullptr, ErrorMessage, nullptr)

#define RTN_NULL_IF_UNZERO_VERBOSE(Result, ErrorMessage, VerboseMessage) RETURN_IF_ERROR(Result, != 0, nullptr, ErrorMessage, nullptr)

#define RTN_NULL_IF_NEGATIVE(Result, ErrorMessage) RETURN_IF_ERROR(Result, < 0, nullptr, ErrorMessage, nullptr)

#define RTN_NULL_IF_NEGATIVE_VERBOSE(Result, ErrorMessage, VerboseMessage) RETURN_IF_ERROR(Result, < 0, nullptr, ErrorMessage, nullptr)


#define RTN_FALSE_IF_UNZERO(Result, ErrorMessage) RETURN_IF_ERROR(Result, != 0, false, ErrorMessage, nullptr)

#define RTN_FALSE_IF_UNZERO_VERBOSE(Result, ErrorMessage, VerboseMessage) RETURN_IF_ERROR(Result, != 0, false, ErrorMessage, nullptr)

#define RTN_FALSE_IF_NEGATIVE(Result, ErrorMessage) RETURN_IF_ERROR(Result, < 0, false, ErrorMessage, nullptr)

#define RTN_FALSE_IF_NEGATIVE_VERBOSE(Result, ErrorMessage, VerboseMessage) RETURN_IF_ERROR(Result, < 0, false, ErrorMessage, nullptr)


VideoPlayerErrorCode CopyRgbaDataRotated(AVFrame* frame, uint8_t* distBuffer, int width, int height, int rotate);

static inline int RoundUp(int numToRound, int multiple) {
	return (numToRound + multiple - 1) & -multiple;
}


static inline int64_t GetTimestampMills()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(
		system_clock::now().time_since_epoch()
	).count();
}