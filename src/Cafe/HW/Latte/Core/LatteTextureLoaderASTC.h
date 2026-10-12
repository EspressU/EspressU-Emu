#pragma once
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include "LatteTextureLoader.h"
#include "Common/DeviceCapabilities.h"
#include "astcenc.h"
#include "astcenc_internal.h"

static inline uint8 astcFloatToUNorm8(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    return (uint8)(v * 255.0f + 0.5f);
}

static inline uint8 astcFloatToUNorm8FromSNorm(float v)
{
    v = std::clamp(v, -1.0f, 1.0f);
    return (uint8)((v * 0.5f + 0.5f) * 255.0f + 0.5f);
}

static inline size_t astcCompressedImageSize(sint32 width, sint32 height)
{
    return (size_t)((width + 3) / 4) * (size_t)((height + 3) / 4) * 16u;
}

static uint32 astcEncodeThreadCount()
{
    static const uint32 count = []() -> uint32 {
        const DeviceCaps::Info& info = DeviceCaps::Get();
        if (info.perfCores == 0)
            return 1;
        const uint32 usable = info.perfCores + info.effCores / 2;
        if (usable <= 2)
            return 1;
        return std::min<uint32>(usable - 2, 3) + 1;
    }();
    return count;
}

static constexpr size_t kAstcThreadedMinTexels = 256 * 256;

struct ASTCEncoderContext
{
    astcenc_context* ctx = nullptr;
    uint32 threads = 1;
};

struct ASTCEncoderContextSet
{
    ASTCEncoderContext ldr;
    ASTCEncoderContext ldrSrgb;

    ~ASTCEncoderContextSet()
    {
        if (ldr.ctx)
            astcenc_context_free(ldr.ctx);
        if (ldrSrgb.ctx)
            astcenc_context_free(ldrSrgb.ctx);
    }
};

static ASTCEncoderContext* astcGetContext(astcenc_profile profile)
{
    thread_local ASTCEncoderContextSet contexts;
    ASTCEncoderContext& enc = (profile == ASTCENC_PRF_LDR_SRGB) ? contexts.ldrSrgb : contexts.ldr;
    if (enc.ctx)
        return &enc;

    astcenc_config config;
    astcenc_error status = astcenc_config_init(
        profile,
        4, 4, 1,
        ASTCENC_PRE_FAST,
        0,
        &config);
    if (status != ASTCENC_SUCCESS)
        return nullptr;

    uint32 threads = astcEncodeThreadCount();
    astcenc_context* ctx = nullptr;
    status = astcenc_context_alloc(&config, threads, &ctx, nullptr);
    if (status != ASTCENC_SUCCESS && threads > 1)
    {
        threads = 1;
        ctx = nullptr;
        status = astcenc_context_alloc(&config, threads, &ctx, nullptr);
    }
    if (status != ASTCENC_SUCCESS)
        return nullptr;

    enc.ctx = ctx;
    enc.threads = threads;
    return &enc;
}

static astcenc_error astcCompressImageThreaded(astcenc_context* context, astcenc_image* image, const astcenc_swizzle* swizzle,
                                               uint8* outputData, size_t outputSize, uint32 threads)
{
    std::atomic<int> helperFailure{(int)ASTCENC_SUCCESS};
    std::vector<std::thread> helpers;
    helpers.reserve(threads - 1);
    for (uint32 i = 1; i < threads; i++)
    {
        try
        {
            helpers.emplace_back([context, image, swizzle, outputData, outputSize, i, &helperFailure]() {
                const astcenc_error st = astcenc_compress_image(context, image, swizzle, outputData, outputSize, i);
                if (st != ASTCENC_SUCCESS)
                {
                    int expected = (int)ASTCENC_SUCCESS;
                    helperFailure.compare_exchange_strong(expected, (int)st);
                }
            });
        }
        catch (...)
        {
            break;
        }
    }

    const astcenc_error mainStatus = astcenc_compress_image(context, image, swizzle, outputData, outputSize, 0);

    for (std::thread& helper : helpers)
        helper.join();

    if (mainStatus != ASTCENC_SUCCESS)
        return mainStatus;
    return (astcenc_error)helperFailure.load();
}

