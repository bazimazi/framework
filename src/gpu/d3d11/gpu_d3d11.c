// gpu_d3d11.c: the D3D11 backend of the GPU API (task 1A-14, D-26). Included by bmf.c before
// gpu.c on Windows; gpu.c calls the gpu_d3d11_* functions below at its `backend:` points with
// validated arguments and frontend pool slot indices. Nothing here re-validates.
//
// 1A-14a: device creation (hardware, falling back to WARP; WARP when forced), the DXGI flip-model
// swapchain (FLIP_DISCARD, 2 buffers, frame-latency waitable object, maximum latency 1, tearing
// when supported and vsync is off), resize, occlusion, passes, present, the debug layer's message
// queue, and device-removed detection.
// 1A-14b: buffers (dynamic: Map WRITE_DISCARD on the first write of a frame, NO_OVERWRITE after;
// default: UpdateSubresource1), textures with their views, samplers.
// 1A-14c: pipelines, draws and readback.
//
// Device requirements: the D3D11.1 runtime (ID3D11DeviceContext1) and the D3D11_OPTIONS
// ConstantBufferOffsetting and MapNoOverwriteOnDynamicConstantBuffer (uniform bindings with
// offsets, appending writes to dynamic uniform buffers; gpu.h). A hardware device without them is
// dropped for WARP, which has both.
//
// DLLs: d3d11.dll is loaded at runtime from System32 only (a --no-gpu run starts on machines
// without it, and no DLL next to the exe can stand in for it); DXGI comes through the device. It
// stays loaded for the life of the process: D3D11 keeps worker threads that must not outlive
// their code.
//
// Device lost (removed, reset, hung): bmf_panic with the operation, the HRESULT name and the
// device-removed reason. There is no device recovery in Phase 1; the crash line names the cause.

// Compiled shader bytecode for one shader id: vs_5_0 `vs_main` and ps_5_0 `ps_main`, plus the
// b/t/s registers the shader declares (VS and PS together). The table gpu_d3d11_shader_codes[]
// and the bmf_shader_id enum are generated from shaders/d3d11/*.hlsl by build.c (task 1A-15,
// out/gen/shaders_d3d11.h, debug or release bytecode by BMF_DEBUG). Tests add test shaders at
// high ids. A pipeline for an id without bytecode is a panic.
typedef struct gpu_d3d11_shader_code {
    const char* name;
    const u8* vertex_code;
    i32 vertex_code_size;
    const u8* pixel_code;
    i32 pixel_code_size;
    u32 uniform_mask;   // bit i: cbuffer register b<i>
    u32 texture_mask;   // bit i: texture register t<i>
    u32 sampler_mask;   // bit i: sampler register s<i>
} gpu_d3d11_shader_code;

#include "out/gen/shaders_d3d11.h"

// Every object of a pipeline, created at bmf_gpu_pipeline_create (none at draw time).
typedef struct gpu_d3d11_pipeline {
    ID3D11VertexShader* vertex_shader;
    ID3D11PixelShader* pixel_shader;
    ID3D11InputLayout* input_layout;      // 0 when the pipeline has no vertex buffers
    ID3D11BlendState* blend_state;
    ID3D11RasterizerState* rasterizer_state;
    ID3D11DepthStencilState* depth_stencil_state;
    u32 topology;                         // WIN32_D3D11_PRIMITIVE_TOPOLOGY_*
    i32 vertex_buffer_count;
    u32 vertex_strides[BMF_GPU_MAX_VERTEX_BUFFERS];
} gpu_d3d11_pipeline;

// A bind set as ready-to-bind arrays. Each non-zero object holds a reference (AddRef at create,
// Release at destroy), like a WebGPU bind group: destroying a resource it names cannot leave a
// dangling pointer here.
typedef struct gpu_d3d11_bindset {
    ID3D11Buffer* uniform_buffers[BMF_GPU_MAX_BIND_UNIFORMS];
    u32 uniform_first_constant[BMF_GPU_MAX_BIND_UNIFORMS];    // offset / 16
    u32 uniform_constant_count[BMF_GPU_MAX_BIND_UNIFORMS];    // size / 16, rounded up to a multiple of 16 (D3D11.1 rule)
    ID3D11ShaderResourceView* views[BMF_GPU_MAX_BIND_TEXTURES];
    ID3D11SamplerState* samplers[BMF_GPU_MAX_BIND_SAMPLERS];
} gpu_d3d11_bindset;

typedef struct gpu_d3d11_state {
    ID3D11Device* device;
    ID3D11DeviceContext* context;         // the ID3D11DeviceContext1 interface (slots 112+ usable)
    ID3D11InfoQueue* info_queue;         // debug layer only, else 0
    ID3D11Debug* debug;                   // debug layer only, else 0
    u32 feature_level;                    // WIN32_D3D_FEATURE_LEVEL_*
    b32 warp;                             // running on the WARP software rasterizer
    b32 tearing_supported;                // DXGI_FEATURE_PRESENT_ALLOW_TEARING
    b32 vsync;
    char adapter_name[256];               // UTF-8, from DXGI_ADAPTER_DESC

    // Surface (window back buffer). All 0 when started without a window.
    HWND hwnd;
    IDXGISwapChain2* swapchain;
    HANDLE frame_latency_waitable;
    u32 swapchain_flags;                  // creation flags, passed again to ResizeBuffers
    i32 surface_slot;                     // frontend texture slot of the surface, -1 without a window
    i32 surface_width;
    i32 surface_height;
    b32 latency_token_held;               // waited on the latency object; no present consumed it yet
    b32 occluded;                         // the last Present returned DXGI_STATUS_OCCLUDED
    u64 presents;                         // successful Present calls since init
    u64 occluded_frames;                  // begin_frame calls that returned 0 (minimized or occluded)

    // Native objects per frontend pool slot; 0 where the slot is free or the object is not needed.
    // The surface's texture slot holds only the swapchain back buffer's render target view.
    ID3D11Buffer* buffers[BMF_GPU_MAX_BUFFERS];
    b32 buffer_dynamic[BMF_GPU_MAX_BUFFERS];
    ID3D11Texture2D* textures[BMF_GPU_MAX_TEXTURES];
    ID3D11ShaderResourceView* shader_resource_views[BMF_GPU_MAX_TEXTURES];   // SAMPLED textures
    ID3D11RenderTargetView* render_target_views[BMF_GPU_MAX_TEXTURES];       // color RENDER_TARGET textures
    ID3D11DepthStencilView* depth_stencil_views[BMF_GPU_MAX_TEXTURES];       // depth textures
    ID3D11SamplerState* samplers[BMF_GPU_MAX_SAMPLERS];
    gpu_d3d11_pipeline pipelines[BMF_GPU_MAX_PIPELINES];
    gpu_d3d11_bindset bindsets[BMF_GPU_MAX_BINDSETS];

    // State bound in the current pass, to skip redundant calls between draws. -1 = unknown.
    i32 bound_pipeline;
    i32 bound_bindset;
    i32 bound_vertex_buffers[BMF_GPU_MAX_VERTEX_BUFFERS];
    i32 bound_vertex_offsets[BMF_GPU_MAX_VERTEX_BUFFERS];

    // Readback in flight (bmf_gpu_texture_read_begin/end).
    ID3D11Texture2D* readback_staging;
    i32 readback_width;
    i32 readback_height;
    b32 readback_bgra;                    // swap R and B into RGBA
} gpu_d3d11_state;

static gpu_d3d11_state gpu_d3d11;
static HMODULE gpu_d3d11_dll;             // process lifetime, see the header comment
static D3D11CreateDevice_fn gpu_d3d11_create_device;

// Names for the HRESULTs D3D11/DXGI calls return here; 0 for unknown codes.
static const char* gpu_d3d11_hresult_name(HRESULT hr) {
    switch (hr) {
    case 0: return "S_OK";
    case WIN32_DXGI_STATUS_OCCLUDED: return "DXGI_STATUS_OCCLUDED";
    case WIN32_DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
    case WIN32_DXGI_ERROR_NOT_FOUND: return "DXGI_ERROR_NOT_FOUND";
    case WIN32_DXGI_ERROR_UNSUPPORTED: return "DXGI_ERROR_UNSUPPORTED";
    case WIN32_DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
    case WIN32_DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
    case WIN32_DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
    case WIN32_DXGI_ERROR_WAS_STILL_DRAWING: return "DXGI_ERROR_WAS_STILL_DRAWING";
    case WIN32_DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case WIN32_DXGI_ERROR_SDK_COMPONENT_MISSING: return "DXGI_ERROR_SDK_COMPONENT_MISSING";
    case WIN32_E_OUTOFMEMORY: return "E_OUTOFMEMORY";
    case WIN32_E_INVALIDARG: return "E_INVALIDARG";
    case WIN32_E_FAIL: return "E_FAIL";
    default: return 0;
    }
}

