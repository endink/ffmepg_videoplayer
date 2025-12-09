// Copyright (c) 2025 Anders Xiao. All rights reserved.
// https://github.com/endink


#include "commons.h"
#include "ffmepg_context.h"
#include "video_stream.h"
#include "format_converter.h"
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm> // clamp

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/avutil.h>
}

const size_t kCustomIoBufferSize = 32 * 1024;
const size_t kInitialPcmBufferSize = 128 * 1024;

struct VideoFrame {
	int Width = 0;
	int Height = 0;
	int Rotation = 0;
	double TimeMills = 0;
	AVFrame* AvFrame = nullptr;
	FFmpegContext* Context = nullptr;
};

struct VideoPlayer
{
	// public API visible fields
	std::string filename;
	std::unique_ptr<FFmpegContext> Context;
	std::unique_ptr<VideoInfo> VideoInfo;
	std::unique_ptr<IVideoStream> IO;
	VideoPlayerOptions Options;
	int64_t FirstFrameTime = 0;
	std::unique_ptr<FormatConverter> FormatConverter;
	std::vector<uint8_t> FrameData;

	// thread-safe state
	std::atomic<int64_t> CurrentTimeMills{ 0 };

	// single mutex protecting shared mutable state (worker, context lifecycle, IO, etc.)
	std::mutex Mutex;

	std::atomic<bool> IsRunning{ false };
	std::thread Worker;
	void* UserData = nullptr;

	// helper fields
	int64_t StartWallClockUS = 0; // used for pts->wallclock sync

	void LoopPlay();

	// helper to atomically stop thread and extract worker for joining (no join inside lock)
	std::thread StopAndExtractWorker()
	{
		std::thread tmp;
		// acquire lock to safely change state and move thread object out
		std::lock_guard<std::mutex> lock(Mutex);
		bool wasRunning = IsRunning.exchange(false);
		if (wasRunning && Worker.joinable()) {
			tmp = std::move(Worker);
		}
		return tmp;
	}
};

static inline int64_t ff_get_best_effort_timestamp(const AVFrame* frame)
{
	if (!frame) return AV_NOPTS_VALUE;

	// prefer best_effort_timestamp if present
#if defined(HAVE_AVFRAME_BEST_EFFORT_TIMESTAMP) || 1
	if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
		return frame->best_effort_timestamp;
#endif

	if (frame->pts != AV_NOPTS_VALUE)
		return frame->pts;

	if (frame->pkt_dts != AV_NOPTS_VALUE)
		return frame->pkt_dts;

	return AV_NOPTS_VALUE;
}

/* -----------------------
   Frame helpers (unchanged)
   ----------------------- */
VP_API void GetFrameInfo(const VideoFrame* frame, VideoFrameInfo* out_info) {
	if (frame && out_info) {
		out_info->TimeMills = frame->TimeMills;
		out_info->SizeInBytes = frame->Width * frame->Height * 4;
		out_info->Width = frame->Width;
		out_info->Height = frame->Height;
		if (frame->AvFrame) {
			switch (frame->AvFrame->format)
			{
			case AVPixelFormat::AV_PIX_FMT_RGBA:
				out_info->Format = VideoFrameFormat::VIDEO_FRAME_RGBA;
				break;
			case AVPixelFormat::AV_PIX_FMT_BGRA:
				out_info->Format = VideoFrameFormat::VIDEO_FRAME_BGRA;
				break;
			default:
				out_info->Format = VideoFrameFormat::VIDEO_FRAME_UNKNWON;
				break;
			}
		}
	}
}

VP_API void GetFrameData(const VideoFrame* frame, uint8_t* dist_data) {
	if (frame && dist_data) {
		CopyRgbaData(frame->AvFrame, dist_data, frame->Width, frame->Height, frame->Rotation);
	}
}
/* -----------------------
   IO callbacks (unchanged)
   ----------------------- */
int ReadCallback(void* opaque, uint8_t* data, int len) {
	if (opaque == nullptr || data == nullptr || len <= 0) return -1;
	VideoPlayer* player = static_cast<VideoPlayer*>(opaque);
	if (player->IO) {
		return player->IO->Read(data, len);
	}
	return -1;
}

int64_t SeekCallback(void* opaque, int64_t offset, int whence) {
	if (opaque == nullptr) return -1;
	VideoPlayer* player = static_cast<VideoPlayer*>(opaque);
	if (player->IO) {
		return player->IO->Seek(offset, whence);
	}
	return -1;
}

/* -----------------------
   Decode / Process frame
   ----------------------- */
