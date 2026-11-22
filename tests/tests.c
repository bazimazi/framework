// tests.c: the explicit test registry (task 1A-08). No auto-registration: every test is listed
// in TESTS below. Add a test file with #include and its tests to the table.
// Naming: "<module>.<what>". Order: by module, in the order the modules are built.

#include "test_runner.c"
#include "test_base_builtins.c"
#include "test_base_memory.c"
#include "test_base_simd.c"
#include "test_base_hash_rng_sort.c"
#include "test_math.c"
#include "test_base_string.c"
#include "test_platform.c"
#include "test_platform_window.c"
#include "test_gpu.c"
#include "test_gpu_d3d11.c"
#include "test_draw2d.c"
#include "test_draw2d_text.c"
#include "test_input.c"
#include "test_core.c"
#include "test_reflect.c"
#include "test_protocol.c"
#include "test_png.c"
#include "test_shot.c"
#include "test_replay.c"
#include "test_snap.c"
#include "test_crash.c"
#include "test_profile.c"
#include "test_golden.c"
#include "test_perf.c"

static const bmf_test_entry TESTS[] = {
    { "runner.check_records_location", test_runner_check_records_location, 0 },
    { "runner.eq_messages",            test_runner_eq_messages,            0 },

    { "base.builtins.memcpy",          test_builtins_memcpy,               0 },
    { "base.builtins.memset",          test_builtins_memset,               0 },
    { "base.builtins.memmove_overlap", test_builtins_memmove_overlap,      0 },
    { "base.builtins.memcmp",          test_builtins_memcmp,               0 },
    { "base.builtins.stack_probe",     test_builtins_stack_probe,          0 },
    { "base.builtins.float_use",       test_builtins_float_use,            0 },
    { "base.builtins.assert_crashes",  test_builtins_assert_crashes,       1 },
    { "base.builtins.panic_crashes",   test_builtins_panic_crashes,        1 },

    { "base.memory.arena_alignment",          test_memory_arena_alignment,            0 },
    { "base.memory.arena_pop_reset_fill",     test_memory_arena_pop_reset_fill,       0 },
    { "base.memory.arena_high_water_commit",  test_memory_arena_high_water_and_commit, 0 },
    { "base.memory.arena_guard_faults",       test_memory_arena_guard_faults,         1 },
    { "base.memory.arena_overflow_panics",    test_memory_arena_overflow_panics,      1 },
    { "base.memory.layout_and_scratch",       test_memory_layout_and_scratch,         0 },
    { "base.memory.pool_basic",               test_memory_pool_basic,                 0 },
    { "base.memory.pool_generations",         test_memory_pool_generations,           0 },
    { "base.memory.pool_dense_iteration",     test_memory_pool_dense_iteration,       0 },

    { "base.simd.matches_scalar",             test_simd_matches_scalar,               0 },
    { "base.simd.integer_and_memory",         test_simd_integer_and_memory,           0 },

    { "base.hash.stream_equals_oneshot",      test_hash_stream_equals_oneshot,        0 },
    { "base.hash.avalanche",                  test_hash_avalanche,                    0 },
    { "base.hash.crc32_adler32_vectors",      test_hash_crc32_adler32_vectors,        0 },
    { "base.rng.pcg32_vectors",               test_rng_pcg32_vectors,                 0 },
    { "base.rng.ranges",                      test_rng_ranges,                        0 },
    { "base.sort.matches_insertion_stable",   test_sort_matches_insertion_and_is_stable, 0 },

    { "base.math.constants",                  test_math_constants,                    0 },
    { "base.math.exact_ops",                  test_math_exact_ops,                    0 },
    { "base.math.special_values",             test_math_special_values,               0 },
    { "base.math.reduction_paths",            test_math_reduction_paths,              0 },
    { "base.math.sincos_matches",             test_math_sincos_matches,               0 },
    { "base.math.ulp_sin",                    test_math_ulp_sin,                      0 },
    { "base.math.ulp_cos",                    test_math_ulp_cos,                      0 },
    { "base.math.ulp_tan",                    test_math_ulp_tan,                      0 },
    { "base.math.ulp_asin",                   test_math_ulp_asin,                     0 },
    { "base.math.ulp_acos",                   test_math_ulp_acos,                     0 },
    { "base.math.ulp_atan",                   test_math_ulp_atan,                     0 },
    { "base.math.ulp_atan2",                  test_math_ulp_atan2,                    0 },
    { "base.math.ulp_exp",                    test_math_ulp_exp,                      0 },
    { "base.math.ulp_exp2",                   test_math_ulp_exp2,                     0 },
    { "base.math.ulp_log",                    test_math_ulp_log,                      0 },
    { "base.math.ulp_log2",                   test_math_ulp_log2,                     0 },
    { "base.math.ulp_pow",                    test_math_ulp_pow,                      0 },
    { "base.math.ulp_hypot",                  test_math_ulp_hypot,                    0 },
    { "base.math.scalar_helpers",             test_math_scalar_helpers,               0 },
    { "base.math.vectors",                    test_math_vectors,                      0 },
    { "base.math.rot_and_transform",          test_math_rot_and_transform,            0 },
    { "base.math.rect_aabb",                  test_math_rect_aabb,                    0 },
    { "base.math.colors",                     test_math_colors,                       0 },
    { "base.math.vector_output_hash",         test_math_vector_output_hash,           0 },
    { "base.math.fast_tier",                  test_math_fast_tier,                    0 },
    { "base.rng.normal",                      test_rng_normal,                        0 },

    { "base.string.str_ops",                  test_string_str_ops,                    0 },
    { "base.string.utf8",                     test_string_utf8,                       0 },
    { "base.string.utf16",                    test_string_utf16,                      0 },
    { "base.string.fmt_specifiers",           test_string_fmt_specifiers,             0 },
    { "base.string.fmt_fixed",                test_string_fmt_fixed,                  0 },
    { "base.string.f32_known",                test_string_f32_known,                  0 },
    { "base.string.f32_roundtrip_10m",        test_string_f32_roundtrip,              0 },
    { "base.string.f32_is_shortest",          test_string_f32_is_shortest,            0 },
    { "base.string.parse_f32_exact",          test_string_parse_f32_exact,            0 },
    { "base.string.parse_f32_cases",          test_string_parse_f32_cases,            0 },
    { "base.string.parse_int",                test_string_parse_int,                  0 },
    { "base.string.json_writer",              test_string_json_writer,                0 },

    { "platform.command_line_split",          test_platform_command_line_split,       0 },
    { "platform.file_roundtrip",              test_platform_file_roundtrip,           0 },
    { "platform.atomic_write",                test_platform_atomic_write,             0 },
    { "platform.file_watch",                  test_platform_file_watch,               0 },
    { "platform.threads_atomic_counter",      test_platform_threads_atomic_counter,   0 },
    { "platform.semaphore",                   test_platform_semaphore,                0 },
    { "platform.thread_fpu_state",            test_platform_thread_fpu_state,         0 },
    { "platform.time_and_sleep",              test_platform_time_and_sleep,           0 },
    { "platform.stdio_line_buffering",        test_platform_stdio_line_buffering,     0 },
    { "platform.library_load",                test_platform_library_load,             0 },
    { "platform.scancode_table",              test_platform_scancode_table,           0 },
    { "platform.utf16_text",                  test_platform_utf16_text,               0 },
    { "platform.gamepad_dead_zone",           test_platform_gamepad_dead_zone,        0 },
    { "platform.event_queue",                 test_platform_event_queue,              0 },
    { "platform.window_messages",             test_platform_window_messages,          0 },

    { "gpu.null_lifecycle",                   test_gpu_null_lifecycle,                0 },
    { "gpu.null_api_usage",                   test_gpu_null_api_usage,                0 },
    { "gpu.validation_draw_outside_pass",     test_gpu_validation_draw_outside_pass,  1 },
    { "gpu.validation_format_mismatch",       test_gpu_validation_format_mismatch,    1 },
    { "gpu.validation_write_overlap",         test_gpu_validation_write_overlap,      1 },
    { "gpu.validation_vertex_read_past_end",  test_gpu_validation_vertex_read_past_end, 1 },
    { "gpu.validation_static_uniform_write",  test_gpu_validation_static_uniform_write, 1 },

    { "gpu.d3d11_headless",                   test_gpu_d3d11_headless,                0 },
    { "gpu.d3d11_surface_present",            test_gpu_d3d11_surface_present,         0 },
    { "gpu.d3d11_resize_stress",              test_gpu_d3d11_resize_stress,           0 },
    { "gpu.d3d11_hresult_text",               test_gpu_d3d11_hresult_text,            0 },
    { "gpu.d3d11_device_lost_panics",         test_gpu_d3d11_device_lost_panics,      1 },
    { "gpu.d3d11_buffers",                    test_gpu_d3d11_buffers,                 0 },
    { "gpu.d3d11_textures_samplers",          test_gpu_d3d11_textures_samplers,       0 },
    { "gpu.d3d11_clear_pixels",               test_gpu_d3d11_clear_pixels,            0 },
    { "gpu.d3d11_triangle_pixels",            test_gpu_d3d11_triangle_pixels,         0 },
    { "gpu.d3d11_texture_sampling",           test_gpu_d3d11_texture_sampling,        0 },
    { "gpu.d3d11_blend_depth",                test_gpu_d3d11_blend_depth,             0 },
    { "gpu.d3d11_shader_tables",              test_gpu_d3d11_shader_tables,           0 },
    { "gpu.d3d11_validation_bindset_slots",   test_gpu_d3d11_validation_bindset_slots, 1 },

    { "draw2d.instance_layout",               test_draw2d_instance_layout,            0 },
    { "draw2d.f16",                           test_draw2d_f16,                        0 },
    { "draw2d.submit_fields",                 test_draw2d_submit_fields,              0 },
    { "draw2d.batches_null",                  test_draw2d_batches_null,               0 },
    { "draw2d.bindset_retire",                test_draw2d_bindset_retire,             0 },
    { "draw2d.budget",                        test_draw2d_budget,                     0 },
    { "draw2d.d3d11_pixels",                  test_draw2d_d3d11_pixels,               0 },
    { "draw2d.d3d11_sdf_sheet",               test_draw2d_d3d11_sdf_sheet,            0 },
    { "draw2d.camera_math",                   test_draw2d_camera_math,                0 },
    { "draw2d.camera_submit",                 test_draw2d_camera_submit,              0 },
    { "draw2d.camera_pop_empty",              test_draw2d_camera_pop_empty,           1 },
    { "draw2d.d3d11_camera_pixels",           test_draw2d_d3d11_camera_pixels,        0 },
    { "draw2d.bench_sprites_10k",             test_draw2d_bench_sprites_10k,          0 },
    { "draw2d.text_glyph_data",               test_draw2d_text_glyph_data,            0 },
    { "draw2d.text_measure",                  test_draw2d_text_measure,               0 },
    { "draw2d.text_submit",                   test_draw2d_text_submit,                0 },
    { "draw2d.d3d11_text_sheet",              test_draw2d_d3d11_text_sheet,           0 },
    { "draw2d.bench_text",                    test_draw2d_bench_text,                 0 },

    { "input.validation",                     test_input_validation,                  0 },
    { "input.edges_frames",                   test_input_edges_frames,                0 },
    { "input.short_presses",                  test_input_short_presses,               0 },
    { "input.axis_normalization",             test_input_axis_normalization,          0 },
    { "input.bindings",                       test_input_bindings,                    0 },
    { "input.pointers",                       test_input_pointers,                    0 },
    { "input.injection",                      test_input_injection,                   0 },
    { "input.text",                           test_input_text,                        0 },

    { "core.check_game",                      test_core_check_game,                   0 },
    { "core.parse_args",                      test_core_parse_args,                   0 },
    { "core.start_init",                      test_core_start_init,                   0 },
    { "core.fixed_step",                      test_core_fixed_step,                   0 },
    { "core.tick_input_events",               test_core_tick_input_events,            0 },
    { "core.swap",                            test_core_swap,                         0 },
    { "core.frame_null",                      test_core_frame_null,                   0 },
    { "core.frame_d3d11",                     test_core_frame_d3d11,                  0 },
    { "core.sfx_in_draw",                     test_core_sfx_in_draw,                  1 },

    { "reflect.fields_hash",                  test_reflect_fields_hash,               0 },
    { "reflect.get",                          test_reflect_get,                       0 },
    { "reflect.arrays",                       test_reflect_arrays,                    0 },
    { "reflect.paths",                        test_reflect_paths,                     0 },
    { "reflect.set",                          test_reflect_set,                       0 },
    { "reflect.dump",                         test_reflect_dump,                      0 },
    { "reflect.suggest",                      test_reflect_suggest,                   0 },
    { "reflect.nonfinite",                    test_reflect_nonfinite,                 0 },
    { "reflect.core",                         test_reflect_core,                      0 },

    { "protocol.commands",                    test_protocol_commands,                 0 },
    { "protocol.large_response",              test_protocol_large_response,           0 },
    { "protocol.process",                     test_protocol_process,                  0 },

    { "png.roundtrip",                        test_png_roundtrip,                     0 },
    { "png.reference",                        test_png_reference,                     0 },
    { "png.errors",                           test_png_errors,                        0 },

    { "shot.diff",                            test_shot_diff,                         0 },
    { "shot.size_flag",                       test_shot_size_flag,                    0 },
    { "shot.capture",                         test_shot_capture,                      0 },
    { "shot.no_gpu",                          test_shot_no_gpu,                       0 },

    { "snap.roundtrip",                       test_snap_roundtrip,                    0 },
    { "snap.slots",                           test_snap_slots,                        0 },
    { "snap.nonfinite",                       test_snap_nonfinite,                    0 },
    { "snap.recordings",                      test_snap_recordings,                   0 },
    { "replay.rle",                           test_replay_rle,                        0 },
    { "replay.roundtrip",                     test_replay_roundtrip,                  0 },
    { "replay.mutated",                       test_replay_mutated,                    0 },
    { "replay.session",                       test_replay_session,                    0 },
    { "replay.errors",                        test_replay_errors,                     0 },

    { "crash.null_write",                     test_crash_null_write,                  0 },
    { "crash.assert",                         test_crash_assert,                      0 },
    { "crash.divide_by_zero",                 test_crash_divide_by_zero,              0 },
    { "crash.panic",                          test_crash_panic,                       0 },
    { "crash.stack_overflow",                 test_crash_stack_overflow,              0 },
    { "crash.zone_order",                     test_crash_zone_order,                  0 },
    { "crash.zone_left_open",                 test_crash_zone_left_open,              0 },

    { "profile.busy_loop",                    test_profile_busy_loop,                 0 },
    { "profile.report",                       test_profile_report,                    0 },
    { "log.ring",                             test_log_ring,                          0 },
    { "log.echo",                             test_log_echo,                          0 },

    { "golden.lint",                          test_golden_lint,                       0 },
    { "golden.compare",                       test_golden_compare,                    0 },
    { "golden.update_names",                  test_golden_update_names,               0 },
    { "perf.budget_parse",                    test_perf_budget_parse,                 0 },
    { "perf.check",                           test_perf_check,                        0 },
};
