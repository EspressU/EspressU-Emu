#pragma once

inline constexpr const char* kFrameGenShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

constexpr sampler linearClamp(coord::normalized, address::clamp_to_edge, filter::linear);
struct Grid { uint2 size; float phase; };
struct Camera { float2 origin; float2 dx; float2 dy; };
struct Vertex { float4 position [[position]]; };

float2 uvFor(uint2 pixel, uint2 size) { return (float2(pixel) + 0.5f) / float2(size); }
bool inside(float2 uv) { return all(uv >= 0.0f) && all(uv <= 1.0f); }

float patchError(texture2d<float> a, texture2d<float> b, float2 p, float2 v, float2 pixel, float radius)
{
    if (!inside(p + v * pixel))
        return 1.0f;
    float cost = 0.0f;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float2 uv = p + float2(x, y) * pixel * radius;
            float3 error = abs(a.sample(linearClamp, uv).rgb - b.sample(linearClamp, uv + v * pixel).rgb);
            cost += dot(error, float3(1.0f / 27.0f));
        }
    return cost;
}

void consider(texture2d<float> a, texture2d<float> b, float2 p, float2 pixel, float radius,
              float2 trial, thread float2& best, thread float& error)
{
    float score = patchError(a, b, p, trial, pixel, radius) + length(trial) * 0.00012f;
    if (score < error)
    {
        best = trial;
        error = score;
    }
}

kernel void fg_regions(texture2d<float> a [[texture(0)]], texture2d<float> b [[texture(1)]],
                       texture2d<float, access::write> out [[texture(2)]],
                       constant Grid& grid [[buffer(0)]], uint2 pos [[thread_position_in_grid]])
{
    uint2 size(out.get_width(), out.get_height());
    if (any(pos >= size))
        return;
    float2 p = uvFor(pos, size), pixel = 1.0f / float2(grid.size), best = 0.0f;
    float error = patchError(a, b, p, best, pixel, 3.0f);
    if (error > 0.002f)
    {
        for (int y = -2; y <= 2; ++y)
            for (int x = -2; x <= 2; ++x)
                consider(a, b, p, pixel, 3.0f, float2(x, y), best, error);
        if (error > 0.012f)
        {
            for (int y = -4; y <= 4; ++y)
                for (int x = -4; x <= 4; ++x)
                    consider(a, b, p, pixel, 3.0f, float2(x, y) * 4.0f, best, error);
            if (error > 0.025f && all((pos & 1u) == 0u))
                for (int y = -4; y <= 4; ++y)
                    for (int x = -4; x <= 4; ++x)
                        consider(a, b, p, pixel, 3.0f, float2(x, y) * 8.0f, best, error);
        }
        for (float step = 2.0f; step >= 0.5f; step *= 0.5f)
        {
            float2 center = best;
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                    consider(a, b, p, pixel, 3.0f, center + float2(x, y) * step, best, error);
        }
    }
    out.write(float4(best, error, 0.0f), pos);
}

uint2 cameraSample(uint seed, uint2 size)
{
    seed = seed * 747796405u + 2891336453u;
    seed = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return uint2(seed % size.x, (seed >> 16u) % size.y);
}

float2 cameraMotion(Camera model, float2 uv)
{
    float2 p = uv - 0.5f;
    return model.origin + model.dx * p.x + model.dy * p.y;
}

