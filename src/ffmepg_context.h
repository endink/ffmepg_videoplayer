// Copyright (c) 2025 Anders Xiao. All rights reserved.
// https://github.com/endink

#pragma once
#include <string>
extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
}
#include "videoplayer_c_api.h"


struct FFmpegContext {
	AVIOContext* ioContext = nullptr;
	AVFormatContext* avformatContext = nullptr;
	const AVCodec* Codec = nullptr;
	AVCodecContext* videoCodecContext = nullptr;
	AVCodecContext* audioCodecContext = nullptr;
	AVStream* videoStream = nullptr;
	int videoStreamIdx = -1;
	int audioStreamIdx = -1;
	int64_t durationInStreamTimebase = 0;
	double durationInSeconds = 0.0;
	int videoRotation = 0;
	AVPixelFormat videoFormat = AVPixelFormat::AV_PIX_FMT_NONE;
	AVRational timebase{ 0, 1000000 };
	float frameRate = 30.0f;
	int64_t frameCount = 0;
	int64_t one_second_time = 0;
	int actualFrameWidth = 0;
	int actualFrameHeight = 0;
	int videoFrameSizeInBytes = 0;
	/*
	  pts unit
	*/
	int64_t keyFrameGapTime = 0;
	double decoderFPS = 0;
	std::string mime;

	bool LoadVideoProperties(bool testDeocderFPS);

	inline int64_t getTimeBetweenFrame() const {
		return one_second_time / (int64_t)(frameRate)+1;
	}

	void FillVideoInfo(VideoInfo& videoInfo) const;
	void Flush() const;
	void SeekToStart() const;



	~FFmpegContext();
};