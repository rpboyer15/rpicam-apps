/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * circular_output.hpp - Write output to a circular buffer.
 */

#pragma once

#include "output.hpp"
#include <functional>
#include <vector>

// A simple circular buffer implementation used by the CircularOutput class.

class CircularBuffer
{
public:
	CircularBuffer(size_t size) : size_(size), buf_(size), rptr_(0), wptr_(0) {}
	bool Empty() const { return rptr_ == wptr_; }
	size_t Available() const { return (wptr_ >= rptr_) ? (size_ - (wptr_ - rptr_) - 1) : (rptr_ - wptr_ - 1); }
	size_t Size() const { return size_; }

	void Skip(unsigned int n) { rptr_ = (rptr_ + n) % size_; }

	void Read(std::function<void(void *src, unsigned int n)> dst, unsigned int n)
	{
		if (rptr_ + n > size_)
		{
			unsigned int first = size_ - rptr_;
			dst(&buf_[rptr_], first);
			dst(&buf_[0], n - first);
			rptr_ = n - first;
		}
		else
		{
			dst(&buf_[rptr_], n);
			rptr_ += n;
		}
	}

	void Write(const void *ptr, unsigned int n)
	{
		if (wptr_ + n > size_)
		{
			unsigned int first = size_ - wptr_;
			memcpy(&buf_[wptr_], ptr, first);
			memcpy(&buf_[0], static_cast<const uint8_t *>(ptr) + first, n - first);
			wptr_ = n - first;
		}
		else
		{
			memcpy(&buf_[wptr_], ptr, n);
			wptr_ += n;
		}
	}

	void Pad(unsigned int n) { wptr_ = (wptr_ + n) % size_; }

	void Copy(std::function<void(void *src, unsigned int n)> dst, size_t offset, unsigned int n) const
	{
		size_t pos = (rptr_ + offset) % size_;
		if (pos + n > size_)
		{
			unsigned int first = size_ - pos;
			dst((void *)&buf_[pos], first);
			dst((void *)&buf_[0], n - first);
		}
		else
		{
			dst((void *)&buf_[pos], n);
		}
	}

	void CopyOut(void *dst_buf) const
	{
		if (wptr_ >= rptr_)
		{
			memcpy(dst_buf, &buf_[rptr_], wptr_ - rptr_);
		}
		else
		{
			size_t first = size_ - rptr_;
			memcpy(dst_buf, &buf_[rptr_], first);
			memcpy(static_cast<uint8_t *>(dst_buf) + first, &buf_[0], wptr_);
		}
	}

	void CopyFromAbsolutePosition(std::function<void(void *src, unsigned int n)> dst, size_t pos, unsigned int n) const
	{
		pos = pos % size_;
		if (pos + n > size_)
		{
			unsigned int first = size_ - pos;
			dst((void *)&buf_[pos], first);
			dst((void *)&buf_[0], n - first);
		}
		else
		{
			dst((void *)&buf_[pos], n);
		}
	}

	size_t getReadPointer() const { return rptr_; }
	size_t getWritePointer() const { return wptr_; }

private:
	const size_t size_;
	std::vector<uint8_t> buf_;
	size_t rptr_, wptr_;
};

// Write frames to a circular buffer, and dump them to disk when signaled.

class CircularOutput : public Output
{
public:
	CircularOutput(VideoOptions const *options);
	~CircularOutput();
	void DumpToFile();
	void Signal() override;

protected:
	void outputBuffer(void *mem, size_t size, int64_t timestamp_us, uint32_t flags) override;
	void timestampReady(int64_t timestamp) override;

private:
	CircularBuffer cb_;
	FILE *fp_;
	size_t last_keyframe_pos_ = 0;
	bool keyframe_seen_ = false;
	size_t latest_keyframe_pos_ = 0;
};