static bool astcCompressRGBA8Image(const uint8* rgba8, sint32 width, sint32 height, astcenc_profile profile, uint8* outputData)
{
    ASTCEncoderContext* enc = astcGetContext(profile);
    if (!enc) {
        cemuLog_log(LogType::Force, "ASTC Encode Fail: no context");
        return false;
    }
    astcenc_context* context = enc->ctx;

    astcenc_image image;
    image.dim_x = (unsigned int)width;
    image.dim_y = (unsigned int)height;
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;

    uint8* imageSlice = const_cast<uint8*>(rgba8);
    image.data = reinterpret_cast<void**>(&imageSlice);

    static const astcenc_swizzle kIdentitySwizzle = {
        ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A
    };

    size_t outputSize = astcCompressedImageSize(width, height);
    uint32 threads = enc->threads;
    if ((size_t)width * (size_t)height < kAstcThreadedMinTexels)
        threads = 1;

    astcenc_error status;
    if (threads > 1)
        status = astcCompressImageThreaded(context, &image, &kIdentitySwizzle, outputData, outputSize, threads);
    else
        status = astcenc_compress_image(context, &image, &kIdentitySwizzle, outputData, outputSize, 0);

    astcenc_compress_reset(context);

    if (status != ASTCENC_SUCCESS) {
        cemuLog_log(LogType::Force, "ASTC Encode Fail: {}", (int)status);
        return false;
    }
    return true;
}

template<typename DecodeFn>
static void decodeBCToRGBA8Image(DecodeFn fn, LatteTextureLoaderCtx* tl, uint8* rgba8)
{
    for (sint32 y = 0; y < tl->height; y += tl->stepY) {
        for (sint32 x = 0; x < tl->width; x += tl->stepX) {
            uint8* blockData = LatteTextureLoader_GetInput(tl, x, y);
            sint32 bsX = std::min(4, tl->width - x);
            sint32 bsY = std::min(4, tl->height - y);

            float floatBuf[16 * 4] = {};
            fn(blockData, floatBuf);

            for (sint32 py = 0; py < bsY; ++py) {
                for (sint32 px = 0; px < bsX; ++px) {
                    sint32 src = (py * 4 + px) * 4;
                    sint32 dst = ((y + py) * tl->width + (x + px)) * 4;
                    rgba8[dst + 0] = astcFloatToUNorm8(floatBuf[src + 0]);
                    rgba8[dst + 1] = astcFloatToUNorm8(floatBuf[src + 1]);
                    rgba8[dst + 2] = astcFloatToUNorm8(floatBuf[src + 2]);
                    rgba8[dst + 3] = astcFloatToUNorm8(floatBuf[src + 3]);
                }
            }
        }
    }
}

template<typename DecodeFn>
static void decodeBC4ToRGBA8Image(DecodeFn fn, LatteTextureLoaderCtx* tl, uint8* rgba8, bool isSigned)
{
    for (sint32 y = 0; y < tl->height; y += tl->stepY) {
        for (sint32 x = 0; x < tl->width; x += tl->stepX) {
            uint8* blockData = LatteTextureLoader_GetInput(tl, x, y);
            sint32 bsX = std::min(4, tl->width - x);
            sint32 bsY = std::min(4, tl->height - y);

            float floatBuf[16] = {};
            fn(blockData, floatBuf);

            for (sint32 py = 0; py < bsY; ++py) {
                for (sint32 px = 0; px < bsX; ++px) {
                    sint32 src = py * 4 + px;
                    sint32 dst = ((y + py) * tl->width + (x + px)) * 4;
                    uint8 r = isSigned ? astcFloatToUNorm8FromSNorm(floatBuf[src]) : astcFloatToUNorm8(floatBuf[src]);
                    rgba8[dst + 0] = r;
                    rgba8[dst + 1] = 0;
                    rgba8[dst + 2] = 0;
                    rgba8[dst + 3] = 255;
                }
            }
        }
    }
}