// Writes "NAME (0x8xxxxxxx)" or "0x8xxxxxxx" for an HRESULT into `out`.
static void gpu_d3d11_hresult_text(HRESULT hr, char* out, i32 capacity) {
    const char* name = gpu_d3d11_hresult_name(hr);
    if (name) bmf_fmt_buf(out, capacity, "%s (0x%08x)", name, (u32)hr);
    else bmf_fmt_buf(out, capacity, "0x%08x", (u32)hr);
}

// Stops the process with a clean message when `hr` says the device is gone. `operation` names the
// call that reported it. Any other failure is left to the caller.
static void gpu_d3d11_check_device_lost(HRESULT hr, const char* operation) {
    if (hr != WIN32_DXGI_ERROR_DEVICE_REMOVED && hr != WIN32_DXGI_ERROR_DEVICE_RESET && hr != WIN32_DXGI_ERROR_DEVICE_HUNG) return;
    static char message[256];   // static: bmf_panic must not depend on the stack state of a dying device
    char result_text[64];
    char reason_text[64];
    gpu_d3d11_hresult_text(hr, result_text, (i32)sizeof result_text);
    gpu_d3d11_hresult_text(gpu_d3d11.device->lpVtbl->GetDeviceRemovedReason(gpu_d3d11.device), reason_text, (i32)sizeof reason_text);
    bmf_fmt_buf(message, (i32)sizeof message, "bmf_gpu: D3D11 device lost in %s: %s, removed reason %s (driver crash, update or GPU reset)",
                operation, result_text, reason_text);
    bmf_panic(message);
}

// Stops the process when creating a D3D11 object failed (out of GPU memory or device lost):
// creates are not allowed to fail (gpu.h).
static void gpu_d3d11_check_create(HRESULT hr, const char* operation, const char* label) {
    if (hr >= 0) return;
    gpu_d3d11_check_device_lost(hr, operation);
    static char message[256];
    char result_text[64];
    gpu_d3d11_hresult_text(hr, result_text, (i32)sizeof result_text);
    bmf_fmt_buf(message, (i32)sizeof message, "bmf_gpu: D3D11 %s failed for '%s': %s", operation, label ? label : "(no label)", result_text);
    bmf_panic(message);
}

// One {"event":"warning","source":"d3d11","message":...} line on stderr (until bmf_log, 1A-36).
static void gpu_d3d11_warn(const char* message, i32 message_length) {
    static char line[1200];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "warning");
    bmf_json_key(&w, "source"); bmf_json_string(&w, "d3d11");
    bmf_json_key(&w, "message"); bmf_json_str(&w, bmf_str_make(message, message_length));
    bmf_json_object_end(&w);
    line[w.len] = '\n';
    bmf_platform_write(BMF_STREAM_STDERR, line, w.len + 1);
}

// Debug layer only: empties the message queue. Errors and corruption are backend bugs (the
// frontend validated the call), so they stop the process with the layer's text; warnings go to
// stderr; info messages are dropped.
static void gpu_d3d11_drain_messages(void) {
    if (!gpu_d3d11.info_queue) return;
    ID3D11InfoQueue* queue = gpu_d3d11.info_queue;
    u64 count = queue->lpVtbl->GetNumStoredMessages(queue);
    static u64 message_storage[512];   // 4 KiB, 8-byte aligned for D3D11_MESSAGE
    for (u64 i = 0; i < count; i++) {
        size_t length = 0;
        if (queue->lpVtbl->GetMessage(queue, i, 0, &length) < 0 || length > sizeof message_storage) continue;
        D3D11_MESSAGE* message = (D3D11_MESSAGE*)message_storage;
        if (queue->lpVtbl->GetMessage(queue, i, message, &length) < 0) continue;
        i32 text_length = (i32)message->DescriptionByteLength;
        if (text_length > 0 && message->pDescription[text_length - 1] == 0) text_length--;   // the length counts the NUL
        if (message->Severity <= WIN32_D3D11_MESSAGE_SEVERITY_ERROR) {
            static char panic_message[1100];
            bmf_fmt_buf(panic_message, (i32)sizeof panic_message, "bmf_gpu: D3D11 debug layer error #%u: %S", message->ID,
                        bmf_str_make(message->pDescription, text_length));
            queue->lpVtbl->ClearStoredMessages(queue);
            bmf_panic(panic_message);
        }
        if (message->Severity == WIN32_D3D11_MESSAGE_SEVERITY_WARNING) gpu_d3d11_warn(message->pDescription, text_length);
    }
    queue->lpVtbl->ClearStoredMessages(queue);
}

// Creates the back buffer's render target view in the surface slot.
static void gpu_d3d11_surface_view_create(void) {
    ID3D11Texture2D* back_buffer = 0;
    HRESULT hr = gpu_d3d11.swapchain->lpVtbl->GetBuffer(gpu_d3d11.swapchain, 0, &WIN32_IID_ID3D11Texture2D, (void**)&back_buffer);
    gpu_d3d11_check_create(hr, "IDXGISwapChain::GetBuffer", "surface");
    ID3D11RenderTargetView* view = 0;
    hr = gpu_d3d11.device->lpVtbl->CreateRenderTargetView(gpu_d3d11.device, (ID3D11Resource*)back_buffer, 0, &view);
    back_buffer->lpVtbl->Release(back_buffer);
    gpu_d3d11_check_create(hr, "CreateRenderTargetView", "surface");
    gpu_d3d11.render_target_views[gpu_d3d11.surface_slot] = view;
}

static void gpu_d3d11_surface_view_release(void) {
    ID3D11RenderTargetView* view = gpu_d3d11.render_target_views[gpu_d3d11.surface_slot];
    if (view) view->lpVtbl->Release(view);
    gpu_d3d11.render_target_views[gpu_d3d11.surface_slot] = 0;
}

// Releases the objects of a pipeline slot and zeroes it. Safe on an empty slot.
static void gpu_d3d11_pipeline_release(gpu_d3d11_pipeline* pipeline) {
    if (pipeline->vertex_shader) pipeline->vertex_shader->lpVtbl->Release(pipeline->vertex_shader);
    if (pipeline->pixel_shader) pipeline->pixel_shader->lpVtbl->Release(pipeline->pixel_shader);
    if (pipeline->input_layout) pipeline->input_layout->lpVtbl->Release(pipeline->input_layout);
    if (pipeline->blend_state) pipeline->blend_state->lpVtbl->Release(pipeline->blend_state);
    if (pipeline->rasterizer_state) pipeline->rasterizer_state->lpVtbl->Release(pipeline->rasterizer_state);
    if (pipeline->depth_stencil_state) pipeline->depth_stencil_state->lpVtbl->Release(pipeline->depth_stencil_state);
    bmf_memset(pipeline, 0, sizeof *pipeline);
}

// Drops the references a bind set slot holds and zeroes it. Safe on an empty slot.
static void gpu_d3d11_bindset_release(gpu_d3d11_bindset* bindset) {
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_UNIFORMS; i++)
        if (bindset->uniform_buffers[i]) bindset->uniform_buffers[i]->lpVtbl->Release(bindset->uniform_buffers[i]);
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_TEXTURES; i++)
        if (bindset->views[i]) bindset->views[i]->lpVtbl->Release(bindset->views[i]);
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_SAMPLERS; i++)
        if (bindset->samplers[i]) bindset->samplers[i]->lpVtbl->Release(bindset->samplers[i]);
    bmf_memset(bindset, 0, sizeof *bindset);
}

