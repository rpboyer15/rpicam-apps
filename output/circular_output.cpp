/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * circular_output.cpp - Write output to circular buffer which we save on exit.
 */

#include <chrono> // for std::chrono::system_clock
#include <cstdio> // for fopen, fwrite, fclose
#include <cstring> // for memcpy
#include <ctime> // for std::time_t, std::localtime
#include <iomanip> // for std::put_time
#include <iostream> // for std::cerr
#include <sstream> // for std::ostringstream
#include <stdexcept> // for std::runtime_error

#include "circular_output.hpp"

// We're going to align the frames within the buffer to friendly byte boundaries
static constexpr int ALIGN = 16; // power of 2, please

struct Header
{
	unsigned int length;
	bool keyframe;
	int64_t timestamp;
};
static_assert(sizeof(Header) % ALIGN == 0, "Header should have aligned size");

// Size of buffer (options->circular) is given in megabytes.
CircularOutput::CircularOutput(VideoOptions const *options) : Output(options), cb_(options->circular << 20)
{
	// Open this now, so that we can get any complaints out of the way
	if (options_->output == "-")
		fp_ = stdout;
	else if (!options_->output.empty())
	{
		fp_ = fopen(options_->output.c_str(), "w");
	}
	if (!fp_)
		throw std::runtime_error("could not open output file");
}

CircularOutput::~CircularOutput()
{
	unsigned int total = 0, frames = 0;
	bool seen_keyframe = false;
	Header header;
	FILE *fp = fp_; // can't capture a class member in a lambda

	while (!cb_.Empty())
	{
		uint8_t *dst = (uint8_t *)&header;
		cb_.Read(
			[&dst](void *src, int n)
			{
				memcpy(dst, src, n);
				dst += n;
			},
			sizeof(header));

		seen_keyframe |= header.keyframe;
		if (seen_keyframe)
		{
			cb_.Read([fp](void *src, int n) { fwrite(src, 1, n, fp); }, header.length);
			cb_.Skip((ALIGN - header.length) & (ALIGN - 1));
			total += header.length;
			if (fp_timestamps_)
				Output::timestampReady(header.timestamp);
			frames++;
		}
		else
			cb_.Skip((header.length + ALIGN - 1) & ~(ALIGN - 1));
	}

	fclose(fp_);
	LOG(1, "Wrote " << total << " bytes (" << frames << " frames)");
}

void CircularOutput::outputBuffer(void *mem, size_t size, int64_t timestamp_us, uint32_t flags)
{
	int pad = (ALIGN - size) & (ALIGN - 1);
	while (size + pad + sizeof(Header) > cb_.Available())
	{
		if (cb_.Empty())
			throw std::runtime_error("circular buffer too small");

		Header header;
		uint8_t *dst = (uint8_t *)&header;
		cb_.Read(
			[&dst](void *src, int n)
			{
				memcpy(dst, src, n);
				dst += n;
			},
			sizeof(header));

		cb_.Skip((header.length + ALIGN - 1) & ~(ALIGN - 1));
	}

	Header header = { static_cast<unsigned int>(size), !!(flags & FLAG_KEYFRAME), timestamp_us };
	cb_.Write(&header, sizeof(header));
	cb_.Write(mem, size);
	cb_.Pad(pad);
}

void CircularOutput::timestampReady(int64_t timestamp)
{
	// Don't want to save every timestamp as we go along, only outputs them at the end
}

void CircularOutput::DumpToFile()
{
	// Generate a unique filename with timestamp
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::ostringstream filename;
	filename << options_->output << "_" << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S") << ".h264";

	FILE *fp = fopen(filename.str().c_str(), "w");
	if (!fp)
	{
		std::cerr << "Failed to open " << filename.str() << " for writing\n";
		return;
	}

	unsigned int total = 0, frames = 0;
	bool seen_keyframe = false;
	Header header;

	CircularBuffer temp_cb = cb_; // Copy the circular buffer so we don't consume it

	while (!temp_cb.Empty())
	{
		uint8_t *dst = (uint8_t *)&header;
		temp_cb.Read(
			[&dst](void *src, int n)
			{
				memcpy(dst, src, n);
				dst += n;
			},
			sizeof(header));

		seen_keyframe |= header.keyframe;
		if (seen_keyframe)
		{
			temp_cb.Read([fp](void *src, int n) { fwrite(src, 1, n, fp); }, header.length);
			temp_cb.Skip((ALIGN - header.length) & (ALIGN - 1));
			total += header.length;
			if (fp_timestamps_)
				Output::timestampReady(header.timestamp);
			frames++;
		}
		else
			temp_cb.Skip((header.length + ALIGN - 1) & ~(ALIGN - 1));
	}

	fclose(fp);
	LOG(1, "Dumped circular buffer to " << filename.str() << " (" << frames << " frames, " << total << " bytes)");
}