kernel void fg_camera(texture2d<float, access::read> regions [[texture(0)]],
                      device Camera* output [[buffer(0)]], uint lane [[thread_index_in_threadgroup]])
{
    threadgroup Camera models[32];
    threadgroup float errors[32];
    uint2 size(regions.get_width(), regions.get_height());
    uint2 p0 = cameraSample(lane * 3u, size);
    uint2 p1 = cameraSample(lane * 3u + 1u, size);
    uint2 p2 = cameraSample(lane * 3u + 2u, size);
    float4 v0 = regions.read(p0), v1 = regions.read(p1), v2 = regions.read(p2);
    Camera model{lane == 0u ? float2(0.0f) : v0.xy, float2(0.0f), float2(0.0f)};
    float2 u0 = uvFor(p0, size), d1 = uvFor(p1, size) - u0, d2 = uvFor(p2, size) - u0;
    float det = d1.x * d2.y - d1.y * d2.x;
    if (lane > 4u && abs(det) > 0.04f && max(v0.z, max(v1.z, v2.z)) < 0.08f)
    {
        model.dx = ((v1.xy - v0.xy) * d2.y - (v2.xy - v0.xy) * d1.y) / det;
        model.dy = ((v2.xy - v0.xy) * d1.x - (v1.xy - v0.xy) * d2.x) / det;
        model.origin = v0.xy - model.dx * (u0.x - 0.5f) - model.dy * (u0.y - 0.5f);
    }
    float error = 0.025f * (length(model.dx) + length(model.dy));
    for (uint i = 0u; i < 24u; ++i)
    {
        uint2 p = cameraSample(i + 97u, size);
        float4 measured = regions.read(p);
        float weight = 1.0f - smoothstep(0.015f, 0.09f, measured.z);
        error += weight * min(length(cameraMotion(model, uvFor(p, size)) - measured.xy), 4.0f);
    }
    models[lane] = model;
    errors[lane] = error;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (lane == 0u)
    {
        uint best = 0u;
        for (uint i = 1u; i < 32u; ++i)
            if (errors[i] < errors[best])
                best = i;
        *output = models[best];
    }
}

kernel void fg_refine(texture2d<float> a [[texture(0)]], texture2d<float> b [[texture(1)]],
                      texture2d<float, access::read> regions [[texture(2)]],
                      texture2d<float, access::write> out [[texture(3)]],
                      constant Grid& grid [[buffer(0)]], constant Camera& camera [[buffer(1)]],
                      uint2 pos [[thread_position_in_grid]])
{
    uint2 size(out.get_width(), out.get_height());
    if (any(pos >= size))
        return;
    float2 p = uvFor(pos, size), pixel = 1.0f / float2(grid.size);
    float2 best = 0.0f;
    float error = patchError(a, b, p, best, pixel, 1.5f);
    if (error > 0.002f)
    {
        consider(a, b, p, pixel, 1.5f, cameraMotion(camera, p), best, error);
        if (error > 0.008f)
        {
            int2 extent(regions.get_width(), regions.get_height());
            int2 first = int2(floor(p * float2(extent) - 0.5f));
            for (int y = 0; y <= 1; ++y)
                for (int x = 0; x <= 1; ++x)
                {
                    uint2 at = uint2(clamp(first + int2(x, y), int2(0), extent - 1));
                    consider(a, b, p, pixel, 1.5f, regions.read(at).xy, best, error);
                }
            for (float step = 2.0f; step >= 0.5f; step *= 0.5f)
            {
                float2 center = best;
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                        consider(a, b, p, pixel, 1.5f, center + float2(x, y) * step, best, error);
            }
        }
    }
    out.write(float4(best, 1.0f - smoothstep(0.012f, 0.1f, error), error), pos);
}

float3 flowAt(texture2d<float, access::read> field, float2 p)
{
    uint2 size(field.get_width(), field.get_height());
    uint2 at = min(uint2(clamp(p, 0.0f, 1.0f) * float2(size)), size - 1u);
    return field.read(at).xyz;
}

kernel void fg_project(texture2d<float> a [[texture(0)]], texture2d<float> b [[texture(1)]],
                       texture2d<float, access::read> forward [[texture(2)]],
                       texture2d<float, access::read> backward [[texture(3)]],
                       constant Grid& grid [[buffer(0)]], device atomic_uint* map [[buffer(1)]],
                       uint2 pos [[thread_position_in_grid]])
{
    if (any(pos >= grid.size))
        return;
    float2 p = uvFor(pos, grid.size);
    float3 v = flowAt(forward, p);
    float2 q = p + v.xy / float2(grid.size);
    if (v.z < 0.5f || !inside(q))
        return;
    float3 reverse = flowAt(backward, q);
    float mismatch = length(v.xy + reverse.xy);
    float residual = length(a.sample(linearClamp, p).rgb - b.sample(linearClamp, q).rgb);
    float confidence = min(v.z, reverse.z) * (1.0f - smoothstep(0.7f, 2.5f, mismatch))
        * (1.0f - smoothstep(0.05f, 0.22f, residual));
    if (confidence < 0.35f)
        return;
    float2 target = float2(pos) + grid.phase * v.xy;
    int2 base = int2(floor(target));
    float2 frac = fract(target);
    uint source = pos.y * grid.size.x + pos.x + 1u;
    for (int y = 0; y <= 1; ++y)
        for (int x = 0; x <= 1; ++x)
        {
            int2 dst = base + int2(x, y);
            if (any(dst < 0) || any(uint2(dst) >= grid.size))
                continue;
            float2 weight = mix(1.0f - frac, frac, float2(x, y));
            uint quality = uint(confidence * weight.x * weight.y * 32767.0f);
            if (quality > 0u)
                atomic_fetch_max_explicit(map + uint(dst.y) * grid.size.x + uint(dst.x),
                                          (quality << 17u) | source, memory_order_relaxed);
        }
}

