#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 fogColor [[attribute(3)]];
    float fogFactor [[attribute(4)]];
};

struct VertexOutput {
    float4 position [[position]];
    float4 color;
    float2 uv;
    float4 fogColor;
    float fogFactor;
};

vertex VertexOutput spriteVertex(VertexInput input [[stage_in]]) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    output.fogColor = input.fogColor;
    output.fogFactor = input.fogFactor;
    return output;
}
