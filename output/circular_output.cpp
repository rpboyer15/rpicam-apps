/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * circular_output.cpp - Write output to circular buffer which we save on signal.
 */

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "circular_output.hpp"

static constexpr int ALIGN = 16; // Power of 2

struct Header
{
	unsigned int length;
	bool keyframe;
	int64_t timestamp;
};
static_assert(sizeof(Header) % ALIGN == 0, "Header should have aligned size");

CircularOutput::CircularOutput(VideoOptions const *options)
	: Output(options), cb_(options->circular << 20), fp_(nullptr)
{
	if (options_->output == "-")
		fp_ = stdout;
}

CircularOutput::~CircularOutput()
{
	// Do nothing
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
	// No-op for now
}

void CircularOutput::DumpToFile()
{
	if (options_->output.empty() || options_->output == "-")
	{
		std::cerr << "Output filename is not set or is stdout; cannot dump circular buffer.\n";
		return;
	}

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

	size_t w = cb_.getWritePointer();
	size_t buffer_size = cb_.Size();
	size_t r = cb_.getReadPointer();
	size_t search = r;
	size_t latest_keyframe_pos = r;
	bool found_keyframe = false;

	// Step 1: search from read ptr to write ptr for the most recent keyframe
	while (search != w)
	{
		Header header;
		size_t pos = search;
		cb_.CopyFromAbsolutePosition([&](void *src, unsigned int n) { memcpy(&header, src, n); }, pos, sizeof(header));

		search = (search + sizeof(header)) % buffer_size;

		if (header.keyframe)
		{
			latest_keyframe_pos = pos;
			found_keyframe = true;
		}

		unsigned int padded_len = (header.length + ALIGN - 1) & ~(ALIGN - 1);
		search = (search + padded_len) % buffer_size;
	}

	if (!found_keyframe)
	{
		std::cerr << "[DumpToFile] No keyframe found. Skipping.\n";
		fclose(fp);
		return;
	}

	// Step 2: write from latest keyframe to write pointer
	r = latest_keyframe_pos;
	while (r != w)
	{
		Header header;
		size_t pos = r;
		cb_.CopyFromAbsolutePosition([&](void *src, unsigned int n) { memcpy(&header, src, n); }, pos, sizeof(header));

		r = (r + sizeof(header)) % buffer_size;

		cb_.CopyFromAbsolutePosition([&](void *src, unsigned int n) { fwrite(src, 1, n, fp); }, r, header.length);

		total += header.length;
		frames++;

		unsigned int padded_len = (header.length + ALIGN - 1) & ~(ALIGN - 1);
		r = (r + padded_len) % buffer_size;
	}

	fclose(fp);
	std::ofstream done_file(filename.str() + ".done");
	done_file << "done";
	done_file.close();

	LOG(1, "Dumped circular buffer from latest keyframe to " << filename.str() << " (" << frames << " frames, " << total
															 << " bytes)");
}

void CircularOutput::Signal()
{
	DumpToFile();
}
