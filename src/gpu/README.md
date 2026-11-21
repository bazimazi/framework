# src/gpu

The backend-agnostic GPU API (SPEC §12.1, D-25, D-38). Internal: games draw through draw2d; nothing here is in `bmf.h`.

## Files
- `gpu.h`: the API (task 1A-13). Generational handles (`bmf_gpu_buffer/texture/sampler/pipeline/bindset`, one-member structs around a `u32` so kinds cannot be mixed up), descriptors, frame/pass/draw calls, two-phase readback, stats. Every function has its contract comment.
- `gpu.c`: the frontend. Handle pools, validation, frame and pass state, stats. With `BMF_GPU_BACKEND_NULL` it is the whole backend (D-38): every call is validated and counted, nothing is rendered. Backends hook in at the lines marked `// backend:`.
- `d3d11/gpu_d3d11.c` (1A-14): the D3D11 backend, included by `bmf.c` before `gpu.c` on Windows. Static `gpu_d3d11_*` functions that `gpu.c` calls at its `backend:` points (inside `#if BMF_OS_WINDOWS`). Done so far (1A-14a): device (hardware, WARP fallback, WARP when `software`), flip-model swapchain, frame-latency wait, present, occlusion, resize, surface passes (bind, viewport, clear), debug-layer message queue, device-lost panic.
- `webgpu/` (1B-42): empty so far.

## Invariants
- No bound state (D-25): every `bmf_gpu_draw` names its pipeline, bind set and vertex buffers. Backends skip redundant state changes themselves.
- All validation is in the frontend and identical for every backend, so the null backend in a native test catches what WebGPU would reject. Validation = `BMF_ASSERT` (debug only); each assert expression ends in a message `"bmf_gpu_<fn>: <rule>"`, which the crash JSON line prints. Backends never re-validate.
- Backends store native objects in arrays indexed by the frontend's pool slot index (`gpu_handle_slot`).
- The surface (window back buffer) is an ordinary texture slot with `is_surface` set: `RENDER_TARGET` only, cannot be destroyed, sampled or read. It exists only when `bmf_gpu_init` got a window.
- Pipelines bake in the target formats (WebGPU rule): a draw's pipeline `color_format`/`depth_format` must equal the pass's targets exactly (a pipeline with depth NONE cannot draw in a pass with a depth target).
- A bind set is made for a shader id and is usable with every pipeline of that shader. Slot i = HLSL `b<i>`/`t<i>`/`s<i>`; WGSL `@group(0)` bindings i / 8+i / 16+i.
- Vertex attribute location L = HLSL semantic `ATTR<L>`, WGSL `@location(L)`.
- Buffer writes (WebGPU queue-write semantics): between two `end_frame` calls (one "write period"), writes to one buffer go to increasing, non-overlapping ranges. Dynamic buffers: bytes not written in the current period are undefined (D3D11 `MAP_WRITE_DISCARD`).
- Destroying resources and resizing the surface happen outside a frame. Bind sets referring to a destroyed resource may live on but must not be drawn with (checked at draw).
- Readback is two-phase because WebGPU maps asynchronously: `read_begin`, then `read_end` until it stops returning `BMF_ERR_PENDING`. One read in flight. The null backend returns `BMF_ERR_UNSUPPORTED` from `read_begin` (the `shot` command maps it to `no_gpu`).
- Portable limits are the stricter of D3D11 and WebGPU defaults: buffers ≤ 256 MiB, uniform buffers ≤ 64 KiB with 256-byte binding offsets, textures ≤ 8192², vertex stride ≤ 2048, 16 vertex locations.

- D3D11: `d3d11.dll` is loaded with `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` once per process and never unloaded. The device is single-threaded (main thread rule), feature level 11_1 or 11_0. The DXGI factory comes from the device's adapter (a swapchain must come from the factory that made the device).
- D3D11 swapchain: `FLIP_DISCARD`, 2 buffers, BGRA8_UNORM, `SCALING_STRETCH`, `FRAME_LATENCY_WAITABLE_OBJECT` with maximum latency 1, `ALLOW_TEARING` when supported (used when vsync is off), DXGI Alt+Enter disabled (fullscreen is borderless, platform layer). The surface slot's render target view is recreated on resize.
- D3D11 frame pacing: `begin_frame` waits (1 s timeout) on the latency object once per presented frame: a frame that does not present keeps its "token", so the next frame does not wait again. A minimized window (`IsIconic`: flip-model `Present` keeps succeeding at once while minimized on Windows 11) or an occluded `Present` makes `begin_frame` return 0 without waiting (after an occluded present it only runs `Present(DXGI_PRESENT_TEST)` until that succeeds); surface passes are then validated and dropped by the frontend.
- D3D11 errors: device removed/reset/hung from any call = `bmf_panic("bmf_gpu: D3D11 device lost in <call>: <HRESULT>, removed reason <HRESULT> ...")`; a failed create = panic naming the call and the object label. With `debug_layer`, layer errors and corruption messages panic (backend bugs) and warnings go to stderr as `{"event":"warning","source":"d3d11",...}`; a missing layer (Windows optional feature "Graphics Tools") prints one warning and runs without it.

## Data layout
- `gpu_frontend` (one static struct): five `bmf_pool`s with their slot arrays, per-slot info (`gpu_buffer_info`, `gpu_texture_info`, copies of sampler/pipeline/bind set descriptors), frame/pass state, two `bmf_gpu_stats` (frame in progress, last completed). About 150 KiB of `.bss`, most of it 64 pipeline and 256 bind set descriptor copies.
- Pool capacities: buffers 256, textures 256 (incl. the surface), samplers 32, pipelines 64, bind sets 256. A full pool is a `bmf_panic` (fixed budget).

- `gpu_d3d11_state` (one static struct): device, context, debug interfaces, swapchain and latency handle, surface size and pacing flags, and render target / depth views per frontend texture slot (2 × 256 pointers).

## Hot paths
- `bmf_gpu_draw`: release = a few compares and counter updates; debug adds handle checks, format checks and an O(attributes) vertex range check per vertex buffer.
- `bmf_gpu_buffer_write`: O(1) bookkeeping plus the backend copy.

## Limits
- Main thread only.
- No index buffers, storage buffers, mipmaps, MSAA, scissor, viewport other than the full target, texture uploads after creation, or compute. Add them with their first user.
- Shader ids are only range-checked (`0..BMF_GPU_MAX_SHADERS-1`) until the generated shader table (1A-15) exists; backends then check bind sets against each shader's declared slots.
- D3D11 runs device, swapchain, frames and surface passes only (1A-14a). Buffers, textures, samplers, pipelines and bind sets are frontend-only until 1A-14b/c: do not render to or draw with them on D3D11 yet. WebGPU returns `BMF_ERR_UNSUPPORTED` until 1B-42.
- D3D11 has no device-loss recovery (panic). No exclusive fullscreen, HDR or MSAA swapchain.
- Tests: `tests/test_gpu.c` (lifecycle with and without a surface, a two-pass two-frame API usage run with exact stats, handle staleness and slot reuse, four expected-crash validation tests: draw outside a pass, pipeline/pass format mismatch, overlapping writes in one period, vertex read past the end). `tests/test_gpu_d3d11.c` (WARP with the debug layer: headless start with hardware request and forced WARP, hidden and shown window presenting, 1000-resize leak check through `ID3D11Debug` live objects, HRESULT names, device-lost panic as an expected crash).