// Releases everything gpu_d3d11_init created, in reverse order. Safe on a partly created state.
static void gpu_d3d11_release_all(void) {
    if (gpu_d3d11.context) {
        gpu_d3d11.context->lpVtbl->ClearState(gpu_d3d11.context);
        gpu_d3d11.context->lpVtbl->Flush(gpu_d3d11.context);
    }
    // Every resource still alive (the frontend's shutdown does not destroy them one by one).
    for (i32 i = 0; i < BMF_GPU_MAX_BUFFERS; i++)
        if (gpu_d3d11.buffers[i]) gpu_d3d11.buffers[i]->lpVtbl->Release(gpu_d3d11.buffers[i]);
    for (i32 i = 0; i < BMF_GPU_MAX_TEXTURES; i++) {
        if (gpu_d3d11.shader_resource_views[i]) gpu_d3d11.shader_resource_views[i]->lpVtbl->Release(gpu_d3d11.shader_resource_views[i]);
        if (gpu_d3d11.render_target_views[i]) gpu_d3d11.render_target_views[i]->lpVtbl->Release(gpu_d3d11.render_target_views[i]);
        if (gpu_d3d11.depth_stencil_views[i]) gpu_d3d11.depth_stencil_views[i]->lpVtbl->Release(gpu_d3d11.depth_stencil_views[i]);
        if (gpu_d3d11.textures[i]) gpu_d3d11.textures[i]->lpVtbl->Release(gpu_d3d11.textures[i]);
    }
    for (i32 i = 0; i < BMF_GPU_MAX_SAMPLERS; i++)
        if (gpu_d3d11.samplers[i]) gpu_d3d11.samplers[i]->lpVtbl->Release(gpu_d3d11.samplers[i]);
    for (i32 i = 0; i < BMF_GPU_MAX_PIPELINES; i++) gpu_d3d11_pipeline_release(&gpu_d3d11.pipelines[i]);
    for (i32 i = 0; i < BMF_GPU_MAX_BINDSETS; i++) gpu_d3d11_bindset_release(&gpu_d3d11.bindsets[i]);
    if (gpu_d3d11.readback_staging) gpu_d3d11.readback_staging->lpVtbl->Release(gpu_d3d11.readback_staging);
    if (gpu_d3d11.frame_latency_waitable) CloseHandle(gpu_d3d11.frame_latency_waitable);
    if (gpu_d3d11.swapchain) gpu_d3d11.swapchain->lpVtbl->Release(gpu_d3d11.swapchain);
    if (gpu_d3d11.info_queue) {
        gpu_d3d11_drain_messages();
        gpu_d3d11.info_queue->lpVtbl->Release(gpu_d3d11.info_queue);
    }
    if (gpu_d3d11.debug) gpu_d3d11.debug->lpVtbl->Release(gpu_d3d11.debug);
    if (gpu_d3d11.context) gpu_d3d11.context->lpVtbl->Release(gpu_d3d11.context);
    if (gpu_d3d11.device) gpu_d3d11.device->lpVtbl->Release(gpu_d3d11.device);
    bmf_memset(&gpu_d3d11, 0, sizeof gpu_d3d11);
    gpu_d3d11.surface_slot = -1;
}

