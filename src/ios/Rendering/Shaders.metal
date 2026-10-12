#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut screenVertex(
    const device VertexIn* vertices [[buffer(0)]],
    uint vertexId [[vertex_id]])
{
    VertexOut out;
    out.position = float4(vertices[vertexId].position, 0.0, 1.0);
    out.texCoord = vertices[vertexId].texCoord;
    return out;
}

fragment float4 screenFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 color = colorTexture.sample(textureSampler, in.texCoord);
    return color;
}

fragment float4 bilinearUpscaleFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant float2& sourceSize [[buffer(0)]],
    constant float2& destSize [[buffer(1)]],
    sampler textureSampler [[sampler(0)]])
{
    float2 sourceCoord = in.texCoord * sourceSize;
    float2 fracCoord = fract(sourceCoord);
    float2 texelCoord = floor(sourceCoord);

    float4 c00 = colorTexture.sample(textureSampler, (texelCoord + float2(0, 0)) / sourceSize);
    float4 c10 = colorTexture.sample(textureSampler, (texelCoord + float2(1, 0)) / sourceSize);
    float4 c01 = colorTexture.sample(textureSampler, (texelCoord + float2(0, 1)) / sourceSize);
    float4 c11 = colorTexture.sample(textureSampler, (texelCoord + float2(1, 1)) / sourceSize);

    float4 c0 = mix(c00, c10, fracCoord.x);
    float4 c1 = mix(c01, c11, fracCoord.x);
    float4 result = mix(c0, c1, fracCoord.y);

    return result;
}

fragment float4 lanczosUpscaleFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant float2& sourceSize [[buffer(0)]],
    constant float2& destSize [[buffer(1)]],
    sampler textureSampler [[sampler(0)]])
{
    float2 sourceCoord = in.texCoord * sourceSize;
    float2 fracCoord = fract(sourceCoord);
    float2 centerCoord = floor(sourceCoord) + 0.5;

    float4 result = float4(0.0);
    float weight_sum = 0.0;

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            float2 offset = float2(dx, dy);
            float2 sampleCoord = (centerCoord + offset) / sourceSize;

            if (sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
                sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0) {

                float dx_f = fracCoord.x + float(dx) - 0.5;
                float dy_f = fracCoord.y + float(dy) - 0.5;

                float weight_x = 1.0;
                float weight_y = 1.0;

                if (abs(dx_f) < 1.0) {
                    weight_x = cos(3.14159 * dx_f) * 0.5 + 0.5;
                } else if (abs(dx_f) < 2.0) {
                    weight_x = cos(3.14159 * dx_f * 0.5) * 0.5;
                } else {
                    weight_x = 0.0;
                }

                if (abs(dy_f) < 1.0) {
                    weight_y = cos(3.14159 * dy_f) * 0.5 + 0.5;
                } else if (abs(dy_f) < 2.0) {
                    weight_y = cos(3.14159 * dy_f * 0.5) * 0.5;
                } else {
                    weight_y = 0.0;
                }

                float weight = weight_x * weight_y;
                result += colorTexture.sample(textureSampler, sampleCoord) * weight;
                weight_sum += weight;
            }
        }
    }

    return result / max(weight_sum, 0.001);
}

fragment float4 sharpeningFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant float& strength [[buffer(0)]],
    sampler textureSampler [[sampler(0)]])
{
    float2 texel = 1.0 / float2(colorTexture.get_width(), colorTexture.get_height());

    float4 center = colorTexture.sample(textureSampler, in.texCoord);
    float4 top = colorTexture.sample(textureSampler, in.texCoord + float2(0, texel.y));
    float4 bottom = colorTexture.sample(textureSampler, in.texCoord - float2(0, texel.y));
    float4 left = colorTexture.sample(textureSampler, in.texCoord - float2(texel.x, 0));
    float4 right = colorTexture.sample(textureSampler, in.texCoord + float2(texel.x, 0));

    float4 sharpened = center * (1.0 + strength * 4.0) -
                       (top + bottom + left + right) * (strength);

    return clamp(sharpened, float4(0.0), float4(1.0));
}

fragment float4 gammaFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant float& gamma [[buffer(0)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 color = colorTexture.sample(textureSampler, in.texCoord);
    return pow(color, float4(1.0 / gamma));
}

fragment float4 contrastFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant float& contrast [[buffer(0)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 color = colorTexture.sample(textureSampler, in.texCoord);
    return (color - 0.5) * contrast + 0.5;
}

fragment float4 brightnessFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant float& brightness [[buffer(0)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 color = colorTexture.sample(textureSampler, in.texCoord);
    return color + float4(brightness);
}
