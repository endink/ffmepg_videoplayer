// Copyright (c) 2025 Anders Xiao. All rights reserved.
// https://github.com/endink

#include "ffmepg_context.h"
#include "commons.h" 

extern "C" {
	#include <libavutil/dict.h>
	#include <libavutil/imgutils.h>
	#include <libavutil/display.h>
}

float GetDecoderFPS(FFmpegContext& context)
{
	if (!context.avformatContext || !context.videoCodecContext) {
		return 0.0f;
	}

	// 记录当前的位置（应该为0）
	int64_t initialPos = -1;
	if (context.avformatContext->pb) {
		initialPos = avio_tell(context.avformatContext->pb);
		LogDebug("Initial position before FPS test: %lld", (long long)initialPos);
	}

	// 重置解码器内部状态
	avcodec_flush_buffers(context.videoCodecContext);

	const int targetFrames = 10;   // More accurate
	int frameCount = 0;
	bool hasError = false;

	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	double start = GetTimestampMills();

	while (frameCount < targetFrames && !hasError)
	{
		int ret = av_read_frame(context.avformatContext, packet);
		if (ret == AVERROR_EOF) break;
		if (ret < 0) {
			LogError("av_read_frame failed: %s", getAvError(ret));
			hasError = true;
			break;
		}

		if (packet->stream_index != context.videoStreamIdx) {
			av_packet_unref(packet);
			continue;
		}

		ret = avcodec_send_packet(context.videoCodecContext, packet);
		av_packet_unref(packet);
		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			LogError("avcodec_send_packet failed: %s", getAvError(ret));
			hasError = true;
			break;
		}

		// Try to receive frames
		while (frameCount < targetFrames) {
			ret = avcodec_receive_frame(context.videoCodecContext, frame);

			if (ret == AVERROR(EAGAIN)) break;
			if (ret == AVERROR_EOF) break;
			if (ret < 0) {
				LogError("avcodec_receive_frame failed: %s", getAvError(ret));
				hasError = true;
				break;
			}

			frameCount++;
			av_frame_unref(frame);
		}
	}

	// Drain decoder - 确保所有缓存帧都被处理
	avcodec_send_packet(context.videoCodecContext, nullptr);
	while (avcodec_receive_frame(context.videoCodecContext, frame) == 0) {
		frameCount++;
		av_frame_unref(frame);
	}

	double elapsed = GetTimestampMills() - start;

	av_packet_free(&packet);
	av_frame_free(&frame);

	// 关键部分：恢复到原始位置（应该是0）
	// 不要使用 SeekToStart()，因为它是 const 方法且可能有问题
	// 直接在这里执行 seek 操作

	// 方法1：使用 av_seek_frame
	if (context.avformatContext && context.videoStreamIdx >= 0) {
		int ret = av_seek_frame(context.avformatContext,
			context.videoStreamIdx,
			0,
			AVSEEK_FLAG_BACKWARD);
		if (ret < 0) {
			LogError("Failed to seek to start after FPS test: %s", getAvError(ret));

			// 方法2：如果方法1失败，尝试使用 avformat_seek_file
			ret = avformat_seek_file(context.avformatContext,
				-1,  // 自动选择流
				INT64_MIN,
				0,
				INT64_MAX,
				AVSEEK_FLAG_BACKWARD);
			if (ret < 0) {
				LogError("avformat_seek_file also failed: %s", getAvError(ret));
			}
			else {
				LogDebug("Successfully restored position using avformat_seek_file");
			}
		}
		else {
			LogDebug("Successfully restored position using av_seek_frame");
		}
	}

	// 再次刷新解码器，确保状态干净
	avcodec_flush_buffers(context.videoCodecContext);

	// 如果有音频解码器，也刷新它
	if (context.audioCodecContext) {
		avcodec_flush_buffers(context.audioCodecContext);
	}

	// 验证位置是否正确恢复
	if (context.avformatContext->pb) {
		int64_t currentPos = avio_tell(context.avformatContext->pb);
		LogDebug("Position after restoration: %lld (initial was: %lld)",
			(long long)currentPos, (long long)initialPos);

		// 如果位置差很多，可能需要重新打开流
		if (initialPos >= 0 && abs(currentPos - initialPos) > 1000) {
			LogWarning("Position not properly restored (diff: %lld)",
				(long long)(currentPos - initialPos));
		}
	}

	if (hasError || elapsed <= 0.0 || frameCount == 0)
		return 0.0f;

	float fps = (float)((frameCount * 1000.0) / elapsed);
	LogDebug("Measured FPS: %.2f (frames=%d, elapsed=%.2fms)", fps, frameCount, elapsed);

	return fps;
}



