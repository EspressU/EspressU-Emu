// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace ios_audit
{
	enum class PixelLayout : int
	{
		RGBA8 = 0,
		BGRA8 = 1,
		RGB10A2 = 2,
	};

	struct FrameStats
	{
		float meanR = 0, meanG = 0, meanB = 0;
		float blackFraction = 0;
		float whiteFraction = 0;
		uint32_t minLuma = 255;
		uint32_t maxLuma = 0;
		uint64_t hash = 0;
	};

	inline void UnpackPixel(const uint8_t* p, PixelLayout layout, uint32_t& r, uint32_t& g, uint32_t& b)
	{
		switch (layout)
		{
		case PixelLayout::BGRA8:
			b = p[0]; g = p[1]; r = p[2];
			break;
		case PixelLayout::RGB10A2:
		{
			uint32_t v;
			std::memcpy(&v, p, 4);
			r = (v & 0x3FFu) >> 2;
			g = ((v >> 10) & 0x3FFu) >> 2;
			b = ((v >> 20) & 0x3FFu) >> 2;
			break;
		}
		case PixelLayout::RGBA8:
		default:
			r = p[0]; g = p[1]; b = p[2];
			break;
		}
	}

	inline void ComputeStatsAndThumb(const uint8_t* src, uint32_t width, uint32_t height, uint32_t rowBytes,
	                                 PixelLayout layout, uint32_t thumbW, uint32_t thumbH,
	                                 uint8_t* thumbRGB, FrameStats& out)
	{
		out = FrameStats{};
		if (!src || width == 0 || height == 0 || thumbW == 0 || thumbH == 0)
			return;

		std::vector<uint32_t> txOfX(width);
		for (uint32_t x = 0; x < width; ++x)
			txOfX[x] = (uint32_t)(((uint64_t)x * thumbW) / width);

		const size_t cells = (size_t)thumbW * thumbH;
		std::vector<uint64_t> sum(cells * 3, 0);
		std::vector<uint32_t> count(cells, 0);

		uint64_t sumR = 0, sumG = 0, sumB = 0, blackCount = 0, whiteCount = 0;
		uint64_t hash = 1469598103934665603ull;
		uint32_t minLuma = 255, maxLuma = 0;

		for (uint32_t y = 0; y < height; ++y)
		{
			const uint8_t* row = src + (size_t)y * rowBytes;
			const uint32_t ty = (uint32_t)(((uint64_t)y * thumbH) / height);
			const size_t cellRow = (size_t)ty * thumbW;

			size_t bytes = (size_t)width * 4;
			size_t i = 0;
			for (; i + 8 <= bytes; i += 8)
			{
				uint64_t w;
				std::memcpy(&w, row + i, 8);
				hash = (hash ^ w) * 1099511628211ull;
				hash ^= hash >> 29;
			}
			for (; i < bytes; ++i)
				hash = (hash ^ row[i]) * 1099511628211ull;

			for (uint32_t x = 0; x < width; ++x)
			{
				uint32_t r, g, b;
				UnpackPixel(row + (size_t)x * 4, layout, r, g, b);
				const size_t cell = cellRow + txOfX[x];
				sum[cell * 3 + 0] += r;
				sum[cell * 3 + 1] += g;
				sum[cell * 3 + 2] += b;
				count[cell] += 1;
				sumR += r; sumG += g; sumB += b;
				const uint32_t hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
				const uint32_t lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
				if (hi <= 8) ++blackCount;
				if (lo >= 247) ++whiteCount;
				const uint32_t luma = (r * 54 + g * 183 + b * 19) >> 8;
				if (luma < minLuma) minLuma = luma;
				if (luma > maxLuma) maxLuma = luma;
			}
		}

		const double total = (double)width * (double)height;
		out.meanR = (float)((double)sumR / total);
		out.meanG = (float)((double)sumG / total);
		out.meanB = (float)((double)sumB / total);
		out.blackFraction = (float)((double)blackCount / total);
		out.whiteFraction = (float)((double)whiteCount / total);
		out.minLuma = minLuma;
		out.maxLuma = maxLuma;
		out.hash = hash;

		if (thumbRGB)
		{
			for (size_t c = 0; c < cells; ++c)
			{
				const uint32_t n = count[c] ? count[c] : 1;
				thumbRGB[c * 3 + 0] = (uint8_t)(sum[c * 3 + 0] / n);
				thumbRGB[c * 3 + 1] = (uint8_t)(sum[c * 3 + 1] / n);
				thumbRGB[c * 3 + 2] = (uint8_t)(sum[c * 3 + 2] / n);
			}
		}
	}
}
