// Spike P0-08: D3D11 at build time (D3DCompile via LoadLibrary) and headless WARP render
// with hand-declared COM vtables. CRT-free. Throwaway.
typedef unsigned long long u64; typedef long long i64; typedef unsigned u32; typedef int i32;
typedef unsigned char u8; typedef float f32; typedef void* HANDLE; typedef i32 HRESULT;
#define WINAPI __attribute__((stdcall))
__declspec(dllimport) void WINAPI ExitProcess(u32 code);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(u32 n);
__declspec(dllimport) i32 WINAPI WriteFile(HANDLE h, const void* buf, u32 n, u32* written, void* ov);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char* name);
__declspec(dllimport) void* WINAPI GetProcAddress(HANDLE mod, const char* name);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char* name, u32 access, u32 share, void* sa, u32 disp, u32 flags, HANDLE tmpl);
__declspec(dllimport) i32 WINAPI CloseHandle(HANDLE h);
__declspec(dllimport) i32 WINAPI QueryPerformanceCounter(i64* c);
__declspec(dllimport) i32 WINAPI QueryPerformanceFrequency(i64* f);

static char obuf[1 << 20]; static i32 olen;
static void put(const char* s) { while (*s) obuf[olen++] = *s++; }
static void put_u(u64 v) { char b[24]; i32 i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); put(b + i); }
static void put_hex(u64 v, i32 digits) { for (i32 i = digits - 1; i >= 0; i--) obuf[olen++] = "0123456789abcdef"[(v >> (i * 4)) & 15]; }
static void flush(void) { u32 w; WriteFile(GetStdHandle((u32)-11), obuf, (u32)olen, &w, 0); olen = 0; }
static void fail(const char* what, HRESULT hr) { put("{\"ok\":0,\"error\":\""); put(what); put("\",\"hr\":\"0x"); put_hex((u32)hr, 8); put("\"}\n"); flush(); ExitProcess(1); }
static void write_file(const char* path, const void* data, u32 len) {
    HANDLE f = CreateFileA(path, 0x40000000u /*GENERIC_WRITE*/, 0, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
    u32 w; WriteFile(f, data, len, &w, 0); CloseHandle(f);
}

// ---------------------------------------------------------------- COM declarations (no d3d11.h)
// Each vtable lists every slot up to the last one used, in SDK order. "void* _N" marks unused slots.
typedef struct ID3DBlob ID3DBlob;
typedef struct { HRESULT (WINAPI *QueryInterface)(ID3DBlob*, const void*, void**); u32 (WINAPI *AddRef)(ID3DBlob*); u32 (WINAPI *Release)(ID3DBlob*);
                 void* (WINAPI *GetBufferPointer)(ID3DBlob*); u64 (WINAPI *GetBufferSize)(ID3DBlob*); } ID3DBlobVtbl;   // slots 0-4
struct ID3DBlob { const ID3DBlobVtbl* v; };

typedef struct ID3D11Device ID3D11Device;
typedef struct ID3D11DeviceContext ID3D11DeviceContext;
typedef struct { void* v; } ID3D11Resource, ID3D11Texture2D, ID3D11RenderTargetView, ID3D11VertexShader, ID3D11PixelShader;

typedef struct { u32 Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CPUAccessFlags, MiscFlags; } D3D11_TEXTURE2D_DESC;
typedef struct { f32 TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; } D3D11_VIEWPORT;
typedef struct { void* pData; u32 RowPitch; u32 DepthPitch; } D3D11_MAPPED_SUBRESOURCE;

typedef struct {
    void* _0_QueryInterface; void* _1_AddRef; u32 (WINAPI *Release)(ID3D11Device*);
    void* _3_CreateBuffer; void* _4_CreateTexture1D;
    HRESULT (WINAPI *CreateTexture2D)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const void* init, ID3D11Texture2D** out);     // 5
    void* _6_CreateTexture3D; void* _7_CreateShaderResourceView; void* _8_CreateUnorderedAccessView;
    HRESULT (WINAPI *CreateRenderTargetView)(ID3D11Device*, ID3D11Resource*, const void* desc, ID3D11RenderTargetView** out); // 9
    void* _10_CreateDepthStencilView; void* _11_CreateInputLayout;
    HRESULT (WINAPI *CreateVertexShader)(ID3D11Device*, const void* code, u64 len, void* linkage, ID3D11VertexShader** out);  // 12
    void* _13_CreateGeometryShader; void* _14_CreateGeometryShaderWithStreamOutput;
    HRESULT (WINAPI *CreatePixelShader)(ID3D11Device*, const void* code, u64 len, void* linkage, ID3D11PixelShader** out);    // 15
} ID3D11DeviceVtbl;
struct ID3D11Device { const ID3D11DeviceVtbl* v; };

