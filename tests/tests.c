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

    { "gpu.d3d11_headless",                   test_gpu_d3d11_headless,                0 },
    { "gpu.d3d11_surface_present",            test_gpu_d3d11_surface_present,         0 },
    { "gpu.d3d11_resize_stress",              test_gpu_d3d11_resize_stress,           0 },
    { "gpu.d3d11_hresult_text",               test_gpu_d3d11_hresult_text,            0 },
    { "gpu.d3d11_device_lost_panics",         test_gpu_d3d11_device_lost_panics,      1 },
};
