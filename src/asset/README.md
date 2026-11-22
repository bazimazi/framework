# src/asset

The runtime pak loader and procedural textures (SPEC §14.3). **Not implemented yet**: milestone M8, tasks 1B-32 (`asset.{h,c}`) and 1B-33 (`texgen.c`); the baker that writes the pak is `tools/bake/` (1B-30, 1B-31). Until then games have no textures or sampled sounds (sprites are internal: `bmf_draw_sprite` in `src/draw2d/draw2d.h`, used by the bench scene `tests/bench/sprites10k`).

## Invariants (to keep)
- The runtime does no decoding or parsing beyond the table of contents: blobs in the pak are already in their final GPU or mixer format (RGBA8 atlas pages, PCM16), 64-byte aligned.
- Asset IDs are compile-time constants from the generated `out/gen/<g>/assets.h` (`BMF_ASSET_<TYPE>_<NAME>`): a misspelled asset is a compile error, not a runtime failure.
- The TOC is validated before use (magic, version, every offset and size inside the file, PLAN 1B-32); a bad pak is an error, never a crash. The header's content hash is for the baker's incremental builds, not checked at load.
- Native builds memory-map the pak; web builds fetch it once into linear memory. Loading allocates from arenas only (D-16).
- Procedural textures (`bmf_tex_gen_*`: noise, fBm, gradients, SDF shapes) are generated at load time into atlas regions with the same skyline packer as the baker; they are presentation data, never state.

## Data layout (planned)
Pak: header (magic, version, content hash), TOC entries (id, type, offset, size, params), aligned blobs. Runtime: the mapped file, a GPU texture per atlas page, per-sprite atlas uv rects (`bmf_asset_sprite(id)`), sound buffers pointing into the mapping.

## Hot paths
None per frame: lookups are O(1) by ID; all work happens at load.

## Limits (planned)
Decoders for source formats (PNG, WAV; TTF to MSDF in Phase 2) belong to the baker. Exception today: the stored-deflate PNG reader (`base/base_png_read_stored.c`, used by golden tests) is compiled into every runtime; whether release runtimes should drop it is open.