VideoPlayerErrorCode processDecodedVideoFrame(VideoPlayer* player, AVFrame* frame) {
	VideoPlayerErrorCode ret = VideoPlayerErrorCode::kErrorCode_Success;
	double pts_sec = 0.0;
	if (!player || !frame) return VideoPlayerErrorCode::kErrorCode_Invalid_Param;

	if (player->Context == nullptr || player->Context->videoStreamIdx < 0) {
		return VideoPlayerErrorCode::kErrorCode_Invalid_Param;
	}

	// get pts in stream timebase
	int64_t pts = ff_get_best_effort_timestamp(frame);
	if (pts == AV_NOPTS_VALUE) pts = 0;

	pts_sec = pts * av_q2d(player->Context->avformatContext->streams[player->Context->videoStreamIdx]->time_base);

	AVFrame* avFrame = frame;
	if ((avFrame->format != AV_PIX_FMT_RGBA && avFrame->format != AV_PIX_FMT_BGRA) || player->Options.FrameScale != 1.0f) {
		player->FormatConverter->Convert(frame);
		avFrame = player->FormatConverter->convertedFrame;
	}

	int rotate = 0 - player->Context->videoRotation;
	rotate = rotate >= 0 ? rotate : 360 + rotate;

	VideoFrame vf;
	vf.AvFrame = avFrame;
	vf.Height = player->Context->actualFrameHeight;
	vf.Width = player->Context->actualFrameWidth;
	vf.Rotation = rotate;
	vf.Context = player->Context.get();
	vf.TimeMills = (int64_t)(pts_sec * 1000);

	if (player->Options.FrameCallback) {
		// callback executed on decode thread - user must ensure callback is safe
		player->Options.FrameCallback(&vf, player->UserData);
	}

	return ret;
}

/* -----------------------
   LoopPlay - decode thread
   ----------------------- */
