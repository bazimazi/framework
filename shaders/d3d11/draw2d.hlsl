// draw2d.hlsl: the single draw2d pipeline (SPEC §12.2, src/draw2d/draw2d.h).
// One instance per shape (bmf_draw_instance, 48 bytes), drawn as a 4-vertex triangle strip.
//
// Vertex shader (1A-20a/b): places a quad around each kind's shape in target pixels, padded by
// 1 pixel plus the edge softness so the antialiased fringe fits, and passes the shape's
// parameters (constant per instance) to the pixel shader.
// Pixel shader (1A-20b): evaluates the kind's signed distance d (pixels, negative inside) at the
// pixel center (SV_Position.xy): rounded box (RECT, SPRITE), circle, capsule (CAPSULE, GLYPH),
// triangle. A stroke s > 0 turns the shape into the band of width s inside its edge. Coverage =
// saturate(0.5 - d / (1 + softness)): every distance is an exact Euclidean distance in target
// pixels (instances are in pixel space, so no derivatives are needed), which makes the result
// independent of the rasterizer's derivative precision and reproducible on the CPU (tests).
// Output: color * texture (SPRITE only) * coverage, premultiplied.

cbuffer draw2d_frame : register(b0) {
    float2 target_size;           // pixels
    float2 inverse_target_size;   // 1 / target_size
};
Texture2D sprite_texture : register(t0);
SamplerState sprite_sampler : register(s0);

static const uint KIND_RECT = 0;
static const uint KIND_CIRCLE = 1;
static const uint KIND_CAPSULE = 2;
static const uint KIND_TRIANGLE = 3;
static const uint KIND_SPRITE = 4;
static const uint KIND_GLYPH = 5;

struct vs_in {
    float2 position : ATTR0;
    float2 size : ATTR1;
    float rotation : ATTR2;          // radians, y-down pixel space
    float4 color_abgr : ATTR3;       // bmf_color bytes in memory order: a, b, g, r
    float4 extra : ATTR4;            // uv rect, or the other capsule/triangle points
    uint kind_corner : ATTR5;        // kind (bits 0..7) | corner radius f16 (16..31)
    uint stroke_softness : ATTR6;    // stroke f16 (0..15) | softness f16 (16..31)
};

// shape_a / shape_b per kind (pixels):
//   RECT, SPRITE  a = (center.x, center.y, cos, sin), b = (half.x, half.y, corner radius, 0)
//   CIRCLE        a = (center.x, center.y, 0, 0),     b = (radius, 0, 0, 0)
//   CAPSULE/GLYPH a = (a.x, a.y, b.x, b.y),           b = (radius, 0, 0, 0)
//   TRIANGLE      a = (a.x, a.y, b.x, b.y),           b = (c.x, c.y, 0, 0)
struct vs_out {
    float4 position : SV_Position;
    float4 color : COLOR0;                              // straight alpha
    float2 uv : TEXCOORD0;                              // SPRITE only
    nointerpolation float4 shape_a : TEXCOORD1;
    nointerpolation float4 shape_b : TEXCOORD2;
    nointerpolation float2 stroke_softness : TEXCOORD3;
    nointerpolation uint kind : TEXCOORD4;
};

