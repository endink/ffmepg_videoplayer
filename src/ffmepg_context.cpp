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
	context.SeekToStart();
	int frameCount = 0;
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	double start = GetTimestampMills();
	bool hasError = false;
	while (frameCount < 10)
	{

		int ret = av_read_frame(context.avformatContext, packet);
		if (ret == AVERROR_EOF)
		{
			break;
		}
		if (ret != 0)
		{
			LogDebug("av_read_frame: %s", getAvError(ret));
			hasError = true;
			break;
		}


		if (packet->stream_index == context.videoStreamIdx)
		{
			LogDebug("test frame: %d", frameCount);
			// while(true) {
			// 	if(avcodec_receive_frame(context.videoCodecContext, frame) != 0) {
			// 		break;
			// 	}
			// 	av_frame_unref(frame);
			// }
			ret = avcodec_send_packet(context.videoCodecContext, packet);
			if (ret < 0)
			{
				LogDebug("avcodec_send_packet: %s", getAvError(ret));
				hasError = true;
				break;
			}

			ret = avcodec_receive_frame(context.videoCodecContext, frame);
			if (ret == AVERROR(EAGAIN)) {
				continue;
			}
			if (ret != 0)
			{
				LogDebug("avcodec_receive_frame: %s", getAvError(ret));
				hasError = true;
				break;
			}
			frameCount++;
			av_frame_unref(frame);
		}
		av_packet_unref(packet);
	}

	av_packet_free(&packet);
	av_frame_free(&frame);
	if (hasError)
	{
		LogDebug("test fps failed");
		return 0.0f;
	}
	LogDebug("test fps, frames: %ld, time: %.lf", (frameCount * 1000), (GetTimestampMills() - start));
	return (float)((frameCount * 1000) / (GetTimestampMills() - start));
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
	context.Flush();
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
	actualFrameWidth = videoCodecContext->width;
	actualFrameHeight = videoCodecContext->height;

	AVCodecParameters* codecpar = videoStream->codecpar;
	videoFormat = static_cast<AVPixelFormat>(codecpar->format);

	videoFrameSizeInBytes = av_image_get_buffer_size(
		videoFormat,
		actualFrameWidth,
		actualFrameHeight, 1);


	if (abs(videoRotation) == 90 || abs(videoRotation) == 270)
	{
		actualFrameWidth = videoCodecContext->height;
		actualFrameHeight = videoCodecContext->width;
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

void FFmpegContext::Flush() const
{
	if (videoCodecContext)
	{
		avcodec_flush_buffers(videoCodecContext);
	}
	if (audioCodecContext)
	{
		avcodec_flush_buffers(audioCodecContext);
	}
}

void FFmpegContext::SeekToStart() const
{
	if (videoStreamIdx >= 0 && avformatContext)
	{
		av_seek_frame(avformatContext, videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(videoCodecContext);
		LogDebug("Video stream flushed.");
	}
	if (audioStreamIdx >= 0 && audioCodecContext)
	{
		av_seek_frame(avformatContext, audioStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(audioCodecContext);
		LogDebug("Audio stream flushed.");
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
		avio_context_free(&ioContext);
		ioContext = nullptr;
	}
}
