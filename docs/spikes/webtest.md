# Spike P0-07: headless browser test harness with no CDP client

Date: 2026-11-18. Browser: Edge 157 (`HeadlessChrome/157` user agent). GPU: Intel gen-12lp. Spike code: `spikes/webtest/` (throwaway).

## Result: works, including WebGPU without a GPU (SwiftShader)

## Design (as planned, amendment A-10)

`serve.c` (about 120 lines, winsock, CRT):
1. Binds `127.0.0.1:0` (ephemeral port, so parallel test runs do not collide).
2. Launches Edge with only command-line flags and a URL: `msedge.exe --headless=new --user-data-dir=<temp>\bmf_edge_<pid> --no-first-run --no-default-browser-check --disable-extensions <flags> http://127.0.0.1:<port>/<page>`.
3. Serves static files (MIME table with `application/wasm`, `Cache-Control: no-store`, COOP/COEP headers).
4. The page reports back with plain `fetch` POSTs: `/result` (JSON; the server prints it to stdout and exits 0 if it contains `"ok":1`, else 1), `/log` (to stderr), `/file/<name>` (binary body saved to `out/`).
5. Timeout (exit 3) if no result arrives.
6. Edge is started suspended, put into a Job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, then resumed. Closing the job handle kills Edge and all of its child processes; no stray `msedge.exe` remain.

No DevTools protocol client, no Node, no WebDriver.

## WebGPU adapter in headless mode

| Flags (in addition to the base flags above) | Adapter | Info | `requestAdapter`+`requestDevice` | Triangle render + readback |
|---|---|---|---|---|
| (none) | yes, hardware | intel / gen-12lp | 458 ms | 75 ms, pixels correct |
| `--enable-unsafe-webgpu` | yes, hardware | intel / gen-12lp | 164 ms | 16 ms, pixels correct |
| `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader` | yes, software | google / swiftshader, `isFallbackAdapter=true` | 508 ms | 11 ms, pixels correct |
| `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader --disable-gpu` | yes, software | google / swiftshader, fallback | 15 ms | 13 ms, pixels correct |
| `--disable-gpu` only | **no adapter** | - | - | - |

**Flags that give a working adapter without a GPU (CI, K-04):**
```
--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader --disable-gpu
```
This is also the most reproducible choice for golden images: the rasterizer is the same software implementation on every machine, which is the web analogue of WARP. Recommendation: `serve --test` uses exactly these flags by default; a `--gpu` option uses the hardware adapter for perf runs.

## Readback

Render pass (clear blue, one orange triangle) into a 64×64 `rgba8unorm` texture, `copyTextureToBuffer` (`bytesPerRow` = 256, which satisfies the 256-byte alignment rule at width 64), `mapAsync(READ)`. Pixel checks: corner `(0,0,255,255)`, center `(255,128,0,255)`: correct in every configuration with an adapter. The page also POSTed a PNG (made with `OffscreenCanvas.convertToBlob` for this spike only; bmf will encode PNGs in wasm with its own encoder), saved as `spikes/webtest/out/tri_*.png` and visually checked.

Note for 1B-42: for widths where `W*4` is not a multiple of 256, the readback rows must be padded and unpadded.

## Timings

Whole run (launch Edge, load page, adapter, render, readback, POST result, kill Edge): 1.5–2.4 s. The SwiftShader + `--disable-gpu` combination was the fastest (1.5 s).

## Inputs for later tasks

- 1B-45: keep the Job-object kill and the ephemeral port. Add WebSocket for the control bridge; keep POST `/result` for test mode.
- D-02: the dev server needs `ws2_32.dll` (Winsock). It is a system DLL but not in the D-02 list. Proposed: add it with the A-07 amendment.
- The page must POST a failure result from a `catch` block, otherwise a JS exception becomes a timeout instead of a clear error.