vs_out vs_main(vs_in input, uint vertex_id : SV_VertexID) {
    float2 corner = float2(float(vertex_id & 1u), float(vertex_id >> 1u));   // strip: (0,0) (1,0) (0,1) (1,1)
    uint kind = input.kind_corner & 0xFFu;
    float stroke = f16tof32(input.stroke_softness & 0xFFFFu);
    float softness = f16tof32(input.stroke_softness >> 16);
    float pad = 1.0 + softness;
    vs_out output;
    output.uv = float2(0.5, 0.5);
    float2 pixel;
    if (kind == KIND_CAPSULE || kind == KIND_GLYPH) {
        // Segment a..b with radius r: a box from a - (r + pad) dir to b + (r + pad) dir, 2 (r + pad) wide.
        float2 a = input.position;
        float2 b = input.extra.xy;
        float radius = input.size.y * 0.5;
        float2 axis = b - a;
        float axis_length = length(axis);
        float2 direction = axis_length > 0.0 ? axis / axis_length : float2(1.0, 0.0);
        float2 normal = float2(-direction.y, direction.x);
        float reach = radius + pad;
        pixel = a + direction * lerp(-reach, axis_length + reach, corner.x) + normal * lerp(-reach, reach, corner.y);
        output.shape_a = float4(a, b);
        output.shape_b = float4(radius, 0.0, 0.0, 0.0);
    } else if (kind == KIND_TRIANGLE) {
        float2 low = min(input.position, min(input.extra.xy, input.extra.zw)) - pad;
        float2 high = max(input.position, max(input.extra.xy, input.extra.zw)) + pad;
        pixel = lerp(low, high, corner);
        output.shape_a = float4(input.position, input.extra.xy);
        output.shape_b = float4(input.extra.zw, 0.0, 0.0);
    } else {
        // RECT, CIRCLE, SPRITE: centered box of `size` plus padding, rotated.
        float2 local = (corner - 0.5) * (input.size + 2.0 * pad);
        float s, c;
        sincos(input.rotation, s, c);
        pixel = input.position + float2(c * local.x - s * local.y, s * local.x + c * local.y);
        float2 half_size = input.size * 0.5;
        if (kind == KIND_CIRCLE) {
            output.shape_a = float4(input.position, 0.0, 0.0);
            output.shape_b = float4(half_size.x, 0.0, 0.0, 0.0);
        } else {
            output.shape_a = float4(input.position, c, s);
            output.shape_b = float4(half_size, kind == KIND_SPRITE ? 0.0 : f16tof32(input.kind_corner >> 16), 0.0);
        }
        if (kind == KIND_SPRITE) {
            // uv follows the unpadded rect; the padding extrapolates (clamped by the sampler).
            float2 t = input.size.x > 0.0 && input.size.y > 0.0 ? local / input.size + 0.5 : float2(0.5, 0.5);
            output.uv = lerp(input.extra.xy, input.extra.zw, t);
        }
    }
    output.position = float4(pixel.x * inverse_target_size.x * 2.0 - 1.0, 1.0 - pixel.y * inverse_target_size.y * 2.0, 0.0, 1.0);
    output.color = input.color_abgr.wzyx;
    output.stroke_softness = float2(stroke, softness);
    output.kind = kind;
    return output;
}

// Rounded box centered at 0 with half size `half_size` and corner radius r (clamped to the
// shorter half side).
float distance_round_box(float2 p, float2 half_size, float r) {
    r = min(r, min(half_size.x, half_size.y));
    float2 q = abs(p) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float distance_segment(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float length_sq = dot(ba, ba);
    float h = length_sq > 0.0 ? saturate(dot(pa, ba) / length_sq) : 0.0;
    return length(pa - ba * h);
}

// Exact signed distance to a triangle of either winding (negative inside).
float distance_triangle(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    float2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    float2 q0 = v0 - e0 * saturate(dot(v0, e0) / max(dot(e0, e0), 1e-12));
    float2 q1 = v1 - e1 * saturate(dot(v1, e1) / max(dot(e1, e1), 1e-12));
    float2 q2 = v2 - e2 * saturate(dot(v2, e2) / max(dot(e2, e2), 1e-12));
    float s = e0.x * e2.y - e0.y * e2.x >= 0.0 ? 1.0 : -1.0;
    float2 d0 = float2(dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x));
    float2 d1 = float2(dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x));
    float2 d2 = float2(dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x));
    float2 d = min(min(d0, d1), d2);
    return d.y > 0.0 ? -sqrt(d.x) : sqrt(d.x);
}

float4 ps_main(vs_out input) : SV_Target {
    float2 p = input.position.xy;   // pixel center, target pixels
    float d;
    if (input.kind == KIND_CIRCLE) {
        d = length(p - input.shape_a.xy) - input.shape_b.x;
    } else if (input.kind == KIND_CAPSULE || input.kind == KIND_GLYPH) {
        d = distance_segment(p, input.shape_a.xy, input.shape_a.zw) - input.shape_b.x;
    } else if (input.kind == KIND_TRIANGLE) {
        d = distance_triangle(p, input.shape_a.xy, input.shape_a.zw, input.shape_b.xy);
    } else {
        float2 offset = p - input.shape_a.xy;
        float c = input.shape_a.z, s = input.shape_a.w;
        float2 local = float2(c * offset.x + s * offset.y, c * offset.y - s * offset.x);   // inverse rotation
        d = distance_round_box(local, input.shape_b.xy, input.shape_b.z);
    }
    float stroke = input.stroke_softness.x;
    if (stroke > 0.0) d = abs(d + stroke * 0.5) - stroke * 0.5;
    float coverage = saturate(0.5 - d / (1.0 + input.stroke_softness.y));
    float4 color = input.color;
    if (input.kind == KIND_SPRITE) color *= sprite_texture.Sample(sprite_sampler, input.uv);
    float alpha = color.a * coverage;
    return float4(color.rgb * alpha, alpha);
}
