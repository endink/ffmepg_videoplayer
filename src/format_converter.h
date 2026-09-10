#pragma once

extern "C" {
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
    }
#include <iostream>

struct FormatConverter {
    AVFrame* bufferFrame = nullptr;      // 临时源帧
    AVFrame* convertedFrame = nullptr;   // 转换后的帧
    uint8_t* distBufferData = nullptr;   // 转换帧的数据缓冲
    SwsContext* swsContext = nullptr;

    int srcWidth = 0;
    int srcHeight = 0;
    int distWidth = 0;
    int distHeight = 0;

    float scale = 1.0f;
    AVPixelFormat srcPixelFormat = AV_PIX_FMT_NONE;
    AVPixelFormat distPixelFormat = AV_PIX_FMT_NONE;

    FormatConverter(int width, int height, AVPixelFormat dstFormat, float scale = 1.0f)
        : srcWidth(width), srcHeight(height), scale(scale), distPixelFormat(dstFormat)
    {
        // 计算目标宽高
        distWidth = (scale > 0 && scale  != 1.0f) ? static_cast<int>(width * scale) : width;
        distHeight = (scale > 0 && scale != 1.0f) ? static_cast<int>(height * scale) : height;

        // 分配帧
        bufferFrame = av_frame_alloc();
        if (!bufferFrame) {
            throw std::runtime_error("Failed to allocate bufferFrame");
        }

        convertedFrame = InitAVFrame(distWidth, distHeight, distPixelFormat, &distBufferData);
        if (!convertedFrame) {
            av_frame_free(&bufferFrame);
            throw std::runtime_error("Failed to allocate convertedFrame");
        }
    }

    ~FormatConverter() {
        if (distBufferData) {
            av_free(distBufferData);
            distBufferData = nullptr;
        }

        if (convertedFrame) {
            av_frame_unref(convertedFrame);
            av_frame_free(&convertedFrame);
        }

        if (swsContext) {
            sws_freeContext(swsContext);
            swsContext = nullptr;
        }

        if (bufferFrame) {
            av_frame_unref(bufferFrame);
            av_frame_free(&bufferFrame);
        }
    }

    void Convert(AVFrame* sourceFrame = nullptr) {
        AVFrame* frame = (sourceFrame) ? sourceFrame : bufferFrame;
        if (!frame) return;

        LoadContext(static_cast<AVPixelFormat>(frame->format));

        sws_scale(
            swsContext,
            frame->data,
            frame->linesize,
            0,
            frame->height,        // 使用实际高度
            convertedFrame->data,
            convertedFrame->linesize
        );

        convertedFrame->pts = frame->pts;
    }

private:
    AVFrame* InitAVFrame(int width, int height, AVPixelFormat format, uint8_t** buffer) {
        AVFrame* frame = av_frame_alloc();
        if (!frame) return nullptr;

        frame->width = width;
        frame->height = height;
        frame->format = format;

        int numBytes = av_image_get_buffer_size(format, width, height, 1);
        if (numBytes <= 0) {
            av_frame_free(&frame);
            return nullptr;
        }

        *buffer = (uint8_t*)av_malloc(numBytes);
        if (!*buffer) {
            av_frame_free(&frame);
            return nullptr;
        }

        av_image_fill_arrays(frame->data, frame->linesize, *buffer, format, width, height, 1);
        return frame;
    }

    void LoadContext(AVPixelFormat format) {
        if (!swsContext || srcPixelFormat != format) {
            if (swsContext) {
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
                nullptr, nullptr, nullptr
            );

            if (!swsContext) {
                throw std::runtime_error("Failed to create SwsContext");
            }
        }
    }
};