template<typename DecodeFn>
static void decodeBC5ToRGBA8Image(DecodeFn fn, LatteTextureLoaderCtx* tl, uint8* rgba8, bool isSigned)
{
    for (sint32 y = 0; y < tl->height; y += tl->stepY) {
        for (sint32 x = 0; x < tl->width; x += tl->stepX) {
            uint8* blockData = LatteTextureLoader_GetInput(tl, x, y);
            sint32 bsX = std::min(4, tl->width - x);
            sint32 bsY = std::min(4, tl->height - y);

            float floatBuf[32] = {};
            fn(blockData, floatBuf);

            for (sint32 py = 0; py < bsY; ++py) {
                for (sint32 px = 0; px < bsX; ++px) {
                    sint32 src = (py * 4 + px) * 2;
                    sint32 dst = ((y + py) * tl->width + (x + px)) * 4;
                    uint8 r = isSigned ? astcFloatToUNorm8FromSNorm(floatBuf[src + 0]) : astcFloatToUNorm8(floatBuf[src + 0]);
                    uint8 g = isSigned ? astcFloatToUNorm8FromSNorm(floatBuf[src + 1]) : astcFloatToUNorm8(floatBuf[src + 1]);
                    rgba8[dst + 0] = r;
                    rgba8[dst + 1] = g;
                    rgba8[dst + 2] = 0;
                    rgba8[dst + 3] = 255;
                }
            }
        }
    }
}

namespace astcdirect
{
    static inline void writeRGBBlock(uint8* blk, const uint8 e0[3], const uint8 e1[3], const uint8* w, int weightBits)
    {
        unsigned __int128 v = (unsigned __int128)(weightBits == 2 ? 66u : 83u);
        v |= (unsigned __int128)8u << 13;
        const uint64 ep = (uint64)e0[0] | (uint64)e1[0] << 8 | (uint64)e0[1] << 16 | (uint64)e1[1] << 24
                        | (uint64)e0[2] << 32 | (uint64)e1[2] << 40;
        v |= (unsigned __int128)ep << 17;
        unsigned __int128 wv = 0;
        for (int t = 0; t < 16; t++)
            wv |= (unsigned __int128)w[t] << (t * weightBits);
        const uint64 lo = (uint64)wv, hi = (uint64)(wv >> 64);
        v |= (unsigned __int128)__builtin_bitreverse64(lo) << 64 | (unsigned __int128)__builtin_bitreverse64(hi);
        memcpy(blk, &v, 16);
    }

    static inline void orderAndWrite(uint8* blk, uint8 e0[3], uint8 e1[3], uint8* w, int weightBits)
    {
        if (e1[0] + e1[1] + e1[2] < e0[0] + e0[1] + e0[2])
        {
            uint8 t[3];
            memcpy(t, e0, 3); memcpy(e0, e1, 3); memcpy(e1, t, 3);
            const uint8 top = (uint8)((1 << weightBits) - 1);
            for (int i = 0; i < 16; i++)
                w[i] = (uint8)(top - w[i]);
        }
        writeRGBBlock(blk, e0, e1, w, weightBits);
    }

    static inline void expand565(uint16 c, uint8 out[3])
    {
        const int r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
        out[0] = (uint8)(r << 3 | r >> 2); out[1] = (uint8)(g << 2 | g >> 4); out[2] = (uint8)(b << 3 | b >> 2);
    }

    static inline bool transcodeBC1(const uint8* bc, uint8* blk)
    {
        const uint16 c0 = (uint16)(bc[0] | bc[1] << 8), c1 = (uint16)(bc[2] | bc[3] << 8);
        if (c0 <= c1)
            return false;
        uint8 e0[3], e1[3];
        expand565(c0, e0); expand565(c1, e1);
        static const uint8 kWeight[4] = { 0, 3, 1, 2 };
        const uint32 idx = (uint32)bc[4] | (uint32)bc[5] << 8 | (uint32)bc[6] << 16 | (uint32)bc[7] << 24;
        uint8 w[16];
        for (int t = 0; t < 16; t++)
            w[t] = kWeight[idx >> (2 * t) & 3];
        orderAndWrite(blk, e0, e1, w, 2);
        return true;
    }

