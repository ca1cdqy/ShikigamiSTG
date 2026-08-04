#include <metal_stdlib>
using namespace metal;

struct FragmentInput {
    float4 color;
    float2 uv;
    float4 fogColor;
    float fogFactor;
};

fragment float4 spriteFragment(
    FragmentInput input [[stage_in]],
    texture2d<float> spriteTexture [[texture(0)]],
    sampler spriteSampler [[sampler(0)]]) {
    const float4 textureColor = spriteTexture.sample(spriteSampler, input.uv);
    float4 result = textureColor * input.color;
    result.rgb = mix(input.fogColor.rgb, result.rgb,
                      saturate(input.fogFactor));
    return result;
}
