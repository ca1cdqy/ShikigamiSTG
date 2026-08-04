// Sprite vertex shader.

struct VSInput {
    float2 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float4 FogColor : TEXCOORD3;
    float FogFactor : TEXCOORD4;
};

struct VSOutput {
    float4 Color : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float4 FogColor : TEXCOORD2;
    float FogFactor : TEXCOORD3;
    float4 Position : SV_Position;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.Position = float4(input.Position, 0.0, 1.0);
    output.Color = input.Color;
    output.UV = input.UV;
    output.FogColor = input.FogColor;
    output.FogFactor = input.FogFactor;
    return output;
}