    static inline unsigned __int128 reverse128(unsigned __int128 v)
    {
        return (unsigned __int128)__builtin_bitreverse64((uint64)v) << 64 | (unsigned __int128)__builtin_bitreverse64((uint64)(v >> 64));
    }

    static inline uint8 quant7(uint8 e)
    {
        int best = e >> 1, bestDiff = 999;
        for (int v = std::max(0, (e >> 1) - 1); v <= std::min(127, (e >> 1) + 1); v++)
        {
            const int d = std::abs(((v << 1) | (v >> 6)) - e);
            if (d < bestDiff) { bestDiff = d; best = v; }
        }
        return (uint8)best;
    }

    static inline bool transcodeBC1ThreeColour(const uint8* bc, uint8* blk)
    {
        const uint16 c0 = (uint16)(bc[0] | bc[1] << 8), c1 = (uint16)(bc[2] | bc[3] << 8);
        if (c0 > c1)
            return false;
        uint8 e0[3], e1[3];
        expand565(c0, e0); expand565(c1, e1);
        const uint32 idx = (uint32)bc[4] | (uint32)bc[5] << 8 | (uint32)bc[6] << 16 | (uint32)bc[7] << 24;
        const bool transparent = (idx & (idx >> 1) & 0x55555555u) != 0;
        uint8 q0[3], q1[3];
        int s0 = 0, s1 = 0;
        for (int c = 0; c < 3; c++)
        {
            if (transparent)
            {
                q0[c] = quant7(e0[c]); q1[c] = quant7(e1[c]);
                s0 += (q0[c] << 1) | (q0[c] >> 6); s1 += (q1[c] << 1) | (q1[c] >> 6);
            }
            else
            {
                s0 += e0[c]; s1 += e1[c];
            }
        }
        const bool swap = s1 < s0;
        const uint8 weightOf[4] = { (uint8)(swap ? 2 : 0), (uint8)(swap ? 0 : 2), 1, (uint8)(swap ? 2 : 0) };
        uint8 w[32];
        uint8 weightBits[16] = {};
        unsigned __int128 v;
        if (!transparent)
        {
            const uint8* lo = swap ? e1 : e0;
            const uint8* hi = swap ? e0 : e1;
            v = (unsigned __int128)81u | (unsigned __int128)8u << 13;
            const uint64 ep = (uint64)lo[0] | (uint64)hi[0] << 8 | (uint64)lo[1] << 16 | (uint64)hi[1] << 24 | (uint64)lo[2] << 32 | (uint64)hi[2] << 40;
            v |= (unsigned __int128)ep << 17;
            for (int t = 0; t < 16; t++)
                w[t] = weightOf[idx >> (2 * t) & 3];
            encode_ise(QUANT_3, 16, w, weightBits, 0);
        }
        else
        {
            const uint8* lo = swap ? q1 : q0;
            const uint8* hi = swap ? q0 : q1;
            v = (unsigned __int128)1105u | (unsigned __int128)12u << 13;
            const uint64 ep = (uint64)lo[0] | (uint64)hi[0] << 7 | (uint64)lo[1] << 14 | (uint64)hi[1] << 21 | (uint64)lo[2] << 28 | (uint64)hi[2] << 35 | (uint64)127 << 42;
            v |= (unsigned __int128)ep << 17;
            for (int t = 0; t < 16; t++)
            {
                const int k = idx >> (2 * t) & 3;
                w[2 * t] = weightOf[k];
                w[2 * t + 1] = k == 3 ? 2 : 0;
            }
            encode_ise(QUANT_3, 32, w, weightBits, 0);
            v |= (unsigned __int128)3u << (128 - 52 - 2);
        }
        unsigned __int128 wv;
        memcpy(&wv, weightBits, 16);
        v |= reverse128(wv);
        memcpy(blk, &v, 16);
        return true;
    }

    static inline uint8 bc4Endpoint(uint8 v, bool isSigned)
    {
        if (!isSigned)
            return v;
        const sint8 s = (sint8)v;
        return astcFloatToUNorm8FromSNorm(s == -128 ? -1.0f : (float)s / 127.0f);
    }

