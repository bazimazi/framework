// Test shader (tests/test_gpu_d3d11.c): pixel coordinates (y down) to clip space through a
// uniform block; per-instance color.
cbuffer params : register(b0) { float2 target_size; float depth; float unused; };
struct vs_in { float2 position : ATTR0; float4 color : ATTR1; };
struct vs_out { float4 position : SV_Position; float4 color : COLOR0; };
vs_out vs_main(vs_in input) {
    vs_out output;
    output.position = float4(input.position.x / target_size.x * 2.0 - 1.0, 1.0 - input.position.y / target_size.y * 2.0, depth, 1.0);
    output.color = input.color;
    return output;
}
float4 ps_main(vs_out input) : SV_Target { return input.color; }