vertex Vertex fg_vertex(uint id [[vertex_id]])
{
    return {float4(float2((id << 1u) & 2u, id & 2u) * 2.0f - 1.0f, 0.0f, 1.0f)};
}

fragment float4 fg_copy(Vertex in [[stage_in]], texture2d<float> source [[texture(0)]])
{
    return source.sample(linearClamp, in.position.xy / float2(source.get_width(), source.get_height()));
}

float2 inverseWarp(float2 p, float phase, uint source, uint2 grid,
                   texture2d<float, access::read> flow)
{
    float2 q = p;
    if (source != 0u)
    {
        --source;
        q = uvFor(uint2(source % grid.x, source / grid.x), grid);
    }
    for (uint i = 0u; i < 3u; ++i)
        q = p - phase * flowAt(flow, q).xy / float2(grid);
    return q;
}

bool resolvePair(texture2d<float> a, texture2d<float> b,
                 texture2d<float, access::read> forward, texture2d<float, access::read> backward,
                 float2 p, float2 ua, float2 ub, Grid grid, thread float4& color)
{
    if (!inside(ua) || !inside(ub))
        return false;
    float3 va = flowAt(forward, ua), vb = flowAt(backward, ub);
    if (min(va.z, vb.z) < 0.5f || length(va.xy + vb.xy) > 2.25f ||
        length((mix(ua, ub, grid.phase) - p) * float2(grid.size)) > 0.9f)
        return false;
    float4 ca = a.sample(linearClamp, ua), cb = b.sample(linearClamp, ub);
    if (length(ca.rgb - cb.rgb) > 0.2f)
        return false;
    color = mix(ca, cb, grid.phase);
    return true;
}

fragment float4 fg_present(Vertex in [[stage_in]], texture2d<float> a [[texture(0)]],
                           texture2d<float> b [[texture(1)]],
                           texture2d<float, access::read> forward [[texture(2)]],
                           texture2d<float, access::read> backward [[texture(3)]],
                           constant Grid& grid [[buffer(0)]],
                           const device uint* fromA [[buffer(1)]], const device uint* fromB [[buffer(2)]])
{
    float2 p = in.position.xy / float2(a.get_width(), a.get_height());
    float4 originalA = a.sample(linearClamp, p), originalB = b.sample(linearClamp, p);
    float4 nearest = grid.phase < 0.5f ? originalA : originalB;
    if (length(originalA.rgb - originalB.rgb) < 0.006f)
        return originalB;
    uint2 tile = min(uint2(p * float2(grid.size)), grid.size - 1u);
    uint index = tile.y * grid.size.x + tile.x;
    float2 ua = inverseWarp(p, grid.phase, fromA[index] & 0x1ffffu, grid.size, forward);
    float2 ub = inverseWarp(p, 1.0f - grid.phase, fromB[index] & 0x1ffffu, grid.size, backward);
    float3 va = flowAt(forward, ua), vb = flowAt(backward, ub);
    bool validA = inside(ua), validB = inside(ub);
    if (!validA || !validB)
    {
        if (validA && va.z > 0.85f)
            return a.sample(linearClamp, ua);
        if (validB && vb.z > 0.85f)
            return b.sample(linearClamp, ub);
        return nearest;
    }
    float4 color;
    if (resolvePair(a, b, forward, backward, p, ua, ub, grid, color))
        return color;
    // Two inverse searches can land on opposite sides of an object edge.
    // Try pairing each surface with its own endpoint before falling back.
    float2 pixels = float2(grid.size);
    if (resolvePair(a, b, forward, backward, p, ua, ua + va.xy / pixels, grid, color) ||
        resolvePair(a, b, forward, backward, p, ub + vb.xy / pixels, ub, grid, color))
        return color;
    return nearest;
}
)MSL";