// Starts the backend. `surface_slot` is the frontend texture slot that stands for the back buffer
// (-1 without a window). Returns BMF_ERR_UNSUPPORTED (with nothing left created) when D3D11 is
// missing, no device can be created, or the swapchain cannot be created for the window.
static bmf_err gpu_d3d11_init(const bmf_gpu_desc* desc, i32 surface_slot) {
    bmf_memset(&gpu_d3d11, 0, sizeof gpu_d3d11);
    gpu_d3d11.surface_slot = -1;
    gpu_d3d11.vsync = desc->vsync;

    if (!gpu_d3d11_dll) {
        static const WCHAR d3d11_name[] = { 'd', '3', 'd', '1', '1', '.', 'd', 'l', 'l', 0 };
        gpu_d3d11_dll = LoadLibraryExW(d3d11_name, 0, WIN32_LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!gpu_d3d11_dll) return BMF_ERR_UNSUPPORTED;
        gpu_d3d11_create_device = (D3D11CreateDevice_fn)GetProcAddress(gpu_d3d11_dll, "D3D11CreateDevice");
    }
    if (!gpu_d3d11_create_device) return BMF_ERR_UNSUPPORTED;

    // Device: hardware first unless WARP is forced; WARP when hardware fails. Single-threaded
    // (main thread only, gpu.h) avoids D3D11's internal locking. 11_1 first: constant buffer
    // offsets (BMF_GPU_UNIFORM_OFFSET_ALIGNMENT) need the 11.1 runtime.
    static const u32 feature_levels[] = { WIN32_D3D_FEATURE_LEVEL_11_1, WIN32_D3D_FEATURE_LEVEL_11_0 };
    u32 flags = WIN32_D3D11_CREATE_DEVICE_SINGLETHREADED | WIN32_D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    b32 debug_layer = desc->debug_layer;
    HRESULT hr = -1;
    for (i32 attempt = desc->software ? 1 : 0; attempt < 2; attempt++) {
        u32 driver_type = attempt == 0 ? WIN32_D3D_DRIVER_TYPE_HARDWARE : WIN32_D3D_DRIVER_TYPE_WARP;
        hr = gpu_d3d11_create_device(0, driver_type, 0, flags | (debug_layer ? WIN32_D3D11_CREATE_DEVICE_DEBUG : 0u), feature_levels,
                                     2, WIN32_D3D11_SDK_VERSION, &gpu_d3d11.device, &gpu_d3d11.feature_level, &gpu_d3d11.context);
        if (hr == WIN32_DXGI_ERROR_SDK_COMPONENT_MISSING && debug_layer) {
            // The debug layer is the optional Windows feature "Graphics Tools". Run without it.
            static const char missing[] = "debug layer requested but not installed (Windows optional feature 'Graphics Tools'); running without it";
            gpu_d3d11_warn(missing, (i32)sizeof missing - 1);
            debug_layer = 0;
            hr = gpu_d3d11_create_device(0, driver_type, 0, flags, feature_levels, 2, WIN32_D3D11_SDK_VERSION, &gpu_d3d11.device,
                                         &gpu_d3d11.feature_level, &gpu_d3d11.context);
        }
        if (hr < 0) continue;
        // Requirements (header comment). The context is replaced by its ID3D11DeviceContext1.
        ID3D11DeviceContext* context1 = 0;
        D3D11_FEATURE_DATA_D3D11_OPTIONS options;
        bmf_memset(&options, 0, sizeof options);
        hr = gpu_d3d11.context->lpVtbl->QueryInterface(gpu_d3d11.context, &WIN32_IID_ID3D11DeviceContext1, (void**)&context1);
        if (hr >= 0) {
            gpu_d3d11.context->lpVtbl->Release(gpu_d3d11.context);
            gpu_d3d11.context = context1;
            hr = gpu_d3d11.device->lpVtbl->CheckFeatureSupport(gpu_d3d11.device, WIN32_D3D11_FEATURE_D3D11_OPTIONS, &options, (u32)sizeof options);
        }
        if (hr >= 0 && options.ConstantBufferOffsetting && options.MapNoOverwriteOnDynamicConstantBuffer) {
            gpu_d3d11.warp = driver_type == WIN32_D3D_DRIVER_TYPE_WARP;
            break;
        }
        hr = WIN32_DXGI_ERROR_UNSUPPORTED;
        gpu_d3d11.context->lpVtbl->Release(gpu_d3d11.context);
        gpu_d3d11.device->lpVtbl->Release(gpu_d3d11.device);
        gpu_d3d11.context = 0;
        gpu_d3d11.device = 0;
    }
    if (hr < 0) {
        gpu_d3d11_release_all();
        return BMF_ERR_UNSUPPORTED;
    }

    if (debug_layer) {
        gpu_d3d11.device->lpVtbl->QueryInterface(gpu_d3d11.device, &WIN32_IID_ID3D11Debug, (void**)&gpu_d3d11.debug);
        gpu_d3d11.device->lpVtbl->QueryInterface(gpu_d3d11.device, &WIN32_IID_ID3D11InfoQueue, (void**)&gpu_d3d11.info_queue);
        if (gpu_d3d11.info_queue) gpu_d3d11.info_queue->lpVtbl->SetMessageCountLimit(gpu_d3d11.info_queue, 4096);
    }

    // The DXGI factory that created the device's adapter: a swapchain must come from it.
    IDXGIDevice* dxgi_device = 0;
    IDXGIAdapter* adapter = 0;
    IDXGIFactory2* factory = 0;
    hr = gpu_d3d11.device->lpVtbl->QueryInterface(gpu_d3d11.device, &WIN32_IID_IDXGIDevice, (void**)&dxgi_device);
    if (hr >= 0) hr = dxgi_device->lpVtbl->GetAdapter(dxgi_device, &adapter);
    if (hr >= 0) hr = adapter->lpVtbl->GetParent(adapter, &WIN32_IID_IDXGIFactory2, (void**)&factory);
    if (hr >= 0) {
        DXGI_ADAPTER_DESC adapter_desc;
        bmf_memset(&adapter_desc, 0, sizeof adapter_desc);
        adapter->lpVtbl->GetDesc(adapter, &adapter_desc);
        i32 name_length = 0;
        while (name_length < 128 && adapter_desc.Description[name_length]) name_length++;
        i32 bytes = bmf_utf16_to_utf8(adapter_desc.Description, name_length, (u8*)gpu_d3d11.adapter_name, (i32)sizeof gpu_d3d11.adapter_name - 1);
        if (bytes > (i32)sizeof gpu_d3d11.adapter_name - 1) bytes = 0;
        gpu_d3d11.adapter_name[bytes] = 0;

        IDXGIFactory5* factory5 = 0;
        if (factory->lpVtbl->QueryInterface(factory, &WIN32_IID_IDXGIFactory5, (void**)&factory5) >= 0) {
            i32 allow_tearing = 0;
            if (factory5->lpVtbl->CheckFeatureSupport(factory5, WIN32_DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, (u32)sizeof allow_tearing) >= 0)
                gpu_d3d11.tearing_supported = allow_tearing != 0;
            factory5->lpVtbl->Release(factory5);
        }
    }

    if (hr >= 0 && desc->window) {
        gpu_d3d11.hwnd = (HWND)desc->window;
        gpu_d3d11.surface_slot = surface_slot;
        gpu_d3d11.surface_width = desc->surface_width;
        gpu_d3d11.surface_height = desc->surface_height;
        gpu_d3d11.swapchain_flags = WIN32_DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                                    (gpu_d3d11.tearing_supported ? (u32)WIN32_DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
        bmf_memset(&swapchain_desc, 0, sizeof swapchain_desc);
        swapchain_desc.Width = (u32)desc->surface_width;
        swapchain_desc.Height = (u32)desc->surface_height;
        swapchain_desc.Format = WIN32_DXGI_FORMAT_B8G8R8A8_UNORM;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.BufferUsage = WIN32_DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.BufferCount = 2;
        swapchain_desc.Scaling = WIN32_DXGI_SCALING_STRETCH;
        swapchain_desc.SwapEffect = WIN32_DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.AlphaMode = WIN32_DXGI_ALPHA_MODE_IGNORE;
        swapchain_desc.Flags = gpu_d3d11.swapchain_flags;
        IDXGISwapChain1* swapchain1 = 0;
        hr = factory->lpVtbl->CreateSwapChainForHwnd(factory, gpu_d3d11.device, gpu_d3d11.hwnd, &swapchain_desc, 0, 0, &swapchain1);
        if (hr >= 0) {
            hr = swapchain1->lpVtbl->QueryInterface(swapchain1, &WIN32_IID_IDXGISwapChain2, (void**)&gpu_d3d11.swapchain);
            swapchain1->lpVtbl->Release(swapchain1);
        }
        if (hr >= 0) {
            // Fullscreen is borderless and handled by the platform layer (F11), never by DXGI.
            factory->lpVtbl->MakeWindowAssociation(factory, gpu_d3d11.hwnd, WIN32_DXGI_MWA_NO_ALT_ENTER | WIN32_DXGI_MWA_NO_WINDOW_CHANGES);
            hr = gpu_d3d11.swapchain->lpVtbl->SetMaximumFrameLatency(gpu_d3d11.swapchain, 1);
        }
        if (hr >= 0) {
            gpu_d3d11.frame_latency_waitable = gpu_d3d11.swapchain->lpVtbl->GetFrameLatencyWaitableObject(gpu_d3d11.swapchain);
            if (!gpu_d3d11.frame_latency_waitable) hr = -1;
        }
        if (hr >= 0) gpu_d3d11_surface_view_create();
    }

    if (factory) factory->lpVtbl->Release(factory);
    if (adapter) adapter->lpVtbl->Release(adapter);
    if (dxgi_device) dxgi_device->lpVtbl->Release(dxgi_device);
    if (hr < 0) {
        gpu_d3d11_release_all();
        return BMF_ERR_UNSUPPORTED;
    }
    gpu_d3d11_drain_messages();
    return BMF_OK;
}

static void gpu_d3d11_shutdown(void) {
    gpu_d3d11_release_all();
}

// Resizes the swapchain buffers. Outside a frame (frontend rule). Same size: nothing to do.
static void gpu_d3d11_surface_resize(i32 width, i32 height) {
    if (width == gpu_d3d11.surface_width && height == gpu_d3d11.surface_height) return;
    // ResizeBuffers needs every reference to the back buffers gone, bound views included.
    gpu_d3d11.context->lpVtbl->OMSetRenderTargets(gpu_d3d11.context, 0, 0, 0);
    gpu_d3d11_surface_view_release();
    HRESULT hr = gpu_d3d11.swapchain->lpVtbl->ResizeBuffers(gpu_d3d11.swapchain, 0, (u32)width, (u32)height, WIN32_DXGI_FORMAT_UNKNOWN,
                                                            gpu_d3d11.swapchain_flags);
    gpu_d3d11_check_create(hr, "IDXGISwapChain::ResizeBuffers", "surface");
    gpu_d3d11.surface_width = width;
    gpu_d3d11.surface_height = height;
    gpu_d3d11_surface_view_create();
    gpu_d3d11_drain_messages();
}

// ---------------------------------------------------------------- buffers
static void gpu_d3d11_buffer_create(i32 slot, const bmf_gpu_buffer_desc* desc) {
    D3D11_BUFFER_DESC buffer_desc;
    bmf_memset(&buffer_desc, 0, sizeof buffer_desc);
    buffer_desc.ByteWidth = (u32)desc->size;
    buffer_desc.Usage = desc->dynamic ? WIN32_D3D11_USAGE_DYNAMIC : WIN32_D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = desc->usage == BMF_GPU_BUFFER_UNIFORM ? WIN32_D3D11_BIND_CONSTANT_BUFFER : WIN32_D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = desc->dynamic ? (u32)WIN32_D3D11_CPU_ACCESS_WRITE : 0u;
    // gpu.h promises zeros without data; D3D11 does not. (Dynamic buffers promise nothing: bytes
    // not written in the current frame are undefined.) Default buffers up to 64 KiB, which
    // includes every uniform buffer, start from a zero array; larger ones are zero-filled in 64 KiB
    // copies (allowed: they are vertex buffers, which may be updated in part).
    static const u8 zeros[BMF_GPU_MAX_UNIFORM_BUFFER_SIZE];
    b32 zero_fill = !desc->data && !desc->dynamic;
    D3D11_SUBRESOURCE_DATA initial = { desc->data, 0, 0 };
    if (zero_fill && desc->size <= (i32)sizeof zeros) {
        initial.pSysMem = zeros;
        zero_fill = 0;
    }
    ID3D11Buffer* buffer = 0;
    HRESULT hr = gpu_d3d11.device->lpVtbl->CreateBuffer(gpu_d3d11.device, &buffer_desc, initial.pSysMem ? &initial : 0, &buffer);
    gpu_d3d11_check_create(hr, "CreateBuffer", desc->label);
    gpu_d3d11.buffers[slot] = buffer;
    gpu_d3d11.buffer_dynamic[slot] = desc->dynamic;
    if (zero_fill) {
        for (i32 offset = 0; offset < desc->size; offset += (i32)sizeof zeros) {
            i32 end = desc->size - offset > (i32)sizeof zeros ? offset + (i32)sizeof zeros : desc->size;
            D3D11_BOX box = { (u32)offset, 0, 0, (u32)end, 1, 1 };
            gpu_d3d11.context->lpVtbl->UpdateSubresource1(gpu_d3d11.context, (ID3D11Resource*)buffer, 0, &box, zeros, 0, 0, 0);
        }
    }
}

static void gpu_d3d11_buffer_destroy(i32 slot) {
    gpu_d3d11.buffers[slot]->lpVtbl->Release(gpu_d3d11.buffers[slot]);
    gpu_d3d11.buffers[slot] = 0;
    gpu_d3d11.buffer_dynamic[slot] = 0;
}

// `first_in_frame`: no earlier write to this buffer since the last end_frame. A dynamic buffer
// then gets fresh memory (WRITE_DISCARD: draws already submitted keep their data); later writes in
// the same frame append without a stall (NO_OVERWRITE, safe because the frontend guarantees
// increasing, non-overlapping ranges per frame). Default buffers (vertex only: the frontend allows
// writes to dynamic uniform buffers only): UpdateSubresource1, a queued copy the driver orders.
static void gpu_d3d11_buffer_write(i32 slot, i32 offset, const void* data, i32 size, b32 first_in_frame) {
    ID3D11Resource* resource = (ID3D11Resource*)gpu_d3d11.buffers[slot];
    if (gpu_d3d11.buffer_dynamic[slot]) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        u32 map_type = first_in_frame ? (u32)WIN32_D3D11_MAP_WRITE_DISCARD : (u32)WIN32_D3D11_MAP_WRITE_NO_OVERWRITE;
        HRESULT hr = gpu_d3d11.context->lpVtbl->Map(gpu_d3d11.context, resource, 0, map_type, 0, &mapped);
        gpu_d3d11_check_create(hr, "Map (buffer write)", "buffer");
        bmf_memcpy((u8*)mapped.pData + offset, data, size);
        gpu_d3d11.context->lpVtbl->Unmap(gpu_d3d11.context, resource, 0);
    } else {
        D3D11_BOX box = { (u32)offset, 0, 0, (u32)(offset + size), 1, 1 };
        gpu_d3d11.context->lpVtbl->UpdateSubresource1(gpu_d3d11.context, resource, 0, &box, data, 0, 0, 0);
    }
}

