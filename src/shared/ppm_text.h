// Fast text PPM (P3) in and out, and a chunked parallel loop for the per-pixel work in between. A 1280 x 720 picture is 2.7 million numbers: written and read one at a time through iostream
// that took about as long as the GPU took to draw a cartoon frame (and a cartoon frame is written once and read twice). The text is byte for byte what `out << r << " " << g << " " << b << "\n"` writes
// and the numbers read are what `in >> value` reads, so a picture does not change.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace ppm_text {

// Calls fn(begin, end) on consecutive ranges of [0, count) from several threads (one when the work is small).
template <class Fn>
inline void parallel_ranges(std::size_t count, Fn fn, std::size_t minPerThread = 16384) {
	// (min and max in parentheses: this header is also pulled in by files that include <windows.h> without NOMINMAX)
	std::size_t threads = (std::min<std::size_t>)((std::max<std::size_t>)(1, std::thread::hardware_concurrency()), 16);
	threads = (std::min)(threads, (std::max<std::size_t>)(1, count / minPerThread));
	if (threads <= 1) {
		fn(std::size_t(0), count);
		return;
	}
	std::vector<std::thread> pool;
	const std::size_t chunk = (count + threads - 1) / threads;
	for (std::size_t t = 1; t < threads; ++t) {
		const std::size_t begin = t * chunk;
		if (begin >= count) break;
		pool.emplace_back([=] { fn(begin, (std::min)(count, begin + chunk)); });
	}
	fn(std::size_t(0), (std::min)(count, chunk));
	for (std::thread &t : pool) t.join();
}

// The most characters a pixel's "r g b\n" takes (3 + 1 + 3 + 1 + 3 + 1): what a destination buffer for format_p3_pixels() needs per pixel.
constexpr std::size_t kMaxP3BytesPerPixel = 12;

// "r g b\n" for each of `pixelCount` pixels of 8-bit RGB, written to `dst` (which holds at least pixelCount * kMaxP3BytesPerPixel characters and need not be initialised). Returns the number of
// characters written.
inline std::size_t format_p3_pixels(char *dst, const unsigned char *rgb, std::size_t pixelCount) {
	char *p = dst;
	auto put = [&p](unsigned v) {
		if (v >= 100) *p++ = char('0' + v / 100), v %= 100, *p++ = char('0' + v / 10), *p++ = char('0' + v % 10);
		else if (v >= 10) *p++ = char('0' + v / 10), *p++ = char('0' + v % 10);
		else *p++ = char('0' + v);
	};
	for (std::size_t i = 0; i < pixelCount; ++i) {
		put(rgb[i * 3 + 0]);
		*p++ = ' ';
		put(rgb[i * 3 + 1]);
		*p++ = ' ';
		put(rgb[i * 3 + 2]);
		*p++ = '\n';
	}
	return static_cast<std::size_t>(p - dst);
}

// The same text appended to a string (for callers that want one; the renderer writes from a buffer instead, to skip the string's zero-fill).
inline void append_p3_pixels(std::string &out, const unsigned char *rgb, std::size_t pixelCount) {
	std::vector<char> text(pixelCount * kMaxP3BytesPerPixel);
	out.append(text.data(), format_p3_pixels(text.data(), rgb, pixelCount));
}

// Reads `count` whitespace-separated non-negative integers from [p, end) into `out` (each cut to a byte, as `static_cast<unsigned char>(int)` does). False when fewer numbers are there or
// something else is; `failedAt` is the index of the number that could not be read.
inline bool parse_p3_samples(const char *p, const char *end, unsigned char *out, std::size_t count, std::size_t &failedAt) {
	for (std::size_t i = 0; i < count; ++i) {
		while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
		if (p >= end || *p < '0' || *p > '9') {
			failedAt = i;
			return false;
		}
		unsigned v = 0;
		while (p < end && *p >= '0' && *p <= '9') v = v * 10 + unsigned(*p++ - '0');
		out[i] = static_cast<unsigned char>(v);
	}
	return true;
}

}  // namespace ppm_text