typedef struct {
    void* _0_QueryInterface; void* _1_AddRef; u32 (WINAPI *Release)(ID3D11DeviceContext*);
    void* _3_GetDevice; void* _4_GetPrivateData; void* _5_SetPrivateData; void* _6_SetPrivateDataInterface;
    void* _7_VSSetConstantBuffers; void* _8_PSSetShaderResources;
    void (WINAPI *PSSetShader)(ID3D11DeviceContext*, ID3D11PixelShader*, void* const* classes, u32 n);       // 9
    void* _10_PSSetSamplers;
    void (WINAPI *VSSetShader)(ID3D11DeviceContext*, ID3D11VertexShader*, void* const* classes, u32 n);      // 11
    void* _12_DrawIndexed;
    void (WINAPI *Draw)(ID3D11DeviceContext*, u32 vertex_count, u32 start);                                   // 13
    HRESULT (WINAPI *Map)(ID3D11DeviceContext*, ID3D11Resource*, u32 sub, u32 type, u32 flags, D3D11_MAPPED_SUBRESOURCE* out); // 14
    void (WINAPI *Unmap)(ID3D11DeviceContext*, ID3D11Resource*, u32 sub);                                     // 15
    void* _16_PSSetConstantBuffers; void* _17_IASetInputLayout; void* _18_IASetVertexBuffers; void* _19_IASetIndexBuffer;
    void* _20_DrawIndexedInstanced; void* _21_DrawInstanced; void* _22_GSSetConstantBuffers; void* _23_GSSetShader;
    void (WINAPI *IASetPrimitiveTopology)(ID3D11DeviceContext*, u32 topology);                               // 24
    void* _25; void* _26; void* _27; void* _28; void* _29; void* _30; void* _31; void* _32;
    void (WINAPI *OMSetRenderTargets)(ID3D11DeviceContext*, u32 n, ID3D11RenderTargetView* const* rtvs, void* dsv); // 33
    void* _34; void* _35; void* _36; void* _37; void* _38; void* _39; void* _40; void* _41; void* _42; void* _43;
    void (WINAPI *RSSetViewports)(ID3D11DeviceContext*, u32 n, const D3D11_VIEWPORT* vps);                    // 44
    void* _45_RSSetScissorRects; void* _46_CopySubresourceRegion;
    void (WINAPI *CopyResource)(ID3D11DeviceContext*, ID3D11Resource* dst, ID3D11Resource* src);              // 47
    void* _48_UpdateSubresource; void* _49_CopyStructureCount;
    void (WINAPI *ClearRenderTargetView)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const f32 rgba[4]);   // 50
} ID3D11DeviceContextVtbl;
struct ID3D11DeviceContext { const ID3D11DeviceContextVtbl* v; };

typedef HRESULT (WINAPI *PFN_D3DCompile)(const void* src, u64 len, const char* name, const void* defines, void* include,
    const char* entry, const char* target, u32 flags1, u32 flags2, ID3DBlob** code, ID3DBlob** errors);
typedef HRESULT (WINAPI *PFN_D3D11CreateDevice)(void* adapter, u32 driver_type, HANDLE software, u32 flags, const u32* levels,
    u32 level_count, u32 sdk_version, ID3D11Device** device, u32* level, ID3D11DeviceContext** ctx);