// ---------------------------------------------------------------- textures
static u32 gpu_d3d11_dxgi_format(bmf_gpu_format format) {
    switch (format) {
    case BMF_GPU_FORMAT_RGBA8_UNORM: return WIN32_DXGI_FORMAT_R8G8B8A8_UNORM;
    case BMF_GPU_FORMAT_RGBA8_UNORM_SRGB: return WIN32_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case BMF_GPU_FORMAT_BGRA8_UNORM: return WIN32_DXGI_FORMAT_B8G8R8A8_UNORM;
    case BMF_GPU_FORMAT_R8_UNORM: return WIN32_DXGI_FORMAT_R8_UNORM;
    case BMF_GPU_FORMAT_DEPTH24_STENCIL8: return WIN32_DXGI_FORMAT_D24_UNORM_S8_UINT;
    case BMF_GPU_FORMAT_NONE:
    case BMF_GPU_FORMAT_COUNT: break;
    }
    return WIN32_DXGI_FORMAT_UNKNOWN;
}

// Creates the texture and the views its usage needs: a shader resource view for SAMPLED, a render
// target view for a color RENDER_TARGET, a depth stencil view for depth. Textures with data that
// are not render targets are IMMUTABLE (contents never change after creation, gpu.h). COPY_SRC
// needs no bind flag: readback copies to a staging texture (1A-14c). sRGB textures use the
// _SRGB format itself, so sampling decodes and rendering encodes.
static void gpu_d3d11_texture_create(i32 slot, const bmf_gpu_texture_desc* desc) {
    b32 depth = desc->format == BMF_GPU_FORMAT_DEPTH24_STENCIL8;
    b32 render_target = (desc->usage & BMF_GPU_TEXTURE_RENDER_TARGET) != 0;
    b32 sampled = (desc->usage & BMF_GPU_TEXTURE_SAMPLED) != 0;
    D3D11_TEXTURE2D_DESC texture_desc;
    bmf_memset(&texture_desc, 0, sizeof texture_desc);
    texture_desc.Width = (u32)desc->width;
    texture_desc.Height = (u32)desc->height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = gpu_d3d11_dxgi_format(desc->format);
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = (!render_target && desc->data) ? WIN32_D3D11_USAGE_IMMUTABLE : WIN32_D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = (sampled ? (u32)WIN32_D3D11_BIND_SHADER_RESOURCE : 0u) |
                             (render_target ? (depth ? (u32)WIN32_D3D11_BIND_DEPTH_STENCIL : (u32)WIN32_D3D11_BIND_RENDER_TARGET) : 0u);
    i32 pixel_bytes = desc->format == BMF_GPU_FORMAT_R8_UNORM ? 1 : 4;
    D3D11_SUBRESOURCE_DATA initial = { desc->data, (u32)(desc->width * pixel_bytes), 0 };
    ID3D11Texture2D* texture = 0;
    HRESULT hr = gpu_d3d11.device->lpVtbl->CreateTexture2D(gpu_d3d11.device, &texture_desc, desc->data ? &initial : 0, &texture);
    gpu_d3d11_check_create(hr, "CreateTexture2D", desc->label);
    gpu_d3d11.textures[slot] = texture;
    ID3D11Resource* resource = (ID3D11Resource*)texture;
    if (sampled) {
        hr = gpu_d3d11.device->lpVtbl->CreateShaderResourceView(gpu_d3d11.device, resource, 0, &gpu_d3d11.shader_resource_views[slot]);
        gpu_d3d11_check_create(hr, "CreateShaderResourceView", desc->label);
    }
    if (render_target && !depth) {
        hr = gpu_d3d11.device->lpVtbl->CreateRenderTargetView(gpu_d3d11.device, resource, 0, &gpu_d3d11.render_target_views[slot]);
        gpu_d3d11_check_create(hr, "CreateRenderTargetView", desc->label);
    }
    if (depth) {
        hr = gpu_d3d11.device->lpVtbl->CreateDepthStencilView(gpu_d3d11.device, resource, 0, &gpu_d3d11.depth_stencil_views[slot]);
        gpu_d3d11_check_create(hr, "CreateDepthStencilView", desc->label);
    }
}

static void gpu_d3d11_texture_destroy(i32 slot) {
    // Unbind the output merger first: it may still reference this texture's views.
    gpu_d3d11.context->lpVtbl->OMSetRenderTargets(gpu_d3d11.context, 0, 0, 0);
    if (gpu_d3d11.shader_resource_views[slot]) gpu_d3d11.shader_resource_views[slot]->lpVtbl->Release(gpu_d3d11.shader_resource_views[slot]);
    if (gpu_d3d11.render_target_views[slot]) gpu_d3d11.render_target_views[slot]->lpVtbl->Release(gpu_d3d11.render_target_views[slot]);
    if (gpu_d3d11.depth_stencil_views[slot]) gpu_d3d11.depth_stencil_views[slot]->lpVtbl->Release(gpu_d3d11.depth_stencil_views[slot]);
    gpu_d3d11.textures[slot]->lpVtbl->Release(gpu_d3d11.textures[slot]);
    gpu_d3d11.shader_resource_views[slot] = 0;
    gpu_d3d11.render_target_views[slot] = 0;
    gpu_d3d11.depth_stencil_views[slot] = 0;
    gpu_d3d11.textures[slot] = 0;
}

// ---------------------------------------------------------------- samplers
static void gpu_d3d11_sampler_create(i32 slot, const bmf_gpu_sampler_desc* desc) {
    u32 address = desc->address == BMF_GPU_ADDRESS_REPEAT ? (u32)WIN32_D3D11_TEXTURE_ADDRESS_WRAP : (u32)WIN32_D3D11_TEXTURE_ADDRESS_CLAMP;
    D3D11_SAMPLER_DESC sampler_desc;
    bmf_memset(&sampler_desc, 0, sizeof sampler_desc);
    // Textures have one mip level, so the mip filter does not matter (POINT).
    sampler_desc.Filter = desc->filter == BMF_GPU_FILTER_LINEAR ? (u32)WIN32_D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
                                                                : (u32)WIN32_D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = address;
    sampler_desc.AddressV = address;
    sampler_desc.AddressW = address;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = WIN32_D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = BMF_F32_MAX;
    HRESULT hr = gpu_d3d11.device->lpVtbl->CreateSamplerState(gpu_d3d11.device, &sampler_desc, &gpu_d3d11.samplers[slot]);
    gpu_d3d11_check_create(hr, "CreateSamplerState", desc->label);
}

static void gpu_d3d11_sampler_destroy(i32 slot) {
    gpu_d3d11.samplers[slot]->lpVtbl->Release(gpu_d3d11.samplers[slot]);
    gpu_d3d11.samplers[slot] = 0;
}

