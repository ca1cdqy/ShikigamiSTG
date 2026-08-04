// Sprite fragment shader.

struct VSOutput {
    float4 Color : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float4 FogColor : TEXCOORD2;
    float FogFactor : TEXCOORD3;
    float4 Position : SV_Position;
};

Texture2D spriteTexture : register(t0, space2);
SamplerState spriteSampler : register(s0, space2);

float4 main(VSOutput input) : SV_Target0 {
    float4 texColor = spriteTexture.Sample(spriteSampler, input.UV);
    float4 result = texColor * input.Color;
    result.rgb = lerp(input.FogColor.rgb, result.rgb, saturate(input.FogFactor));
    return result;
}