    static inline bool transcodeBC4(const uint8* bc, uint8* blk, bool isSigned)
    {
        const int a0 = isSigned ? (int)(sint8)bc[0] : (int)bc[0];
        const int a1 = isSigned ? (int)(sint8)bc[1] : (int)bc[1];
        if (a0 <= a1)
            return false;
        uint8 e0[3] = { bc4Endpoint(bc[0], isSigned), 0, 0 }, e1[3] = { bc4Endpoint(bc[1], isSigned), 0, 0 };
        uint64 bits = 0;
        for (int k = 0; k < 6; k++)
            bits |= (uint64)bc[2 + k] << (8 * k);
        static const uint8 kWeight[8] = { 0, 7, 1, 2, 3, 4, 5, 6 };
        uint8 w[16];
        for (int t = 0; t < 16; t++)
            w[t] = kWeight[bits >> (3 * t) & 7];
        orderAndWrite(blk, e0, e1, w, 3);
        return true;
    }
}

template<typename DirectFn, typename ToRGBA8Fn>
static void transcodeOrCompressASTC(LatteTextureLoaderCtx* tl, uint8* outputData, astcenc_profile profile, DirectFn direct, ToRGBA8Fn toRGBA8)
{
    const sint32 blocksX = (tl->width + 3) / 4;
    std::vector<std::pair<uint8*, uint8*>> rest;
    for (sint32 y = 0; y < tl->height; y += tl->stepY)
    {
        for (sint32 x = 0; x < tl->width; x += tl->stepX)
        {
            uint8* blockData = LatteTextureLoader_GetInput(tl, x, y);
            uint8* dst = outputData + ((size_t)(y / 4) * (size_t)blocksX + (size_t)(x / 4)) * 16u;
            if (!direct(blockData, dst))
                rest.emplace_back(blockData, dst);
        }
    }
    if (rest.empty())
        return;
    const sint32 stripWidth = (sint32)rest.size() * 4;
    std::vector<uint8> strip((size_t)stripWidth * 4u * 4u);
    for (size_t i = 0; i < rest.size(); i++)
    {
        uint8 texels[16 * 4];
        toRGBA8(rest[i].first, texels);
        for (sint32 py = 0; py < 4; py++)
            memcpy(&strip[((size_t)py * (size_t)stripWidth + i * 4u) * 4u], &texels[py * 16], 16);
    }
    std::vector<uint8> encoded(rest.size() * 16u);
    if (!astcCompressRGBA8Image(strip.data(), stripWidth, 4, profile, encoded.data()))
        std::fill(encoded.begin(), encoded.end(), 0);
    for (size_t i = 0; i < rest.size(); i++)
        memcpy(rest[i].second, &encoded[i * 16u], 16);
}

static void bc1BlockToRGBA8(uint8* blockData, uint8* rgba8)
{
    float buf[16 * 4];
    decodeBC1Block(blockData, buf);
    for (int i = 0; i < 16 * 4; i++)
        rgba8[i] = astcFloatToUNorm8(buf[i]);
}

static void bc4BlockToRGBA8(uint8* blockData, uint8* rgba8, bool isSigned)
{
    float buf[16];
    if (isSigned)
        decodeBC4Block_SNORM(blockData, buf);
    else
        decodeBC4Block_UNORM(blockData, buf);
    for (int i = 0; i < 16; i++)
    {
        rgba8[i * 4 + 0] = isSigned ? astcFloatToUNorm8FromSNorm(buf[i]) : astcFloatToUNorm8(buf[i]);
        rgba8[i * 4 + 1] = 0;
        rgba8[i * 4 + 2] = 0;
        rgba8[i * 4 + 3] = 255;
    }
}

template<astcenc_profile Profile, typename DecodeFn>
static void decodeRGBAAndCompressASTC(LatteTextureLoaderCtx* tl, uint8* outputData, DecodeFn fn)
{
    std::vector<uint8> rgba8((size_t)tl->width * (size_t)tl->height * 4u);
    decodeBCToRGBA8Image(fn, tl, rgba8.data());

    if (!astcCompressRGBA8Image(rgba8.data(), tl->width, tl->height, Profile, outputData))
        std::fill(outputData, outputData + astcCompressedImageSize(tl->width, tl->height), 0);
}