// ---------------------------------------------------------------- pipelines
static u32 gpu_d3d11_vertex_format(bmf_gpu_vertex_format format) {
    switch (format) {
    case BMF_GPU_VERTEX_F32: return WIN32_DXGI_FORMAT_R32_FLOAT;
    case BMF_GPU_VERTEX_F32X2: return WIN32_DXGI_FORMAT_R32G32_FLOAT;
    case BMF_GPU_VERTEX_F32X3: return WIN32_DXGI_FORMAT_R32G32B32_FLOAT;
    case BMF_GPU_VERTEX_F32X4: return WIN32_DXGI_FORMAT_R32G32B32A32_FLOAT;
    case BMF_GPU_VERTEX_U32: return WIN32_DXGI_FORMAT_R32_UINT;
    case BMF_GPU_VERTEX_U8X4_UNORM: return WIN32_DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return WIN32_DXGI_FORMAT_UNKNOWN;
}

// Creates every object of the pipeline. Vertex attribute location L is semantic ATTR<L> (gpu.h).
// Blend presets (premultiplied colors everywhere): OPAQUE off; PREMULTIPLIED src ONE, dst
// INV_SRC_ALPHA; ADDITIVE ONE, ONE; alpha the same as color. Front faces are counter-clockwise.
// Depth: test = LESS_EQUAL; write without test = ALWAYS; neither = depth disabled.
static void gpu_d3d11_pipeline_create(i32 slot, const bmf_gpu_pipeline_desc* desc) {
    const gpu_d3d11_shader_code* code = &gpu_d3d11_shader_codes[desc->shader];
    if (!code->vertex_code || !code->pixel_code) {
        static char message[160];
        bmf_fmt_buf(message, (i32)sizeof message, "bmf_gpu_pipeline_create: shader id %i has no D3D11 bytecode (pipeline '%s')", desc->shader,
                    desc->label ? desc->label : "(no label)");
        bmf_panic(message);
    }
    ID3D11Device* device = gpu_d3d11.device;
    gpu_d3d11_pipeline* pipeline = &gpu_d3d11.pipelines[slot];
    HRESULT hr = device->lpVtbl->CreateVertexShader(device, code->vertex_code, (size_t)code->vertex_code_size, 0, &pipeline->vertex_shader);
    gpu_d3d11_check_create(hr, "CreateVertexShader", desc->label);
    hr = device->lpVtbl->CreatePixelShader(device, code->pixel_code, (size_t)code->pixel_code_size, 0, &pipeline->pixel_shader);
    gpu_d3d11_check_create(hr, "CreatePixelShader", desc->label);

    D3D11_INPUT_ELEMENT_DESC elements[BMF_GPU_MAX_VERTEX_BUFFERS * BMF_GPU_MAX_VERTEX_ATTRIBUTES];
    i32 element_count = 0;
    for (i32 b = 0; b < desc->vertex_buffer_count; b++) {
        const bmf_gpu_vertex_layout* layout = &desc->vertex_buffers[b];
        b32 per_instance = layout->step == BMF_GPU_STEP_INSTANCE;
        for (i32 a = 0; a < layout->attribute_count; a++) {
            D3D11_INPUT_ELEMENT_DESC* element = &elements[element_count++];
            element->SemanticName = "ATTR";
            element->SemanticIndex = (u32)layout->attributes[a].location;
            element->Format = gpu_d3d11_vertex_format(layout->attributes[a].format);
            element->InputSlot = (u32)b;
            element->AlignedByteOffset = (u32)layout->attributes[a].offset;
            element->InputSlotClass = per_instance ? (u32)WIN32_D3D11_INPUT_PER_INSTANCE_DATA : (u32)WIN32_D3D11_INPUT_PER_VERTEX_DATA;
            element->InstanceDataStepRate = per_instance ? 1u : 0u;
        }
        pipeline->vertex_strides[b] = (u32)layout->stride;
    }
    pipeline->vertex_buffer_count = desc->vertex_buffer_count;
    if (element_count > 0) {
        hr = device->lpVtbl->CreateInputLayout(device, elements, (u32)element_count, code->vertex_code, (size_t)code->vertex_code_size,
                                               &pipeline->input_layout);
        gpu_d3d11_check_create(hr, "CreateInputLayout", desc->label);
    }

    D3D11_BLEND_DESC blend;
    bmf_memset(&blend, 0, sizeof blend);
    D3D11_RENDER_TARGET_BLEND_DESC* target = &blend.RenderTarget[0];
    target->BlendEnable = desc->blend != BMF_GPU_BLEND_OPAQUE;
    target->SrcBlend = WIN32_D3D11_BLEND_ONE;
    target->DestBlend = desc->blend == BMF_GPU_BLEND_ADDITIVE ? (u32)WIN32_D3D11_BLEND_ONE
                      : desc->blend == BMF_GPU_BLEND_PREMULTIPLIED ? (u32)WIN32_D3D11_BLEND_INV_SRC_ALPHA : (u32)WIN32_D3D11_BLEND_ZERO;
    target->BlendOp = WIN32_D3D11_BLEND_OP_ADD;
    target->SrcBlendAlpha = target->SrcBlend;
    target->DestBlendAlpha = target->DestBlend;
    target->BlendOpAlpha = WIN32_D3D11_BLEND_OP_ADD;
    target->RenderTargetWriteMask = WIN32_D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->lpVtbl->CreateBlendState(device, &blend, &pipeline->blend_state);
    gpu_d3d11_check_create(hr, "CreateBlendState", desc->label);

    D3D11_RASTERIZER_DESC rasterizer;
    bmf_memset(&rasterizer, 0, sizeof rasterizer);
    rasterizer.FillMode = WIN32_D3D11_FILL_SOLID;
    rasterizer.CullMode = desc->cull == BMF_GPU_CULL_BACK ? (u32)WIN32_D3D11_CULL_BACK
                        : desc->cull == BMF_GPU_CULL_FRONT ? (u32)WIN32_D3D11_CULL_FRONT : (u32)WIN32_D3D11_CULL_NONE;
    rasterizer.FrontCounterClockwise = 1;
    rasterizer.DepthClipEnable = 1;
    hr = device->lpVtbl->CreateRasterizerState(device, &rasterizer, &pipeline->rasterizer_state);
    gpu_d3d11_check_create(hr, "CreateRasterizerState", desc->label);

    D3D11_DEPTH_STENCIL_DESC depth;
    bmf_memset(&depth, 0, sizeof depth);
    depth.DepthEnable = desc->depth_test || desc->depth_write;
    depth.DepthWriteMask = desc->depth_write ? (u32)WIN32_D3D11_DEPTH_WRITE_MASK_ALL : (u32)WIN32_D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = desc->depth_test ? (u32)WIN32_D3D11_COMPARISON_LESS_EQUAL : (u32)WIN32_D3D11_COMPARISON_ALWAYS;
    D3D11_DEPTH_STENCILOP_DESC keep = { WIN32_D3D11_STENCIL_OP_KEEP, WIN32_D3D11_STENCIL_OP_KEEP, WIN32_D3D11_STENCIL_OP_KEEP,
                                        WIN32_D3D11_COMPARISON_ALWAYS };
    depth.FrontFace = keep;
    depth.BackFace = keep;
    hr = device->lpVtbl->CreateDepthStencilState(device, &depth, &pipeline->depth_stencil_state);
    gpu_d3d11_check_create(hr, "CreateDepthStencilState", desc->label);

    pipeline->topology = desc->topology == BMF_GPU_TOPOLOGY_TRIANGLE_STRIP ? (u32)WIN32_D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
                       : desc->topology == BMF_GPU_TOPOLOGY_LINE_LIST ? (u32)WIN32_D3D11_PRIMITIVE_TOPOLOGY_LINELIST
                                                                      : (u32)WIN32_D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

static void gpu_d3d11_pipeline_destroy(i32 slot) {
    gpu_d3d11_pipeline_release(&gpu_d3d11.pipelines[slot]);
    gpu_d3d11.bound_pipeline = -1;
}

// ---------------------------------------------------------------- bind sets
// Slot index of a live frontend handle (the same split as gpu.c's gpu_handle_slot).
static inline i32 gpu_d3d11_handle_slot(u32 id) { return (i32)(id >> BMF_HANDLE_GENERATION_BITS); }

// Debug check for the frontend (gpu.c bmf_gpu_bindset_create): the shader has bytecode and the
// bind set fills exactly the registers it declares (generated metadata, gpu.h rule).
static inline b32 gpu_d3d11_bindset_fits_shader(const bmf_gpu_bindset_desc* desc) {
    const gpu_d3d11_shader_code* code = &gpu_d3d11_shader_codes[desc->shader];
    if (!code->vertex_code) return 0;
    u32 uniform_mask = 0, texture_mask = 0, sampler_mask = 0;
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_UNIFORMS; i++) if (desc->uniforms[i].buffer.id != 0) uniform_mask |= 1u << i;
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_TEXTURES; i++) if (desc->textures[i].id != 0) texture_mask |= 1u << i;
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_SAMPLERS; i++) if (desc->samplers[i].id != 0) sampler_mask |= 1u << i;
    return uniform_mask == code->uniform_mask && texture_mask == code->texture_mask && sampler_mask == code->sampler_mask;
}