int GetKeyFrameInterval(FFmpegContext& context)
{
	int64_t time = -1;
	AVPacket* packet = av_packet_alloc();
	int keyFramesCount = 0;
	while (keyFramesCount < 3)
	{
		int ret = av_read_frame(context.avformatContext, packet);
		if (ret == AVERROR_EOF)
		{
			break;
		}
		if (ret != 0)
		{
			if (keyFramesCount == 0)
			{
				av_packet_free(&packet);
				LogDebug("Test frame failed (av_read_frame): %s", getAvError(ret));
				return -1;
			}
			break;
		}
		if (packet->stream_index == context.videoStreamIdx)
		{
			bool isKeyFrame = (packet->flags & AV_PKT_FLAG_KEY) == AV_PKT_FLAG_KEY;
			if (packet->pts == AV_NOPTS_VALUE)
			{
				av_packet_free(&packet);
				return -1;
			}
			if (isKeyFrame)
			{
				keyFramesCount++;
				time = packet->pts;
				LogDebug("Check key frame: %.2f (index: %d)", packet->pts * av_q2d(context.timebase), keyFramesCount - 1);
			}
		}
		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	if (keyFramesCount <= 1)
	{
		return -1;
	}
	return time / (keyFramesCount - 1);
}

int GetAvStreamRotateAngle(FFmpegContext& context)
{
	AVDictionaryEntry* tag = NULL;
	tag = av_dict_get(context.videoStream->metadata, "rotate", tag, 0);
	if (tag != nullptr)
	{
		int angle = atoi(tag->value);
		angle %= 360;
		return angle;
	}
	else
	{
		LogInfo("context.videoStream->nb_side_data (%d).", context.videoStream->nb_side_data);
		if (context.videoStream->nb_side_data > 0)
		{
			for (int i = 0; i < context.videoStream->nb_side_data; i++)
			{
				auto& data = context.videoStream->side_data[i];
				if (data.type == AV_PKT_DATA_DISPLAYMATRIX)
				{
					auto value = av_display_rotation_get(reinterpret_cast<const int32_t*>(data.data));
					auto angle = ((int)round(value)) % 360;
					return angle;
				}
			}
		}
	}
	return 0;
}

bool FFmpegContext::LoadVideoProperties(bool testDeocderFPS)
{
	videoStreamIdx = -1;
	for (unsigned int i = 0; i < avformatContext->nb_streams; ++i) {
		AVStream* stream = avformatContext->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStreamIdx = i;
			videoStream = stream;
			break; 
		}
	}


	if (videoStreamIdx < 0) {
		LogError("No video stream found");
		return false;
	}
	else {
		LogInfo("Video stream index: %d", videoStreamIdx);
	}


	AVCodecParameters* codecpar = videoStream->codecpar;
	const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
	if (!codec) {
		LogError("Failed to find decoder for codec id %d", codecpar->codec_id);
		return false;
	}

	AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		LogError("Failed to allocate AVCodecContext");
		return false;
	}

	// 将流参数拷贝到 codec context
	if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
		LogError("Failed to copy codec parameters to context");
		avcodec_free_context(&codec_ctx);
		return false;
	}

	if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
		LogError("Failed to open codec");
		avcodec_free_context(&codec_ctx);
		return false;
	}

	videoCodecContext = codec_ctx;

	if (!videoStream) {
		return false;
	}
	timebase = videoStream->time_base;
	if (videoStream->duration > 0)
	{
		durationInStreamTimebase = videoStream->duration;
	}
	else if (avformatContext->duration > 0)
	{
		durationInStreamTimebase = av_rescale_q(avformatContext->duration, AVRational{ 1, AV_TIME_BASE }, videoStream->time_base);
	}
	else
	{
		LogError("Unable to get video duration.");
		return false;
	}
	frameRate = (float)av_q2d(videoStream->avg_frame_rate);
	frameCount = (int64_t)(durationInStreamTimebase * av_q2d(av_mul_q(timebase, videoStream->avg_frame_rate)));
	one_second_time = (int64_t)av_q2d(av_inv_q(timebase));
	durationInSeconds = durationInStreamTimebase * av_q2d(timebase);
	videoRotation = GetAvStreamRotateAngle(*this);
	originWidth = videoCodecContext->width;
	originHeight = videoCodecContext->height;
	codecName = std::string(codec->name);

	videoFormat = static_cast<AVPixelFormat>(codecpar->format);

	videoFrameSizeInBytes = av_image_get_buffer_size(
		videoFormat,
		actualFrameWidth,
		actualFrameHeight, 1);


	if (abs(videoRotation) == 90 || abs(videoRotation) == 270)
	{
		actualFrameWidth = originHeight;
		actualFrameHeight = originWidth;
	}
	else 
	{
		actualFrameWidth = originWidth;
		actualFrameHeight = originHeight;
	}

	auto& self = *this;
	keyFrameGapTime = GetKeyFrameInterval(self);
	if (testDeocderFPS) {
		LogDebug("Start test decoder fps.");
		decoderFPS = GetDecoderFPS(self);
	}
	SeekToStart();
	return true;
}