static const char HLSL[] =
    "float4 vs_main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p[3] = { float2(0.0, 0.8), float2(0.8, -0.8), float2(-0.8, -0.8) };\n"
    "  return float4(p[id], 0.0, 1.0);\n"
    "}\n"
    "float4 ps_main() : SV_Target { return float4(1.0, 0.5, 0.0, 1.0); }\n";

static ID3DBlob* compile(PFN_D3DCompile D3DCompile, const char* entry, const char* target) {
    ID3DBlob* code = 0; ID3DBlob* errors = 0;
    HRESULT hr = D3DCompile(HLSL, sizeof HLSL - 1, "tri.hlsl", 0, 0, entry, target, 1u << 15 /*OPTIMIZATION_LEVEL3*/, 0, &code, &errors);
    if (hr < 0) { if (errors) { put((const char*)errors->v->GetBufferPointer(errors)); } fail("D3DCompile", hr); }
    return code;
}

static void emit_c_array(const char* name, ID3DBlob* b) {   // what `build shaders` would write to out/gen/
    const u8* p = (const u8*)b->v->GetBufferPointer(b); u64 n = b->v->GetBufferSize(b);
    put("static const unsigned char "); put(name); put("[] = {");
    for (u64 i = 0; i < n; i++) { put(i % 16 ? "" : "\n    "); put("0x"); put_hex(p[i], 2); put(","); }
    put("\n};\n");
}