static void gpu_d3d11_bindset_create(i32 slot, const bmf_gpu_bindset_desc* desc) {
    gpu_d3d11_bindset* bindset = &gpu_d3d11.bindsets[slot];
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_UNIFORMS; i++) {
        if (desc->uniforms[i].buffer.id == 0) continue;
        ID3D11Buffer* buffer = gpu_d3d11.buffers[gpu_d3d11_handle_slot(desc->uniforms[i].buffer.id)];
        buffer->lpVtbl->AddRef(buffer);
        bindset->uniform_buffers[i] = buffer;
        bindset->uniform_first_constant[i] = (u32)(desc->uniforms[i].offset / 16);
        bindset->uniform_constant_count[i] = (u32)((desc->uniforms[i].size / 16 + 15) & ~15);
    }
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_TEXTURES; i++) {
        if (desc->textures[i].id == 0) continue;
        ID3D11ShaderResourceView* view = gpu_d3d11.shader_resource_views[gpu_d3d11_handle_slot(desc->textures[i].id)];
        view->lpVtbl->AddRef(view);
        bindset->views[i] = view;
    }
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_SAMPLERS; i++) {
        if (desc->samplers[i].id == 0) continue;
        ID3D11SamplerState* sampler = gpu_d3d11.samplers[gpu_d3d11_handle_slot(desc->samplers[i].id)];
        sampler->lpVtbl->AddRef(sampler);
        bindset->samplers[i] = sampler;
    }
}

static void gpu_d3d11_bindset_destroy(i32 slot) {
    gpu_d3d11_bindset_release(&gpu_d3d11.bindsets[slot]);
    gpu_d3d11.bound_bindset = -1;
}

// ---------------------------------------------------------------- draw
// Binds what changed since the previous draw of the pass, then draws. `bindset_slot` is -1 for
// none (the previous bindings stay: the shader reads none of them).
static void gpu_d3d11_draw(i32 pipeline_slot, i32 bindset_slot, const i32* vertex_buffer_slots, const bmf_gpu_draw_desc* desc) {
    ID3D11DeviceContext* context = gpu_d3d11.context;
    const gpu_d3d11_pipeline* pipeline = &gpu_d3d11.pipelines[pipeline_slot];
    if (pipeline_slot != gpu_d3d11.bound_pipeline) {
        context->lpVtbl->IASetInputLayout(context, pipeline->input_layout);
        context->lpVtbl->IASetPrimitiveTopology(context, pipeline->topology);
        context->lpVtbl->VSSetShader(context, pipeline->vertex_shader, 0, 0);
        context->lpVtbl->PSSetShader(context, pipeline->pixel_shader, 0, 0);
        context->lpVtbl->OMSetBlendState(context, pipeline->blend_state, 0, 0xFFFFFFFFu);
        context->lpVtbl->RSSetState(context, pipeline->rasterizer_state);
        context->lpVtbl->OMSetDepthStencilState(context, pipeline->depth_stencil_state, 0);
        gpu_d3d11.bound_pipeline = pipeline_slot;
        // Strides belong to the pipeline: rebind the vertex buffers below.
        for (i32 b = 0; b < BMF_GPU_MAX_VERTEX_BUFFERS; b++) gpu_d3d11.bound_vertex_buffers[b] = -1;
    }
    if (bindset_slot >= 0 && bindset_slot != gpu_d3d11.bound_bindset) {
        const gpu_d3d11_bindset* bindset = &gpu_d3d11.bindsets[bindset_slot];
        context->lpVtbl->VSSetConstantBuffers1(context, 0, BMF_GPU_MAX_BIND_UNIFORMS, bindset->uniform_buffers, bindset->uniform_first_constant,
                                               bindset->uniform_constant_count);
        context->lpVtbl->PSSetConstantBuffers1(context, 0, BMF_GPU_MAX_BIND_UNIFORMS, bindset->uniform_buffers, bindset->uniform_first_constant,
                                               bindset->uniform_constant_count);
        context->lpVtbl->VSSetShaderResources(context, 0, BMF_GPU_MAX_BIND_TEXTURES, bindset->views);
        context->lpVtbl->PSSetShaderResources(context, 0, BMF_GPU_MAX_BIND_TEXTURES, bindset->views);
        context->lpVtbl->VSSetSamplers(context, 0, BMF_GPU_MAX_BIND_SAMPLERS, bindset->samplers);
        context->lpVtbl->PSSetSamplers(context, 0, BMF_GPU_MAX_BIND_SAMPLERS, bindset->samplers);
        gpu_d3d11.bound_bindset = bindset_slot;
    }
    for (i32 b = 0; b < pipeline->vertex_buffer_count; b++) {
        if (vertex_buffer_slots[b] == gpu_d3d11.bound_vertex_buffers[b] && desc->vertex_buffer_offsets[b] == gpu_d3d11.bound_vertex_offsets[b]) continue;
        ID3D11Buffer* buffer = gpu_d3d11.buffers[vertex_buffer_slots[b]];
        u32 offset = (u32)desc->vertex_buffer_offsets[b];
        context->lpVtbl->IASetVertexBuffers(context, (u32)b, 1, &buffer, &pipeline->vertex_strides[b], &offset);
        gpu_d3d11.bound_vertex_buffers[b] = vertex_buffer_slots[b];
        gpu_d3d11.bound_vertex_offsets[b] = desc->vertex_buffer_offsets[b];
    }
    context->lpVtbl->DrawInstanced(context, (u32)desc->vertex_count, (u32)desc->instance_count, (u32)desc->first_vertex, (u32)desc->first_instance);
}

// ---------------------------------------------------------------- readback
// Copies the texture to a new staging texture (the GPU copy is queued; read_end waits for it).
static void gpu_d3d11_read_begin(i32 slot, i32 width, i32 height, bmf_gpu_format format) {
    D3D11_TEXTURE2D_DESC staging_desc;
    bmf_memset(&staging_desc, 0, sizeof staging_desc);
    staging_desc.Width = (u32)width;
    staging_desc.Height = (u32)height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = gpu_d3d11_dxgi_format(format);
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = WIN32_D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = WIN32_D3D11_CPU_ACCESS_READ;
    HRESULT hr = gpu_d3d11.device->lpVtbl->CreateTexture2D(gpu_d3d11.device, &staging_desc, 0, &gpu_d3d11.readback_staging);
    gpu_d3d11_check_create(hr, "CreateTexture2D", "readback staging");
    gpu_d3d11.context->lpVtbl->CopyResource(gpu_d3d11.context, (ID3D11Resource*)gpu_d3d11.readback_staging,
                                            (ID3D11Resource*)gpu_d3d11.textures[slot]);
    gpu_d3d11.readback_width = width;
    gpu_d3d11.readback_height = height;
    gpu_d3d11.readback_bgra = format == BMF_GPU_FORMAT_BGRA8_UNORM;
}

// Maps the staging texture (stalls until the GPU is done) and writes RGBA rows top to bottom,
// honoring the driver's row pitch. Always finishes (D3D11 never returns BMF_ERR_PENDING).
static void gpu_d3d11_read_end(u8* out) {
    ID3D11Resource* staging = (ID3D11Resource*)gpu_d3d11.readback_staging;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = gpu_d3d11.context->lpVtbl->Map(gpu_d3d11.context, staging, 0, WIN32_D3D11_MAP_READ, 0, &mapped);
    gpu_d3d11_check_create(hr, "Map (readback)", "readback staging");
    i32 row_bytes = gpu_d3d11.readback_width * 4;
    for (i32 y = 0; y < gpu_d3d11.readback_height; y++) {
        const u8* src = (const u8*)mapped.pData + (i64)y * mapped.RowPitch;
        u8* dst = out + (i64)y * row_bytes;
        if (gpu_d3d11.readback_bgra) {
            for (i32 x = 0; x < row_bytes; x += 4) {
                dst[x + 0] = src[x + 2];
                dst[x + 1] = src[x + 1];
                dst[x + 2] = src[x + 0];
                dst[x + 3] = src[x + 3];
            }
        } else {
            bmf_memcpy(dst, src, row_bytes);
        }
    }
    gpu_d3d11.context->lpVtbl->Unmap(gpu_d3d11.context, staging, 0);
    gpu_d3d11.readback_staging->lpVtbl->Release(gpu_d3d11.readback_staging);
    gpu_d3d11.readback_staging = 0;
}