static void decodeBC4AndCompressASTC(LatteTextureLoaderCtx* tl, uint8* outputData, bool isSigned)
{
    std::vector<uint8> rgba8((size_t)tl->width * (size_t)tl->height * 4u);
    if (isSigned)
        decodeBC4ToRGBA8Image(decodeBC4Block_SNORM, tl, rgba8.data(), true);
    else
        decodeBC4ToRGBA8Image(decodeBC4Block_UNORM, tl, rgba8.data(), false);

    if (!astcCompressRGBA8Image(rgba8.data(), tl->width, tl->height, ASTCENC_PRF_LDR, outputData))
        std::fill(outputData, outputData + astcCompressedImageSize(tl->width, tl->height), 0);
}

static void decodeBC5AndCompressASTC(LatteTextureLoaderCtx* tl, uint8* outputData, bool isSigned)
{
    std::vector<uint8> rgba8((size_t)tl->width * (size_t)tl->height * 4u);
    if (isSigned)
        decodeBC5ToRGBA8Image(decodeBC5Block_SNORM, tl, rgba8.data(), true);
    else
        decodeBC5ToRGBA8Image(decodeBC5Block_UNORM, tl, rgba8.data(), false);

    if (!astcCompressRGBA8Image(rgba8.data(), tl->width, tl->height, ASTCENC_PRF_LDR, outputData))
        std::fill(outputData, outputData + astcCompressedImageSize(tl->width, tl->height), 0);
}

template<astcenc_profile Profile>
class TextureDecoder_BC1_to_ASTC_Generic : public TextureDecoder
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        transcodeOrCompressASTC(tl, outputData, Profile,
            [](const uint8* bc, uint8* blk) { return astcdirect::transcodeBC1(bc, blk) || astcdirect::transcodeBC1ThreeColour(bc, blk); },
            bc1BlockToRGBA8);
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        BC1_GetPixel(blockData, ox, oy, out);
    }
};

class TextureDecoder_BC1_UNORM_to_ASTC : public TextureDecoder_BC1_to_ASTC_Generic<ASTCENC_PRF_LDR>, public SingletonClass<TextureDecoder_BC1_UNORM_to_ASTC>
{
};

class TextureDecoder_BC1_SRGB_to_ASTC : public TextureDecoder_BC1_to_ASTC_Generic<ASTCENC_PRF_LDR_SRGB>, public SingletonClass<TextureDecoder_BC1_SRGB_to_ASTC>
{
};

template<astcenc_profile Profile>
class TextureDecoder_BC2_to_ASTC_Generic : public TextureDecoder
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        decodeRGBAAndCompressASTC<Profile>(tl, outputData, decodeBC2Block_UNORM);
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        float buf[64];
        decodeBC2Block_UNORM(blockData, buf);
        int i = (ox + oy * 4) * 4;
        out[0] = (uint8)(buf[i] * 255.0f);
        out[1] = (uint8)(buf[i + 1] * 255.0f);
        out[2] = (uint8)(buf[i + 2] * 255.0f);
        out[3] = (uint8)(buf[i + 3] * 255.0f);
    }
};

class TextureDecoder_BC2_UNORM_to_ASTC : public TextureDecoder_BC2_to_ASTC_Generic<ASTCENC_PRF_LDR>, public SingletonClass<TextureDecoder_BC2_UNORM_to_ASTC>
{
};

class TextureDecoder_BC2_SRGB_to_ASTC : public TextureDecoder_BC2_to_ASTC_Generic<ASTCENC_PRF_LDR_SRGB>, public SingletonClass<TextureDecoder_BC2_SRGB_to_ASTC>
{
};

