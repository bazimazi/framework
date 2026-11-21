# tools/test

The unit-test runner, built and run by `build test [filter]` (out/test/win64-debug/test.exe).

- Tests are plain functions `static void test_x(bmf_test* t)` in `tests/test_*.c`, listed by hand in the `TESTS[]` table in `tests/tests.c`. No auto-registration.
- Checks: `bmf_check`, `bmf_check_eq_i64`, `bmf_check_eq_u64` (hex), `bmf_check_eq_mem`. Each takes the call site explicitly: `..., __FILE__, __LINE__, "message")`. A failed check records the first failure and the test continues.
- `bmf_test_note(t, text)` attaches a measurement to the test's line as `"note"` (pass or fail), for example the math tests' max ULP error and output hash.
- Output: one JSON line per test (`{"test":..,"ok":1,"ms":..,"checks":..}` plus `note` when set, and `failures`, `file`, `line`, `msg` on failure), then `{"tests":N,"passed":P,"failed":F,"ms":T}`. Exit code 0 if all passed, else 1.
- Expected-crash tests (third column `1` in the table) run in a child process (`test.exe --run-one NAME`, Windows Error Reporting off) and pass when the child dies with an exception code (`>= 0xC0000000`), reported as `exit_code`.
- The runner starts with `bmf_platform_init` and uses the platform layer for arguments, time and stdout (`src/platform/platform.h`).
- Output lines are written with `bmf_json_writer` and messages with `bmf_fmt_buf` (`src/base/base_string.h`); use `bmf_fmt_buf` to build check messages and notes.
- `tests/gen/` holds test-only reference code (`math_ref.c`: big-integer and double-double reference for the math tests).
- Known: the test build is debug (-O0 + UBSan); the math ULP tests take about 4 s and the 10M float round trip about 5 s of the run.