void FFmpegContext::SeekToStart() const
{
	int ret = 0;
	if (videoStreamIdx >= 0 && avformatContext) {
		ret = av_seek_frame(avformatContext, videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
		if (ret < 0) {
			LogError("Failed to seek video stream: %s", getAvError(ret));
		}
		else {
			avcodec_flush_buffers(videoCodecContext);
			LogDebug("Video stream flushed.");
		}
	}

	if (audioStreamIdx >= 0 && audioCodecContext) {
		ret = av_seek_frame(avformatContext, audioStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
		if (ret < 0) {
			LogError("Failed to seek audio stream: %s", getAvError(ret));
		}
		else {
			avcodec_flush_buffers(audioCodecContext);
			LogDebug("Audio stream flushed.");
		}
	}
}

void FFmpegContext::FillVideoInfo(VideoInfo& videoInfo) const
{
	videoInfo.Fps = this->frameRate;
	videoInfo.DurationMills = (int64_t)round(this->durationInSeconds * 1000);
	videoInfo.VideoWidth = this->actualFrameWidth;
	videoInfo.VideoHeight = this->actualFrameHeight;
	videoInfo.TotalFrames = this->videoStream->nb_frames;
	videoInfo.Rotation = this->videoRotation;
	videoInfo.DecoderFPS = this->decoderFPS;
	videoInfo.HasAudio = this->audioStreamIdx >= 0;
	videoInfo.PixelFormat = VideoFrameFormat::VIDEO_FRAME_UNKNWON;
	

	// ----------------------------------------
	// 安全复制 codec 字符串
	// ----------------------------------------
	const char* name = nullptr;

	if (videoCodecContext && videoCodecContext->codec_descriptor)
		name = videoCodecContext->codec_descriptor->name;
	else if (videoCodecContext && videoCodecContext->codec)
		name = videoCodecContext->codec->name;
	else
		name = "unknown";

	strncpy(videoInfo.VideoCodec, name, sizeof(videoInfo.VideoCodec) - 1);
	videoInfo.VideoCodec[sizeof(videoInfo.VideoCodec) - 1] = '\0';
}

FFmpegContext::~FFmpegContext()
{
	if (videoCodecContext) {
		avcodec_close(videoCodecContext);
		videoCodecContext = nullptr;
	}
	if (audioCodecContext) {
		avcodec_close(audioCodecContext);
		audioCodecContext = nullptr;
	}
	//av_free(Codec);
	if (avformatContext) {
		avformat_close_input(&avformatContext);
		avformatContext = nullptr;
	}

	if (ioContext) {
		av_freep(&ioContext->buffer);
		avio_context_free(&ioContext);
		ioContext = nullptr;
	}

}
