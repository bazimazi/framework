// Tests for the test runner itself (task 1A-08): a failed check must record file, line and
// message, and later failures must not overwrite the first one.

static void test_runner_check_records_location(bmf_test* t) {
    bmf_test fake;
    bmf_memset(&fake, 0, sizeof fake);
    bmf_check(&fake, 1, "a.c", 10, "passes");
    i32 fail_line = __LINE__ + 1;
    bmf_check(&fake, 0, __FILE__, fail_line, "first failure");
    bmf_check_eq_i64(&fake, 3, 4, "b.c", 99, "second failure");
    bmf_check_eq_i64(t, fake.check_count, 3, __FILE__, __LINE__, "check count");
    bmf_check_eq_i64(t, fake.failure_count, 2, __FILE__, __LINE__, "failure count");
    bmf_check(t, bmf_cstr_eq(fake.first_file, __FILE__), __FILE__, __LINE__, "first failure file");
    bmf_check_eq_i64(t, fake.first_line, fail_line, __FILE__, __LINE__, "first failure line");
    bmf_check(t, bmf_cstr_eq(fake.first_message, "first failure"), __FILE__, __LINE__, "first failure message");
}

static void test_runner_eq_messages(bmf_test* t) {
    bmf_test fake;
    bmf_memset(&fake, 0, sizeof fake);
    bmf_check_eq_i64(&fake, -5, 7, "x.c", 1, "value");
    bmf_check(t, bmf_cstr_eq(fake.first_message, "value: expected 7, got -5"), __FILE__, __LINE__, fake.first_message);
    bmf_memset(&fake, 0, sizeof fake);
    u8 a[4] = { 1, 2, 3, 4 }, b[4] = { 1, 2, 9, 4 };
    bmf_check_eq_mem(&fake, a, b, 4, "x.c", 2, "bytes");
    bmf_check(t, bmf_cstr_eq(fake.first_message, "bytes: first difference at byte 2 (expected 9, got 3)"), __FILE__, __LINE__, fake.first_message);
}
