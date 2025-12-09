// Copyright (c) 2025 Anders Xiao. All rights reserved.
// https://github.com/endink

#pragma once

#ifdef _WIN32
#include <windows.h>   // 必须放在 FFmpeg 前
#endif

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/display.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

struct FormatConverter {
	AVFrame* bufferFrame = nullptr;
	AVFrame* convertedFrame = nullptr;
	SwsContext* swsContext = nullptr;
	float scale = 1.0f;
	int srcWidth = 0;
	int srcHeight = 0;
	int distWidth = 0;
	int distHeight = 0;
	uint8_t* distBufferData = nullptr;
	AVPixelFormat srcPixelFormat = AV_PIX_FMT_NONE;
	AVPixelFormat distPixelFormat = AV_PIX_FMT_NONE;
	AVPacket* packet;


	FormatConverter(int srcWidth, int srcHeight, AVPixelFormat distFormat, float scale = 1.0f) : srcWidth(srcWidth), srcHeight(srcHeight), scale(scale), distPixelFormat(distFormat)
	{
		packet = av_packet_alloc();
		bufferFrame = av_frame_alloc();
		distWidth = (scale > 0 && scale < 1) ? (int)(srcWidth * scale) : srcWidth;
		distHeight = (scale > 0 && scale < 1) ? (int)(srcHeight * scale) : srcHeight;
		convertedFrame = InitAVFrame(distWidth, distHeight, distFormat, &distBufferData);
	}

	void Convert(AVFrame* sourceFrame = nullptr) {
		auto* buffer = (sourceFrame != nullptr) ? sourceFrame : bufferFrame;
		if (buffer) {
			LoadContext((AVPixelFormat)buffer->format);
			sws_scale(swsContext,
				(uint8_t const* const*)buffer->data,
				buffer->linesize,
				0,
				srcHeight,
				convertedFrame->data,
				convertedFrame->linesize);

			convertedFrame->pts = buffer->pts;
		}
	}

	~FormatConverter() {
		if (distBufferData != nullptr) {
			av_free(distBufferData);
			distBufferData = nullptr;
		}
		if (convertedFrame != nullptr) {
			av_frame_unref(convertedFrame);
			av_frame_free(&convertedFrame);
		}
		if (swsContext != nullptr) {
			sws_freeContext(swsContext);
			swsContext = nullptr;
		}
		if (packet != nullptr) {
			av_packet_unref(packet);
			av_packet_free(&packet);
			packet = nullptr;
		}
		if (bufferFrame != nullptr) {
			av_frame_unref(bufferFrame);
			av_frame_free(&bufferFrame);
			bufferFrame = nullptr;
		}
	}

private:
	AVFrame* InitAVFrame(int width, int height, AVPixelFormat distFormat, uint8_t** buffer)
	{
		AVFrame* pFrameRGB = av_frame_alloc();
		if (pFrameRGB == nullptr)
		{
			return nullptr;
		}
		pFrameRGB->width = width;
		pFrameRGB->height = height;
		int numBytes = av_image_get_buffer_size(distFormat, width, height, 1);
		*buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
		av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize,
			*buffer, distFormat, width, height, 1);

		return pFrameRGB;
	}

	void LoadContext(AVPixelFormat format) {
		if (swsContext == nullptr || srcPixelFormat != format) {
			if (swsContext != nullptr) {
				sws_freeContext(swsContext);
			}
			srcPixelFormat = format;
			swsContext = sws_getContext(
				srcWidth,
				srcHeight,
				srcPixelFormat,
				distWidth,
				distHeight,
				distPixelFormat,
				SWS_BILINEAR,
				nullptr, nullptr, nullptr);
		}
	}
};