void VideoPlayer::LoopPlay()
{
	// Early validate: context must be present. If not, exit.
	{
		std::lock_guard<std::mutex> lock(Mutex);
		if (!Context || !Context->avformatContext) {
			LogError("Invalid video player context in LoopPlay");
			IsRunning = false;
			return;
		}
	}

	AVFormatContext* fmt = nullptr;
	AVStream* stream = nullptr;
	AVCodecContext* codecCtx = nullptr;

	// copy pointers under lock to local variables so we can release lock for decoding
	{
		std::lock_guard<std::mutex> lock(Mutex);
		fmt = Context->avformatContext;
		stream = (Context->videoStreamIdx >= 0) ? fmt->streams[Context->videoStreamIdx] : nullptr;
		codecCtx = Context->videoCodecContext;
	}

	if (!fmt || !stream || !codecCtx) {
		LogError("Missing fmt/stream/codec in LoopPlay");
		return;
	}

	int videoIndex = Context->videoStreamIdx;

	// compute initial wallclock baseline (if CurrentTimeMills set, use it)
	int64_t start_time_us = -1;
	{
		int64_t curMs = CurrentTimeMills.load();
		if (curMs > 0) {
			start_time_us = av_gettime() - (curMs * 1000);
		}
		else {
			start_time_us = -1;
		}
	}

	AVPacket* packet = av_packet_alloc();
	auto* frame = av_frame_alloc();

	while (IsRunning.load())
	{
		int read = av_read_frame(fmt, packet);

		if (read == AVERROR_EOF) {
			// loop: seek to start and continue
			av_seek_frame(fmt, videoIndex, 0, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(codecCtx);
			continue;
		}

		if (read < 0) {
			// small sleep to avoid busy loop on error
			av_usleep(1000 * 5);
			continue;
		}

		if (packet->stream_index == videoIndex)
		{
			if (avcodec_send_packet(codecCtx, packet) < 0)
			{
				av_packet_unref(packet);
				continue;
			}

			while (avcodec_receive_frame(codecCtx, frame) == 0)
			{
				int64_t pts = ff_get_best_effort_timestamp(frame);
				if (pts < 0) pts = 0;

				double pts_sec = pts * av_q2d(stream->time_base);
				int64_t pts_us = (int64_t)(pts_sec * 1000000.0);

				if (start_time_us < 0)
				{
					// first frame: set baseline using CurrentTimeMills if present
					int64_t curMs = CurrentTimeMills.load();
					if (curMs > 0) {
						start_time_us = av_gettime() - (curMs * 1000);
					}
					else {
						start_time_us = av_gettime();
					}
				}

				int64_t now_us = av_gettime() - start_time_us;

				// Wait to match pts timeline
				if (pts_us > now_us) {
					av_usleep(pts_us - now_us);
				}

				// update current time (atomic)
				int64_t t = (int64_t)(pts_sec * 1000.0);
				CurrentTimeMills.store(t);
				// process frame (frame callback)
				processDecodedVideoFrame(this, frame);
			}
		}

		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	// free local frame if we allocated it
	if (frame) {
		av_frame_free(&frame);
	}
}

/* -----------------------
   C API functions (Open/Close/Pause/Resume/Seek)
   ----------------------- */

VP_API VideoPlayer* CreateVideoPlayer(void* user_data) {
	auto* player = new VideoPlayer();
	player->UserData = user_data;
	return player;
}

VP_API void DestroyVideoPlayer(VideoPlayer* player) {
	if (player) {
		Close(player);
		delete player;
	}
}

VP_API bool Open(VideoPlayer* player, const char* file, VideoPlayerOptions options)
{
	if (!player || !file) return false;

	// allocate and setup all resources under lock
	{
		std::lock_guard<std::mutex> lock(player->Mutex);

		player->Options = options;

		if (player->Context) {
			LogWarning("Video player already opened.");
			return false;
		}


		// set IO implementation
		if (strncmp(file, "fd://", 5) == 0) {
			int fd = std::atoi(file + 5);
			if (fd < 0) return false;
			player->IO = std::make_unique<VideoFileDescriptorStream>(fd);
			LogInfo("Use file descriptor stream: %s", file);
		}
		else {
			std::string file_str(file);
			player->IO = std::make_unique<VideoFileStream>(file_str);
			LogInfo("Use file stream: %s", file);
		}


		auto ctx = std::make_unique<FFmpegContext>();
		ctx->avformatContext = avformat_alloc_context();

		uint8_t* buffer = reinterpret_cast<uint8_t*>(av_malloc(kCustomIoBufferSize));
		ctx->IoBufferSize = kCustomIoBufferSize;
		ctx->ioContext = avio_alloc_context(
			buffer,
			ctx->IoBufferSize,
			0,
			player,
			ReadCallback,
			nullptr,
			SeekCallback);

		if (ctx->ioContext == nullptr) {
			LogError("avio_alloc_context failed.");

			if (buffer) {
				av_free(buffer);
			}
			return false;
		}

		ctx->avformatContext->pb = ctx->ioContext;
		ctx->avformatContext->flags = AVFMT_FLAG_CUSTOM_IO;

		RTN_FALSE_IF_UNZERO(avformat_open_input(&ctx->avformatContext, NULL, NULL, NULL), "avformat_open_input failed");
		RTN_FALSE_IF_NEGATIVE(avformat_find_stream_info(ctx->avformatContext, NULL), "avformat_find_stream_info failed.");
		
		
		// open codecs - using your helper functions in FFmpegContext
		
		// assume FFmpegContext provides methods to open streams:
		if (!ctx->LoadVideoProperties(true)) {
			LogError("LoadVideoProperties failed");
			return false;
		}

		// open audio only if not muted
		if (!options.Mute) {
			// if fails, just continue without audio
			//(void)ctx->OpenAudioStream();
		}
		else {
			ctx->audioStreamIdx = -1;
		}

		player->Context = std::move(ctx);



		auto video_info = std::make_unique<VideoInfo>();
		player->Context->FillVideoInfo(*video_info);
		player->VideoInfo = std::move(video_info);

		AVPixelFormat srcFmt = AV_PIX_FMT_RGBA;
		AVPixelFormat dstFmt = srcFmt;
		if (player->Context->videoStream) {
			auto* st = player->Context->videoStream;
			srcFmt = static_cast<AVPixelFormat>(st->codecpar->format);
			dstFmt = srcFmt;
			switch (st->codecpar->format) {
			case AV_PIX_FMT_BGRA:
				player->VideoInfo->PixelFormat = VideoFrameFormat::VIDEO_FRAME_BGRA;
				break;
			case AV_PIX_FMT_RGBA:
				player->VideoInfo->PixelFormat = VideoFrameFormat::VIDEO_FRAME_RGBA;
				break;
			default:
				player->VideoInfo->PixelFormat = VideoFrameFormat::VIDEO_FRAME_RGBA;
				dstFmt = AV_PIX_FMT_RGBA;
				break;
			}
		}

		player->FormatConverter = std::make_unique<FormatConverter>(
			player->Context->actualFrameWidth,
			player->Context->actualFrameHeight,
			dstFmt,
			options.FrameScale);

		// set initial playing time to 0
		player->CurrentTimeMills.store(0);
	}

	// start worker thread (not holding lock)
	{
		std::lock_guard<std::mutex> lock(player->Mutex);
		player->IsRunning = true;
		player->Worker = std::thread(&VideoPlayer::LoopPlay, player);
	}

	// notify video info callback outside lock
	if (player->Options.VideoInfoCallback && player->VideoInfo) {
		player->Options.VideoInfoCallback(player->VideoInfo.get(), player->UserData);
	}

	return true;
}

VP_API void Close(VideoPlayer* player)
{
	if (!player) return;

	// stop thread and join outside lock to avoid deadlock
	std::thread tmp = player->StopAndExtractWorker();
	if (tmp.joinable()) tmp.join();

	// now safe to free resources under lock
	std::lock_guard<std::mutex> lock(player->Mutex);

	player->FormatConverter.reset();
	player->VideoInfo.reset();
	player->IO.reset();

	if (player->Context) {
		player->Context.reset();
	}
}

VP_API void Pause(VideoPlayer* player)
{
	if (!player) return;

	// Stop the worker atomically and join outside lock
	std::thread tmp = player->StopAndExtractWorker();
	if (tmp.joinable()) tmp.join();
}

VP_API bool Resume(VideoPlayer* player)
{
	if (!player || !player->Context) return false;

	// start the worker only if it's not running
	std::lock_guard<std::mutex> lock(player->Mutex);
	if (player->IsRunning.load()) return false;

	// adjust start wall clock so time continuity preserved
	player->StartWallClockUS = av_gettime() - (player->CurrentTimeMills.load() * 1000);

	player->IsRunning = true;
	player->Worker = std::thread(&VideoPlayer::LoopPlay, player);
	return true;
}

VP_API bool IsRunning(VideoPlayer* player)
{
	return player && player->IsRunning.load();
}

VP_API int64_t GetPlayingMills(VideoPlayer* player)
{
	return (player != nullptr) ? player->CurrentTimeMills.load() : 0;
}

VP_API int64_t GetDurationMills(VideoPlayer* player)
{
	return (player && player->Context) ? (int64_t)(player->Context->durationInSeconds * 1000) : 0;
}


VP_API bool SeekToPercent(VideoPlayer* player, float percent)
{
	if (!player || !player->Context) return false;

	percent = std::clamp(percent, 0.0f, 1.0f);

	// stop worker and join (without holding the lock while joining)
	std::thread tmp = player->StopAndExtractWorker();
	if (tmp.joinable()) tmp.join();

	// perform seek under lock to ensure exclusive access to context & codecs
	{
		std::lock_guard<std::mutex> lock(player->Mutex);

		AVFormatContext* fmt = player->Context->avformatContext;
		int videoIndex = player->Context->videoStreamIdx;
		if (!fmt || videoIndex < 0) {
			return false;
		}

		// duration is in AV_TIME_BASE units (microseconds)
		int64_t duration = fmt->duration;
		if (duration <= 0) {
			// if duration not available, try stream duration
			if (fmt->streams[videoIndex] && fmt->streams[videoIndex]->duration > 0)
				duration = av_rescale_q(fmt->streams[videoIndex]->duration,
					fmt->streams[videoIndex]->time_base, AV_TIME_BASE_Q);
		}
		if (duration <= 0) return false;

		int64_t target_us = (int64_t)((double)duration * percent);

		// flush decoders to drop any buffered frames
		if (player->Context->videoCodecContext)
			avcodec_flush_buffers(player->Context->videoCodecContext);
		if (player->Context->audioCodecContext)
			avcodec_flush_buffers(player->Context->audioCodecContext);

		// seek by global timestamp (AV_TIME_BASE units)
		int ret = av_seek_frame(fmt, -1, target_us, AVSEEK_FLAG_BACKWARD);
		if (ret < 0) {
			LogError("Seek failed: %s", getAvError(ret));
			return false;
		}

		// set current time millis
		player->CurrentTimeMills.store(target_us / 1000);

		// reset wall clock baseline so LoopPlay uses correct timing on resume
		player->StartWallClockUS = av_gettime() - target_us;
	}

	// restart worker if needed
	{
		std::lock_guard<std::mutex> lock(player->Mutex);
		if (!player->IsRunning.load()) {
			player->IsRunning = true;
			player->Worker = std::thread(&VideoPlayer::LoopPlay, player);
		}
	}

	return true;
}