template<astcenc_profile Profile>
class TextureDecoder_BC3_to_ASTC_Generic : public TextureDecoder
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        decodeRGBAAndCompressASTC<Profile>(tl, outputData, decodeBC3Block_UNORM);
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        float buf[64];
        decodeBC3Block_UNORM(blockData, buf);
        int i = (ox + oy * 4) * 4;
        out[0] = (uint8)(buf[i] * 255.0f);
        out[1] = (uint8)(buf[i + 1] * 255.0f);
        out[2] = (uint8)(buf[i + 2] * 255.0f);
        out[3] = (uint8)(buf[i + 3] * 255.0f);
    }
};

class TextureDecoder_BC3_UNORM_to_ASTC : public TextureDecoder_BC3_to_ASTC_Generic<ASTCENC_PRF_LDR>, public SingletonClass<TextureDecoder_BC3_UNORM_to_ASTC>
{
};

class TextureDecoder_BC3_SRGB_to_ASTC : public TextureDecoder_BC3_to_ASTC_Generic<ASTCENC_PRF_LDR_SRGB>, public SingletonClass<TextureDecoder_BC3_SRGB_to_ASTC>
{
};

class TextureDecoder_BC4_UNORM_to_ASTC : public TextureDecoder, public SingletonClass<TextureDecoder_BC4_UNORM_to_ASTC>
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        transcodeOrCompressASTC(tl, outputData, ASTCENC_PRF_LDR,
            [](const uint8* bc, uint8* blk) { return astcdirect::transcodeBC4(bc, blk, false); },
            [](uint8* bc, uint8* rgba8) { bc4BlockToRGBA8(bc, rgba8, false); });
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        float buf[16];
        decodeBC4Block_UNORM(blockData, buf);
        uint8 v = (uint8)(buf[ox + oy * 4] * 255.0f);
        out[0] = v;
        out[1] = 0;
        out[2] = 0;
        out[3] = 255;
    }
};

class TextureDecoder_BC4_SNORM_to_ASTC : public TextureDecoder, public SingletonClass<TextureDecoder_BC4_SNORM_to_ASTC>
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        transcodeOrCompressASTC(tl, outputData, ASTCENC_PRF_LDR,
            [](const uint8* bc, uint8* blk) { return astcdirect::transcodeBC4(bc, blk, true); },
            [](uint8* bc, uint8* rgba8) { bc4BlockToRGBA8(bc, rgba8, true); });
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        float buf[16];
        decodeBC4Block_SNORM(blockData, buf);
        uint8 v = (uint8)((buf[ox + oy * 4] * 0.5f + 0.5f) * 255.0f);
        out[0] = v;
        out[1] = 0;
        out[2] = 0;
        out[3] = 255;
    }
};

class TextureDecoder_BC5_UNORM_to_ASTC : public TextureDecoder, public SingletonClass<TextureDecoder_BC5_UNORM_to_ASTC>
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        decodeBC5AndCompressASTC(tl, outputData, false);
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        float buf[32];
        decodeBC5Block_UNORM(blockData, buf);
        int i = (ox + oy * 4) * 2;
        out[0] = (uint8)(buf[i] * 255.0f);
        out[1] = (uint8)(buf[i + 1] * 255.0f);
        out[2] = 0;
        out[3] = 255;
    }
};

class TextureDecoder_BC5_SNORM_to_ASTC : public TextureDecoder, public SingletonClass<TextureDecoder_BC5_SNORM_to_ASTC>
{
public:
    sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 16; }
    sint32 getTexelCountX(LatteTextureLoaderCtx* tl) override { return (tl->width + 3) / 4; }
    sint32 getTexelCountY(LatteTextureLoaderCtx* tl) override { return (tl->height + 3) / 4; }

    void decode(LatteTextureLoaderCtx* tl, uint8* outputData) override
    {
        decodeBC5AndCompressASTC(tl, outputData, true);
    }

    void decodePixelToRGBA(uint8* blockData, uint8* out, uint8 ox, uint8 oy) override
    {
        float buf[32];
        decodeBC5Block_SNORM(blockData, buf);
        int i = (ox + oy * 4) * 2;
        out[0] = (uint8)((buf[i] * 0.5f + 0.5f) * 255.0f);
        out[1] = (uint8)((buf[i + 1] * 0.5f + 0.5f) * 255.0f);
        out[2] = 0;
        out[3] = 255;
    }
};
