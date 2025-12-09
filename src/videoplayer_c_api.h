#pragma once
#include <stdint.h>

#if defined(_MSC_VER)
#if defined(VIDEO_PLAYER_EXPORTS)
#define VP_API __declspec(dllexport)
#else
#define VP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#if defined(VIDEO_PLAYER_EXPORTS)
#define VP_API __attribute__((visibility("default")))
#else
#define VP_API
#endif
#else
#define VP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif
    typedef struct VideoPlayer VideoPlayer;
    typedef struct VideoFrame  VideoFrame;

    typedef enum VideoPlayerLogLevel {
        VIDEO_PLAYER_LOG_DEBUG = 0,
        VIDEO_PLAYER_LOG_INFO,
        VIDEO_PLAYER_LOG_WARNING,
        VIDEO_PLAYER_LOG_ERROR
    } VideoPlayerLogLevel;

    typedef enum VideoFrameFormat {
        VIDEO_FRAME_UNKNWON = 0,
        VIDEO_FRAME_RGBA,
        VIDEO_FRAME_BGRA
    } VideoFrameFormat;

    typedef void (*VideoPlayerLogCallback)(VideoPlayerLogLevel level, const char* msg);
    typedef void (*AvInfoCallback)(const struct VideoInfo* info, void* user_data);
    typedef void (*FrameCallback)(VideoFrame* frame, void* user_data);

    typedef struct VideoInfo {
        int64_t  DurationMills;
        int64_t  TotalFrames;
        int32_t  VideoWidth;
        int32_t  VideoHeight;
        int32_t  AudioChannels;
        int32_t  AudioSampleRate;
        float    Fps;
        char     VideoCodec[64];  
        int32_t  Rotation;
        double   DecoderFPS;
        uint8_t  HasAudio;        // 0/1
        VideoFrameFormat PixelFormat;
    } VideoInfo;

    typedef struct VideoFrameInfo {
        int32_t Width;
        int32_t Height;
        int32_t SizeInBytes;    
        double  TimeMills;    
        VideoFrameFormat Format; // RGBA / BGRA / unknown
    } VideoFrameInfo;

    typedef struct VideoPlayerOptions {
        uint8_t Mute;            // 0/1
        int64_t StartMills;
        float   FrameScale;
        AvInfoCallback VideoInfoCallback;
        FrameCallback  FrameCallback;
    } VideoPlayerOptions;

    // -----------------------------
    // Video player C API
    // -----------------------------
    VP_API const char* GetLibraryVersion();

    // logger
    VP_API void SetVideoPlayerLogCallback(VideoPlayerLogCallback logger);

    // lifecycle
    VP_API VideoPlayer* CreateVideoPlayer(void* user_data);
    VP_API void DestroyVideoPlayer(VideoPlayer* player);

    // frame helpers
    VP_API void GetFrameInfo(const VideoFrame* frame, VideoFrameInfo* out_info); 
    VP_API void GetFrameData(const VideoFrame* frame, uint8_t* dist_data);

    // player control
    VP_API bool Open(VideoPlayer* player, const char* file_or_fd_uri, VideoPlayerOptions options);
    VP_API void Close(VideoPlayer* player);
    VP_API void Pause(VideoPlayer* player);
    VP_API bool Resume(VideoPlayer* player);
    VP_API bool IsRunning(VideoPlayer* player);
    VP_API int64_t GetPlayingMills(VideoPlayer* player);
    VP_API int64_t GetDurationMills(VideoPlayer* player);
    VP_API bool SeekToPercent(VideoPlayer* player, float percent);

#ifdef __cplusplus
} // extern "C"
#endif
