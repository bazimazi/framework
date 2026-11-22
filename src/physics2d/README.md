# src/physics2d

Deterministic 2D rigid-body physics (SPEC §14.1, D-30, D-40). **Not implemented yet**: milestone M6, tasks 1B-01 (geometry and mass) to 1B-11 (debug draw). Nothing under this folder is compiled into any binary today; this header records the rules the code must follow when it lands.

## Files (planned, PLAN.md M6)
- `phys_geom.c` (1B-01), `phys_manifold.c` and `phys_distance.c` (1B-02), `phys_world.c` and the physics part of `../bmf.h` (1B-03), `phys_broad.c` (1B-04), `phys_contact.c` (1B-05), `phys_solver.c` (1B-06), `phys_joint.c` (1B-07), `phys_toi.c` (1B-08), `phys_query.c` (1B-09), `phys_island.c` (1B-10), `phys_debug.c` (1B-11). Tests in `tests/physics/`.

## Invariants (to keep)
- The world lives inside the game state block (D-21, D-30): `BMF_PHYS_DECL(name, max_bodies, max_shapes, max_joints, max_contacts)` declares a struct with the world header and every SoA array, fixed capacities chosen by the game. No pointers: bodies, shapes and joints are generational `u32` handles (pools, as `bmf_pool`).
- Everything reachable from a step is simulation code: SPEC §6 / D-37 rules (no `bmf_fast_*`, no FMA, no raw float-to-int casts, no NaN/Inf in state, fixed-order reductions). Every file here is scanned by the golden lint (tools/test/golden.c scans `src/physics2d/*.c` unconditionally).
- Deterministic order everywhere: broadphase pairs from radix-sorted cell keys, the contact table sorted by shape pair, constraints solved in contact-table order, islands built by union-find in that order, query results sorted by `(fraction, shape index)`.
- Derived data (broadphase cells, pair lists) lives in the frame or level arena and is rebuilt every tick; only persistent data (bodies, contacts with warm-start impulses, island and sleep state, events) is state.
- Events (begin/end touch, hit, sensor enter/exit) go to fixed arrays in state, readable in the same tick, with an overflow count.

## Data layout (planned)
SoA arrays per body (position, rotation, linear and angular velocity, inverse mass and inertia, flags, sleep timer) and per shape (type, body, local geometry, filter, friction, restitution, AABB). Polygons have at most 8 vertices with precomputed normals.

## Hot paths (targets)
Broadphase (spatial hash, radix sort) and the substepped solver (default 4 substeps, 1 relax iteration). Bench circles-2k-pile: tick < 2 ms desktop, < 6 ms mobile web, single thread (SPEC §14.1); narrowphase parallelized over the pair array by the job system (1B-12) with outputs written by index.

## Limits (planned)
Shapes: circle, capsule, convex polygon (≤ 8 vertices), segment, chain (static only). Joints in Phase 1: distance, revolute, weld, mouse/target. CCD only for `bullet` bodies against static geometry (TOI sweep) plus speculative contacts for all; fast balls must not tunnel through 0.1 m walls at 100 m/s at 60 Hz.