void bmf_win32_entry(void) {
    i64 f, t0, t1, t2, t3; QueryPerformanceFrequency(&f);
    // ---- build time: compile HLSL to bytecode, emit as C arrays
    QueryPerformanceCounter(&t0);
    HANDLE dc = LoadLibraryA("d3dcompiler_47.dll"); if (!dc) fail("load d3dcompiler_47", 0);
    PFN_D3DCompile D3DCompile = (PFN_D3DCompile)GetProcAddress(dc, "D3DCompile");
    ID3DBlob* vs = compile(D3DCompile, "vs_main", "vs_5_0");
    ID3DBlob* ps = compile(D3DCompile, "ps_main", "ps_5_0");
    QueryPerformanceCounter(&t1);
    emit_c_array("vs_tri", vs); emit_c_array("ps_tri", ps);
    write_file("out/shaders_gen.h", obuf, (u32)olen); olen = 0;

    // ---- runtime: WARP device, offscreen render, staging readback
    HANDLE d3d = LoadLibraryA("d3d11.dll"); if (!d3d) fail("load d3d11", 0);
    PFN_D3D11CreateDevice CreateDevice = (PFN_D3D11CreateDevice)GetProcAddress(d3d, "D3D11CreateDevice");
    ID3D11Device* dev = 0; ID3D11DeviceContext* ctx = 0; u32 level = 0;
    const u32 levels[1] = { 0xb000 /*D3D_FEATURE_LEVEL_11_0*/ };
    QueryPerformanceCounter(&t2);
    HRESULT hr = CreateDevice(0, 5 /*D3D_DRIVER_TYPE_WARP*/, 0, 0x20 /*BGRA_SUPPORT*/, levels, 1, 7 /*D3D11_SDK_VERSION*/, &dev, &level, &ctx);
    QueryPerformanceCounter(&t3);
    if (hr < 0) fail("D3D11CreateDevice", hr);

    enum { W = 64, H = 64 };
    D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, 28 /*R8G8B8A8_UNORM*/, 1, 0, 0 /*DEFAULT*/, 0x20 /*BIND_RENDER_TARGET*/, 0, 0 };
    ID3D11Texture2D* rt = 0; hr = dev->v->CreateTexture2D(dev, &td, 0, &rt); if (hr < 0) fail("CreateTexture2D rt", hr);
    D3D11_TEXTURE2D_DESC sd = td; sd.Usage = 3 /*STAGING*/; sd.BindFlags = 0; sd.CPUAccessFlags = 0x20000 /*CPU_ACCESS_READ*/;
    ID3D11Texture2D* staging = 0; hr = dev->v->CreateTexture2D(dev, &sd, 0, &staging); if (hr < 0) fail("CreateTexture2D staging", hr);
    ID3D11RenderTargetView* rtv = 0; hr = dev->v->CreateRenderTargetView(dev, (ID3D11Resource*)rt, 0, &rtv); if (hr < 0) fail("CreateRTV", hr);
    ID3D11VertexShader* vsh = 0; hr = dev->v->CreateVertexShader(dev, vs->v->GetBufferPointer(vs), vs->v->GetBufferSize(vs), 0, &vsh); if (hr < 0) fail("CreateVS", hr);
    ID3D11PixelShader* psh = 0; hr = dev->v->CreatePixelShader(dev, ps->v->GetBufferPointer(ps), ps->v->GetBufferSize(ps), 0, &psh); if (hr < 0) fail("CreatePS", hr);

    const f32 clear[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    D3D11_VIEWPORT vp = { 0, 0, (f32)W, (f32)H, 0, 1 };
    ctx->v->OMSetRenderTargets(ctx, 1, &rtv, 0);
    ctx->v->RSSetViewports(ctx, 1, &vp);
    ctx->v->ClearRenderTargetView(ctx, rtv, clear);
    ctx->v->IASetPrimitiveTopology(ctx, 4 /*TRIANGLELIST*/);
    ctx->v->VSSetShader(ctx, vsh, 0, 0);
    ctx->v->PSSetShader(ctx, psh, 0, 0);
    ctx->v->Draw(ctx, 3, 0);
    ctx->v->CopyResource(ctx, (ID3D11Resource*)staging, (ID3D11Resource*)rt);
    D3D11_MAPPED_SUBRESOURCE m; hr = ctx->v->Map(ctx, (ID3D11Resource*)staging, 0, 1 /*MAP_READ*/, 0, &m); if (hr < 0) fail("Map", hr);
    static u8 pixels[W * H * 4];
    for (i32 y = 0; y < H; y++) for (i32 x = 0; x < W * 4; x++) pixels[y * W * 4 + x] = ((const u8*)m.pData)[y * m.RowPitch + (u32)x];
    ctx->v->Unmap(ctx, (ID3D11Resource*)staging, 0);
    write_file("out/tri_64x64.rgba", pixels, sizeof pixels);

    const u8* c0 = &pixels[0]; const u8* cc = &pixels[(40 * W + 32) * 4];
    i32 ok = c0[0] == 0 && c0[1] == 0 && c0[2] == 255 && c0[3] == 255 && cc[0] == 255 && cc[1] == 128 && cc[2] == 0 && cc[3] == 255;
    i32 covered = 0; for (i32 i = 0; i < W * H; i++) covered += pixels[i * 4] == 255;
    put("{\"ok\":"); put_u((u64)ok);
    put(",\"compile_us\":"); put_u((u64)((t1 - t0) * 1000000 / f));
    put(",\"warp_create_us\":"); put_u((u64)((t3 - t2) * 1000000 / f));
    put(",\"feature_level\":\"0x"); put_hex(level, 4);
    put("\",\"vs_bytes\":"); put_u(vs->v->GetBufferSize(vs)); put(",\"ps_bytes\":"); put_u(ps->v->GetBufferSize(ps));
    put(",\"row_pitch\":"); put_u(m.RowPitch); put(",\"covered_px\":"); put_u((u64)covered);
    put(",\"corner\":["); put_u(c0[0]); put(","); put_u(c0[1]); put(","); put_u(c0[2]); put(","); put_u(c0[3]);
    put("],\"center\":["); put_u(cc[0]); put(","); put_u(cc[1]); put(","); put_u(cc[2]); put(","); put_u(cc[3]); put("]}\n");
    flush();
    ctx->v->Release(ctx); dev->v->Release(dev);
    ExitProcess(ok ? 0 : 1);
}
