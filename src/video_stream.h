#pragma once

#include <string>
#include <fstream>
#include <cstdint>
#include <algorithm> // for std::min

class IVideoStream {
	public:
		virtual ~IVideoStream() = default;
		virtual int Read(uint8_t* data, int len) = 0;
		virtual int64_t Seek(int64_t offset, int whence) = 0;
};


class VideoFileStream : public IVideoStream {
public:
	VideoFileStream(const std::string& video_file);
	~VideoFileStream() override;

	int Read(uint8_t* data, int len) override;
	int64_t Seek(int64_t offset, int whence) override;
private:
	std::string file;
	std::ifstream input;
	int64_t read_position = 0;
	int64_t file_size = 0;
};

class VideoFileDescriptorStream : public IVideoStream {
public:
	VideoFileDescriptorStream(int file_descriptor);

	~VideoFileDescriptorStream() {
		// 不负责关闭 fd，由调用者管理
	}

	int Read(uint8_t* data, int len) override;
	int64_t Seek(int64_t offset, int whence) override;
private:
	int fd;
	int64_t read_position = 0;
	int64_t file_size = 0; // 可选，用于 EOF 检查
};