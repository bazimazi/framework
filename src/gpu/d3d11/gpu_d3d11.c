// gpu_d3d11.c: the D3D11 backend of the GPU API (task 1A-14, D-26). Included by bmf.c before
// gpu.c on Windows; gpu.c calls the gpu_d3d11_* functions below at its `backend:` points with
// validated arguments and frontend pool slot indices. Nothing here re-validates.
//
// 1A-14a (this part): device creation (hardware, falling back to WARP; WARP when forced), the
// DXGI flip-model swapchain (FLIP_DISCARD, 2 buffers, frame-latency waitable object, maximum
// latency 1, tearing when supported and vsync is off), resize, occlusion, passes to the surface,
// present, the debug layer's message queue, and device-removed detection.
// 1A-14b adds buffers, textures, samplers and views; 1A-14c pipelines, draws and readback.
//
// DLLs: d3d11.dll and dxgi.dll are loaded at runtime from System32 only (a --no-gpu run starts on
// machines without them, and no DLL next to the exe can stand in for them). They stay loaded for
// the life of the process: D3D11 keeps worker threads that must not outlive their code.
//
// Device lost (removed, reset, hung): bmf_panic with the operation, the HRESULT name and the
// device-removed reason. There is no device recovery in Phase 1; the crash line names the cause.

typedef struct gpu_d3d11_state {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11InfoQueue* info_queue;          // debug layer only, else 0
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

    // Render target and depth views per frontend texture slot (the surface's slot holds the
    // swapchain back buffer view). Textures other than the surface come with 1A-14b.
    ID3D11RenderTargetView* render_target_views[BMF_GPU_MAX_TEXTURES];
    ID3D11DepthStencilView* depth_stencil_views[BMF_GPU_MAX_TEXTURES];
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

// Releases everything gpu_d3d11_init created, in reverse order. Safe on a partly created state.
static void gpu_d3d11_release_all(void) {
    if (gpu_d3d11.context) {
        gpu_d3d11.context->lpVtbl->ClearState(gpu_d3d11.context);
        gpu_d3d11.context->lpVtbl->Flush(gpu_d3d11.context);
    }
    if (gpu_d3d11.surface_slot >= 0) gpu_d3d11_surface_view_release();
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
        if (hr >= 0) {
            gpu_d3d11.warp = driver_type == WIN32_D3D_DRIVER_TYPE_WARP;
            break;
        }
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
