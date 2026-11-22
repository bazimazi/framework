// Test shader (tests/test_gpu_d3d11.c): a full-target quad from the vertex index (triangle strip,
// no vertex buffers) sampling t0/s0.
Texture2D source : register(t0);
SamplerState source_sampler : register(s0);
struct vs_out { float4 position : SV_Position; float2 uv : TEXCOORD0; };
vs_out vs_main(uint id : SV_VertexID) {
    float2 uv = float2(id & 1, id >> 1);
    vs_out output;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}
float4 ps_main(vs_out input) : SV_Target { return source.Sample(source_sampler, input.uv); }