// ---------------------------------------------------------------- frame
// Returns 1 when the surface can be presented this frame. Waits on the frame-latency object
// first (at most one frame queued: lowest input latency), unless a wait from an earlier frame has
// not been consumed by a present yet. Minimized or occluded: returns 0 and never waits.
// Minimized is checked with IsIconic because flip-model Present keeps succeeding (and returns at
// once) for a minimized window on Windows 11, which would let the loop spin unthrottled.
static b32 gpu_d3d11_begin_frame(void) {
    if (!gpu_d3d11.swapchain) return 0;
    if (IsIconic(gpu_d3d11.hwnd)) {
        gpu_d3d11.occluded_frames++;
        return 0;
    }
    if (gpu_d3d11.occluded) {
        HRESULT hr = gpu_d3d11.swapchain->lpVtbl->Present(gpu_d3d11.swapchain, 0, WIN32_DXGI_PRESENT_TEST);
        gpu_d3d11_check_device_lost(hr, "IDXGISwapChain::Present(TEST)");
        if (hr == WIN32_DXGI_STATUS_OCCLUDED) {
            gpu_d3d11.occluded_frames++;
            return 0;
        }
        gpu_d3d11.occluded = 0;
    }
    if (!gpu_d3d11.latency_token_held) {
        // 1 s timeout: never hang the loop if DXGI does not signal (the frame then runs unthrottled).
        WaitForSingleObjectEx(gpu_d3d11.frame_latency_waitable, 1000, 1);
        gpu_d3d11.latency_token_held = 1;
    }
    return 1;
}

// Presents when a pass rendered to the surface this frame; otherwise only flushes, so offscreen
// work (headless rendering) starts on the GPU.
static void gpu_d3d11_end_frame(b32 present) {
    if (present) {
        u32 sync_interval = gpu_d3d11.vsync ? 1u : 0u;
        u32 flags = (!gpu_d3d11.vsync && gpu_d3d11.tearing_supported) ? (u32)WIN32_DXGI_PRESENT_ALLOW_TEARING : 0u;
        HRESULT hr = gpu_d3d11.swapchain->lpVtbl->Present(gpu_d3d11.swapchain, sync_interval, flags);
        gpu_d3d11_check_device_lost(hr, "IDXGISwapChain::Present");
        if (hr == WIN32_DXGI_STATUS_OCCLUDED) {
            // Nothing was queued, so the latency object may not signal again: keep the token.
            gpu_d3d11.occluded = 1;
        } else {
            gpu_d3d11_check_create(hr, "IDXGISwapChain::Present", "surface");
            gpu_d3d11.latency_token_held = 0;
            gpu_d3d11.presents++;
        }
    } else {
        gpu_d3d11.context->lpVtbl->Flush(gpu_d3d11.context);
    }
    gpu_d3d11_drain_messages();
}

// Binds the pass targets with a full-target viewport and applies the clears. `depth_slot` is -1
// for no depth target.
static void gpu_d3d11_begin_pass(i32 color_slot, i32 depth_slot, i32 width, i32 height, const bmf_gpu_pass_desc* desc) {
    ID3D11DeviceContext* context = gpu_d3d11.context;
    // Unbind every shader resource first: a texture sampled in an earlier pass may be a target now.
    static ID3D11ShaderResourceView* const no_views[BMF_GPU_MAX_BIND_TEXTURES];
    context->lpVtbl->VSSetShaderResources(context, 0, BMF_GPU_MAX_BIND_TEXTURES, no_views);
    context->lpVtbl->PSSetShaderResources(context, 0, BMF_GPU_MAX_BIND_TEXTURES, no_views);
    gpu_d3d11.bound_pipeline = -1;
    gpu_d3d11.bound_bindset = -1;
    for (i32 b = 0; b < BMF_GPU_MAX_VERTEX_BUFFERS; b++) gpu_d3d11.bound_vertex_buffers[b] = -1;
    ID3D11RenderTargetView* color_view = gpu_d3d11.render_target_views[color_slot];
    ID3D11DepthStencilView* depth_view = depth_slot >= 0 ? gpu_d3d11.depth_stencil_views[depth_slot] : 0;
    context->lpVtbl->OMSetRenderTargets(context, 1, &color_view, depth_view);
    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f };
    context->lpVtbl->RSSetViewports(context, 1, &viewport);
    if (desc->color_load == BMF_GPU_LOAD_CLEAR) {
        f32 color[4] = { desc->clear_color.x, desc->clear_color.y, desc->clear_color.z, desc->clear_color.w };
        context->lpVtbl->ClearRenderTargetView(context, color_view, color);
    }
    if (depth_view && desc->depth_load == BMF_GPU_LOAD_CLEAR)
        context->lpVtbl->ClearDepthStencilView(context, depth_view, WIN32_D3D11_CLEAR_DEPTH | WIN32_D3D11_CLEAR_STENCIL, desc->clear_depth, 0);
}

#if defined(BMF_BUILD_TESTS)
// Tests only: copies the first `size` bytes of a buffer to `out` through a staging buffer (stalls).
static void gpu_d3d11_test_read_buffer(i32 slot, void* out, i32 size) {
    D3D11_BUFFER_DESC staging_desc = { (u32)size, WIN32_D3D11_USAGE_STAGING, 0, WIN32_D3D11_CPU_ACCESS_READ, 0, 0 };
    ID3D11Buffer* staging = 0;
    HRESULT hr = gpu_d3d11.device->lpVtbl->CreateBuffer(gpu_d3d11.device, &staging_desc, 0, &staging);
    gpu_d3d11_check_create(hr, "CreateBuffer", "test readback");
    D3D11_BOX box = { 0, 0, 0, (u32)size, 1, 1 };
    gpu_d3d11.context->lpVtbl->CopySubresourceRegion(gpu_d3d11.context, (ID3D11Resource*)staging, 0, 0, 0, 0,
                                                     (ID3D11Resource*)gpu_d3d11.buffers[slot], 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = gpu_d3d11.context->lpVtbl->Map(gpu_d3d11.context, (ID3D11Resource*)staging, 0, WIN32_D3D11_MAP_READ, 0, &mapped);
    gpu_d3d11_check_create(hr, "Map (test readback)", "test readback");
    bmf_memcpy(out, mapped.pData, size);
    gpu_d3d11.context->lpVtbl->Unmap(gpu_d3d11.context, (ID3D11Resource*)staging, 0);
    staging->lpVtbl->Release(staging);
}

// Tests only: queues a GPU copy from the buffer (without waiting for it), so the buffer is in use
// by submitted work, as after a draw. A WRITE_DISCARD then has to hand out new memory.
static void gpu_d3d11_test_queue_buffer_use(i32 slot, i32 size) {
    D3D11_BUFFER_DESC copy_desc = { (u32)size, WIN32_D3D11_USAGE_DEFAULT, WIN32_D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    ID3D11Buffer* copy = 0;
    HRESULT hr = gpu_d3d11.device->lpVtbl->CreateBuffer(gpu_d3d11.device, &copy_desc, 0, &copy);
    gpu_d3d11_check_create(hr, "CreateBuffer", "test use");
    D3D11_BOX box = { 0, 0, 0, (u32)size, 1, 1 };
    gpu_d3d11.context->lpVtbl->CopySubresourceRegion(gpu_d3d11.context, (ID3D11Resource*)copy, 0, 0, 0, 0,
                                                     (ID3D11Resource*)gpu_d3d11.buffers[slot], 0, &box);
    copy->lpVtbl->Release(copy);
}

// Debug layer only (tests): the number of live device objects, counted from the messages of
// ReportLiveDeviceObjects. -1 without the debug layer. Drains the queue first.
static i32 gpu_d3d11_live_object_count(void) {
    if (!gpu_d3d11.debug || !gpu_d3d11.info_queue) return -1;
    gpu_d3d11_drain_messages();
    gpu_d3d11.debug->lpVtbl->ReportLiveDeviceObjects(gpu_d3d11.debug, WIN32_D3D11_RLDO_DETAIL | WIN32_D3D11_RLDO_IGNORE_INTERNAL);
    i32 count = (i32)gpu_d3d11.info_queue->lpVtbl->GetNumStoredMessages(gpu_d3d11.info_queue);
    gpu_d3d11.info_queue->lpVtbl->ClearStoredMessages(gpu_d3d11.info_queue);
    return count;
}
#endif
