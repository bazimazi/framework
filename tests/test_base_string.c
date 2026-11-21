// Tests for src/base/base_string.* (task 1A-06).

static bmf_str str_c(const char* s) { return bmf_str_from_cstr(s); }

// Checks that formatting produced `want` and returned its length.
static void string_expect(bmf_test* t, const char* got, i32 got_len, const char* want, i32 line) {
    b32 ok = bmf_cstr_eq(got, want) && got_len == bmf_cstr_len(want);
    char msg[512];
    bmf_fmt_buf(msg, sizeof msg, "expected \"%s\" (%i bytes), got \"%s\" (%i)", want, bmf_cstr_len(want), got, got_len);
    bmf_check(t, ok, __FILE__, line, msg);
}

static void string_expect_f32(bmf_test* t, f32 x, const char* want, i32 line) {
    char buf[32];
    i32 n = bmf_f32_to_text(x, buf, sizeof buf);
    string_expect(t, buf, n, want, line);
}

// ---------------------------------------------------------------- bmf_str
static void test_string_str_ops(bmf_test* t) {
    bmf_str s = str_c("hello, world");
    bmf_check(t, bmf_str_eq(s, str_c("hello, world")) && !bmf_str_eq(s, str_c("hello")), __FILE__, __LINE__, "eq");
    bmf_check(t, bmf_str_eq_cstr(bmf_str_slice(s, 7, 100), "world"), __FILE__, __LINE__, "slice clamps");
    bmf_check(t, bmf_str_slice(s, 5, 2).len == 0, __FILE__, __LINE__, "slice start > end is empty");
    bmf_check(t, bmf_str_starts_with(s, str_c("hell")) && !bmf_str_starts_with(s, str_c("world")), __FILE__, __LINE__, "starts_with");
    bmf_check(t, bmf_str_ends_with(s, str_c("world")) && !bmf_str_ends_with(str_c("ld"), str_c("world")), __FILE__, __LINE__, "ends_with");
    bmf_check_eq_i64(t, bmf_str_find(s, str_c("o"), 0), 4, __FILE__, __LINE__, "find");
    bmf_check_eq_i64(t, bmf_str_find(s, str_c("o"), 5), 8, __FILE__, __LINE__, "find from");
    bmf_check_eq_i64(t, bmf_str_find(s, str_c("xyz"), 0), -1, __FILE__, __LINE__, "find missing");
    bmf_check_eq_i64(t, bmf_str_find(s, str_c(""), 3), 3, __FILE__, __LINE__, "find empty needle");
    bmf_check_eq_i64(t, bmf_str_find_byte(s, ',', 0), 5, __FILE__, __LINE__, "find_byte");
    bmf_check(t, bmf_str_eq_cstr(bmf_str_trim(str_c(" \t\r\n x y \n")), "x y"), __FILE__, __LINE__, "trim");
    bmf_check(t, bmf_str_trim(str_c("   ")).len == 0, __FILE__, __LINE__, "trim all spaces");
    const char* want[4] = { "a", "", "b", "" };
    bmf_str_splitter it = bmf_str_split(str_c("a,,b,"), ',');
    bmf_str piece;
    i32 count = 0, bad = 0;
    while (bmf_str_split_next(&it, &piece)) { if (count >= 4 || !bmf_str_eq_cstr(piece, want[count])) bad++; count++; }
    bmf_check(t, count == 4 && bad == 0, __FILE__, __LINE__, "split a,,b, gives a | empty | b | empty");
    it = bmf_str_split(str_c(""), ',');
    count = 0;
    while (bmf_str_split_next(&it, &piece)) count++;
    bmf_check_eq_i64(t, count, 1, __FILE__, __LINE__, "split of empty gives one empty piece");
    bmf_check(t, bmf_cstr_eq(0, 0) && !bmf_cstr_eq("a", 0) && bmf_cstr_len("abc") == 3, __FILE__, __LINE__, "cstr helpers");
}

// ---------------------------------------------------------------- UTF-8 / UTF-16
static void test_string_utf8(bmf_test* t) {
    i32 bad = 0;
    for (u32 c = 0; c <= 0x10FFFF; c++) {
        if (c >= 0xD800 && c <= 0xDFFF) continue;
        u8 bytes[4];
        i32 n = bmf_utf8_encode(c, bytes);
        i32 want_n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        u32 back = 0;
        i32 used = bmf_utf8_decode(bmf_str_make(bytes, n), 0, &back);
        if (n != want_n || used != n || back != c) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "every code point round-trips");
    static const struct { const char* bytes; i32 len; } invalid[] = {
        { "\x80", 1 }, { "\xBF", 1 }, { "\xC0\xAF", 2 }, { "\xE0\x80\xAF", 3 }, { "\xF0\x80\x80\xAF", 4 },   // stray, overlong
        { "\xED\xA0\x80", 3 }, { "\xED\xBF\xBF", 3 },                                                      // surrogates
        { "\xF4\x90\x80\x80", 4 }, { "\xF8\x88\x80\x80\x80", 5 }, { "\xFF", 1 },                           // > U+10FFFF, 5-byte, FF
        { "\xE2\x82", 2 }, { "\xE2\x28\xA1", 3 },                                                          // truncated, bad continuation
    };
    i32 wrong = 0;
    for (i32 i = 0; i < (i32)(sizeof invalid / sizeof invalid[0]); i++) {
        u32 c = 0;
        i32 used = bmf_utf8_decode(bmf_str_make(invalid[i].bytes, invalid[i].len), 0, &c);
        if (c != 0xFFFD || used != 1) wrong++;
    }
    bmf_check_eq_i64(t, wrong, 0, __FILE__, __LINE__, "invalid sequences decode as U+FFFD, 1 byte");
    u8 out[4];
    bmf_check(t, bmf_utf8_encode(0xD800, out) == 3 && out[0] == 0xEF && out[1] == 0xBF && out[2] == 0xBD, __FILE__, __LINE__, "encode surrogate -> U+FFFD");
    bmf_check(t, bmf_utf8_encode(0x110000, out) == 3, __FILE__, __LINE__, "encode > U+10FFFF -> U+FFFD");
}

static void test_string_utf16(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 16, 16);
    static u16 wide[512], wide_back[512];
    static u8 narrow[2048];
    i32 bad = 0;
    for (i32 round = 0; round < 500; round++) {
        i32 n = 0;
        while (n < 500) {
            u32 kind = bmf_rng_next_u32(&r) % 4;
            u32 c = kind == 0 ? bmf_rng_next_u32(&r) % 0x80 : kind == 1 ? 0x80 + bmf_rng_next_u32(&r) % 0x780
                  : kind == 2 ? 0xE000 + bmf_rng_next_u32(&r) % 0x2000 : 0x10000 + bmf_rng_next_u32(&r) % 0x100000;
            if (c >= 0x10000) { c -= 0x10000; wide[n++] = (u16)(0xD800 + (c >> 10)); wide[n++] = (u16)(0xDC00 + (c & 0x3FF)); }
            else wide[n++] = (u16)c;
        }
        i32 len = bmf_utf16_to_utf8(wide, n, narrow, sizeof narrow);
        i32 back = bmf_utf8_to_utf16(bmf_str_make(narrow, len), wide_back, 512);
        if (len > (i32)sizeof narrow || back != n || bmf_memcmp(wide, wide_back, (i64)n * 2) != 0) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "UTF-16 -> UTF-8 -> UTF-16 round trip");
    u16 lone[3] = { 0x41, 0xD800, 0x42 };
    u8 buf[16];
    i32 len = bmf_utf16_to_utf8(lone, 3, buf, sizeof buf);
    bmf_check(t, len == 5 && buf[0] == 'A' && buf[1] == 0xEF && buf[4] == 'B', __FILE__, __LINE__, "unpaired surrogate -> U+FFFD");
    u16 euro[1] = { 0x20AC };
    len = bmf_utf16_to_utf8(euro, 1, buf, 2);
    bmf_check(t, len == 3, __FILE__, __LINE__, "too small: returns the needed size");
}

// ---------------------------------------------------------------- formatting
static void test_string_fmt_specifiers(bmf_test* t) {
    char buf[256];
    i32 n;
    n = bmf_fmt_buf(buf, sizeof buf, "%i|%i|%i", 0, -2147483647 - 1, 2147483647);
    string_expect(t, buf, n, "0|-2147483648|2147483647", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%u|%x|%x", 4294967295u, 0xDEADBEEFu, 0u);
    string_expect(t, buf, n, "4294967295|deadbeef|0", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%li|%lu|%lx", -9223372036854775807ll - 1, 18446744073709551615ull, 0x0123456789ABCDEFull);
    string_expect(t, buf, n, "-9223372036854775808|18446744073709551615|123456789abcdef", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "[%5i][%-5i][%05i][%016lx][%3s][%-3s|]", -42, 42, -42, 0xABCull, "a", "b");
    string_expect(t, buf, n, "[  -42][42   ][-0042][0000000000000abc][  a][b  |]", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%s %s %c%c %%", "text", (const char*)0, 'o', 'k');
    string_expect(t, buf, n, "text (null) ok %", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "<%S> <%S>", bmf_str_make("abcdef", 3), bmf_str_make(0, 0));
    string_expect(t, buf, n, "<abc> <>", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%v2 %v2", bmf_v2_make(1.5f, -0.1f), bmf_v2_make(0.0f, 1e30f));
    string_expect(t, buf, n, "[1.5,-0.1] [0,1e+30]", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%f %f %f %f", 0.1f, -2.0f, 1e-7f, (f64)0.3);
    string_expect(t, buf, n, "0.1 -2 1e-7 0.3", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "[%8f][%-8f][%08f]", -1.5f, 1.5f, -1.5f);
    string_expect(t, buf, n, "[    -1.5][1.5     ][-00001.5]", __LINE__);
    // Truncation: NUL-terminated prefix, full length returned.
    char small[6];
    n = bmf_fmt_buf(small, sizeof small, "%s", "abcdefghij");
    string_expect(t, small, 5, "abcde", __LINE__);
    bmf_check_eq_i64(t, n, 10, __FILE__, __LINE__, "truncated: returns the full length");
    bmf_check_eq_i64(t, bmf_fmt_buf(0, 0, "%i", 12345), 5, __FILE__, __LINE__, "capacity 0 only counts");
    // Arena.
    u8 storage[256];
    bmf_arena arena = { storage, sizeof storage, sizeof storage, 0, 0, "test" };
    bmf_str s = bmf_fmt_arena(&arena, "tick=%li hash=%016lx", (i64)12, 0xFFull);
    bmf_check(t, bmf_str_eq_cstr(s, "tick=12 hash=00000000000000ff") && s.ptr[s.len] == 0, __FILE__, __LINE__, "fmt_arena");
}

static void test_string_fmt_fixed(bmf_test* t) {
    char buf[128];
    i32 n;
    n = bmf_fmt_buf(buf, sizeof buf, "%.2f %.2f %.2f %.0f %.0f %.0f", 0.125f, 0.375f, 1.005f, 2.5f, 3.5f, 0.5f);
    string_expect(t, buf, n, "0.12 0.38 1.00 2 4 0", __LINE__);    // exact binary values, ties to even
    n = bmf_fmt_buf(buf, sizeof buf, "%.3f %.9f %.1f %.1f %.4f", 123.456f, 0.1f, -0.0f, 1e10f, -0.00004f);
    string_expect(t, buf, n, "123.456 0.100000001 -0.0 10000000000.0 -0.0000", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%.0f", 3.40282347e38f);
    string_expect(t, buf, n, "340282346638528859811704183484516925440", __LINE__);
    n = bmf_fmt_buf(buf, sizeof buf, "%.9f %.2f %.1f", 1.4e-45f, 99.995f, 0.05f);
    string_expect(t, buf, n, "0.000000000 100.00 0.1", __LINE__);   // 99.995f = 99.99500274..., 0.05f = 0.0500000007...
    n = bmf_fmt_buf(buf, sizeof buf, "[%8.2f] %.1f", -3.14159f, bmf_f32_from_bits(0x7F800000u));
    string_expect(t, buf, n, "[   -3.14] inf", __LINE__);
}

// ---------------------------------------------------------------- shortest floats
static void test_string_f32_known(bmf_test* t) {
    string_expect_f32(t, 0.0f, "0", __LINE__);
    string_expect_f32(t, -0.0f, "-0", __LINE__);
    string_expect_f32(t, 1.0f, "1", __LINE__);
    string_expect_f32(t, -1.5f, "-1.5", __LINE__);
    string_expect_f32(t, 0.1f, "0.1", __LINE__);
    string_expect_f32(t, 0.3f, "0.3", __LINE__);
    string_expect_f32(t, 2.0f / 3.0f, "0.6666667", __LINE__);
    string_expect_f32(t, 100.0f, "100", __LINE__);
    string_expect_f32(t, 123456.7f, "123456.7", __LINE__);
    string_expect_f32(t, 16777216.0f, "16777216", __LINE__);
    string_expect_f32(t, 1e20f, "100000000000000000000", __LINE__);
    string_expect_f32(t, 1e21f, "1e+21", __LINE__);
    string_expect_f32(t, 0.000001f, "0.000001", __LINE__);
    string_expect_f32(t, 1e-7f, "1e-7", __LINE__);
    string_expect_f32(t, 1.5e-7f, "1.5e-7", __LINE__);
    string_expect_f32(t, 3.40282347e38f, "3.4028235e+38", __LINE__);
    string_expect_f32(t, 1.17549435e-38f, "1.1754944e-38", __LINE__);
    string_expect_f32(t, bmf_f32_from_bits(1), "1e-45", __LINE__);
    string_expect_f32(t, bmf_f32_from_bits(2), "3e-45", __LINE__);
    string_expect_f32(t, bmf_f32_from_bits(0x007FFFFFu), "1.1754942e-38", __LINE__);
    string_expect_f32(t, 33554432.0f, "33554432", __LINE__);
    string_expect_f32(t, 9.999999e9f, "9999999000", __LINE__);
    string_expect_f32(t, bmf_f32_from_bits(0x7F800000u), "inf", __LINE__);
    string_expect_f32(t, bmf_f32_from_bits(0xFF800000u), "-inf", __LINE__);
    string_expect_f32(t, bmf_f32_from_bits(0x7FC00000u), "nan", __LINE__);
}

// Format then parse every one of 10M random bit patterns (plus all exponents' edges): exact.
static void test_string_f32_roundtrip(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 10, 10);
    i32 bad = 0, too_long = 0;
    u32 first_bad = 0;
    for (i32 i = 0; i < 10000000; i++) {
        u32 bits;
        if (i < 512) bits = ((u32)(i >> 1) << 23) | ((i & 1) ? 0x7FFFFFu : 1u);   // each exponent, both mantissa ends
        else bits = bmf_rng_next_u32(&r);
        if ((bits & 0x7F800000u) == 0x7F800000u) continue;
        f32 x = bmf_f32_from_bits(bits);
        char buf[32];
        i32 n = bmf_f32_to_text(x, buf, sizeof buf);
        f32 back = 0.0f;
        bmf_err e = bmf_parse_f32(bmf_str_make(buf, n), &back);
        if (e != BMF_OK || bmf_f32_bits(back) != bits) { if (!bad) first_bad = bits; bad++; }
        if (n > 22) too_long++;
    }
    char note[128];
    bmf_fmt_buf(note, sizeof note, "first_bad_bits=%08x", first_bad);
    if (bad) bmf_test_note(t, note);
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "format -> parse is not exact");
    bmf_check_eq_i64(t, too_long, 0, __FILE__, __LINE__, "text longer than 22 bytes");
}

// No representation with fewer significant digits round-trips (checked by trying the decimals
// nearest to x on every coarser grid).
static void test_string_f32_is_shortest(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 12, 12);
    i32 not_shortest = 0;
    u32 example = 0;
    for (i32 i = 0; i < 100000; i++) {
        u32 bits = bmf_rng_next_u32(&r) & 0x7FFFFFFFu;
        if (bits >= 0x7F800000u || bits == 0) continue;
        f32 x = bmf_f32_from_bits(bits);
        u64 digits;
        i32 k;
        str_f32_shortest(x, &digits, &k);
        i32 len = 0;
        for (u64 d = digits; d; d /= 10) len++;
        for (i32 coarser = 1; coarser < len; coarser++) {
            i32 kk = k + coarser;                         // grid with `coarser` fewer digits
            f64 v = kk >= 0 ? (f64)x / STR_POW10[kk] : (f64)x * STR_POW10[-kk];
            f64 base = __builtin_floor(v);
            for (i32 d = -1; d <= 2; d++) {
                f64 cand = base + (f64)d;
                if (cand < 1.0) continue;
                str_decimal dec;
                dec.count = 0;
                char tmp[24];
                i32 n = 0;
                for (u64 c = (u64)cand; c; c /= 10) tmp[n++] = (char)(c % 10);
                for (i32 j = 0; j < n; j++) dec.digit[dec.count++] = (u8)tmp[n - 1 - j];
                dec.dec_exp = kk + n;
                dec.sticky = 0;
                if (bmf_f32_bits(str_decimal_to_f32(&dec)) == bits) { if (!not_shortest) example = bits; not_shortest++; }
            }
        }
    }
    char note[64];
    bmf_fmt_buf(note, sizeof note, "example_bits=%08x", example);
    if (not_shortest) bmf_test_note(t, note);
    bmf_check_eq_i64(t, not_shortest, 0, __FILE__, __LINE__, "a shorter decimal also round-trips");
}

// ---------------------------------------------------------------- parsing
// Exact check of a parse result: the decimal digits * 10^q must lie between the midpoints to
// f's neighbours (ties go to the even mantissa). Compares big integers, no division.
static i32 string_cmp_decimal_binary(const str_decimal* d, u64 m, i32 e2) {   // sign of D*10^q - m*2^e2
    str_big left, right;
    str_big_set_u64(&left, 0);
    for (i32 i = 0; i < d->count; i++) str_big_mul_add_small(&left, 10, d->digit[i]);
    i32 q = d->dec_exp - d->count;
    str_big_set_u64(&right, m);
    if (q >= 0) str_big_mul_pow10(&left, q); else str_big_mul_pow10(&right, -q);
    if (e2 >= 0) str_big_shl(&right, e2); else str_big_shl(&left, -e2);
    return str_big_cmp(&left, &right);
}

static b32 string_parse_is_correct(const str_decimal* d, f32 f) {
    u32 b = bmf_f32_bits(f);
    if (b == 0x7F800000u) return string_cmp_decimal_binary(d, 0x1FFFFFFull, 103) >= 0;   // >= FLT_MAX + ulp/2
    // f = m * 2^e with the midpoints (2m - 1) * 2^(e-1) and (2m + 1) * 2^(e-1); at a power of two
    // the lower gap is half as wide.
    u32 field = b >> 23;
    u64 m = field ? (b & 0x7FFFFFu) | 0x800000u : b;
    i32 e = field ? (i32)field - 150 : -149;
    b32 even = (m & 1) == 0;
    i32 up = string_cmp_decimal_binary(d, 2 * m + 1, e - 1);
    if (up > 0 || (up == 0 && !even)) return 0;
    if (b == 0) return 1;
    b32 power_of_two = field > 1 && (b & 0x7FFFFFu) == 0;
    i32 down = power_of_two ? string_cmp_decimal_binary(d, 4 * m - 1, e - 2) : string_cmp_decimal_binary(d, 2 * m - 1, e - 1);
    return down > 0 || (down == 0 && even);
}

static void test_string_parse_f32_exact(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 13, 13);
    i32 wrong = 0, fast_differs = 0;
    for (i32 i = 0; i < 30000; i++) {
        str_decimal d;
        i32 kind = i % 3;
        d.count = kind == 0 ? 1 + (i32)(bmf_rng_next_u32(&r) % 9) : kind == 1 ? 10 + (i32)(bmf_rng_next_u32(&r) % 20) : 100 + (i32)(bmf_rng_next_u32(&r) % 20);
        for (i32 k = 0; k < d.count; k++) d.digit[k] = (u8)(bmf_rng_next_u32(&r) % 10);
        d.digit[0] = (u8)(1 + bmf_rng_next_u32(&r) % 9);
        d.dec_exp = bmf_rng_range_i32(&r, -46, 39);
        d.sticky = 0;
        if (i % 7 == 0) {   // exactly at, just above or just below the midpoint above a random f32
            u32 b = bmf_rng_next_u32(&r) % 0x7F7FFFFFu;
            u64 mid = 2 * (u64)((b & 0x7FFFFFu) | ((b >> 23) ? 0x800000u : 0u)) + 1;
            i32 e = (b >> 23) ? (i32)(b >> 23) - 151 : -150;   // midpoint = mid * 2^e exactly
            str_big v;
            str_big_set_u64(&v, mid);
            if (e >= 0) str_big_shl(&v, e);
            else for (i32 k = 0; k < -e; k++) str_big_mul_add_small(&v, 5, 0);   // mid 2^e = mid 5^-e 10^e
            char rev[400];
            i32 len = 0;
            while (v.used > 0) rev[len++] = (char)str_big_div_small(&v, 10);
            if (len > STR_MAX_DIGITS) continue;
            d.count = 0;
            for (i32 k = 0; k < len; k++) d.digit[d.count++] = (u8)rev[len - 1 - k];
            d.dec_exp = len + (e < 0 ? e : 0);
            d.sticky = 0;
            u32 nudge = bmf_rng_next_u32(&r) % 3;
            if (nudge == 1) d.sticky = 1;                 // just above
            if (nudge == 2) {                             // just below: subtract one unit in the last place
                i32 k = d.count - 1;
                while (k >= 0 && d.digit[k] == 0) d.digit[k--] = 9;
                if (k >= 0) d.digit[k]--;
            }
            while (d.count > 0 && d.digit[d.count - 1] == 0 && !d.sticky) d.count--;
            if (d.count == 0 || d.digit[0] == 0) continue;
        }
        f32 fast = str_decimal_to_f32(&d);
        f32 exact = (d.dec_exp >= 40) ? bmf_f32_from_bits(0x7F800000u) : (d.dec_exp <= -46 ? 0.0f : str_decimal_to_f32_exact(&d));
        if (bmf_f32_bits(fast) != bmf_f32_bits(exact)) fast_differs++;
        if (!d.sticky && !string_parse_is_correct(&d, exact)) wrong++;
    }
    bmf_check_eq_i64(t, wrong, 0, __FILE__, __LINE__, "parse result outside the exact rounding interval");
    bmf_check_eq_i64(t, fast_differs, 0, __FILE__, __LINE__, "fast path disagrees with the exact path");
}

static void string_expect_parse(bmf_test* t, const char* text, bmf_err want_err, u32 want_bits, i32 line) {
    f32 v = bmf_f32_from_bits(0x12345678u);
    bmf_err e = bmf_parse_f32(str_c(text), &v);
    char msg[256];
    bmf_fmt_buf(msg, sizeof msg, "parse \"%s\": expected err %i bits %08x, got err %i bits %08x", text, (i32)want_err, want_bits, (i32)e, bmf_f32_bits(v));
    b32 ok = e == want_err && (want_err == BMF_ERR_INVALID ? bmf_f32_bits(v) == 0x12345678u
                              : want_bits == 0x7FC00000u ? v != v : bmf_f32_bits(v) == want_bits);
    bmf_check(t, ok, __FILE__, line, msg);
}

static void test_string_parse_f32_cases(bmf_test* t) {
    string_expect_parse(t, "0", BMF_OK, 0x00000000u, __LINE__);
    string_expect_parse(t, "-0", BMF_OK, 0x80000000u, __LINE__);
    string_expect_parse(t, "-0.000e5", BMF_OK, 0x80000000u, __LINE__);
    string_expect_parse(t, "1", BMF_OK, 0x3F800000u, __LINE__);
    string_expect_parse(t, "+1.5", BMF_OK, 0x3FC00000u, __LINE__);
    string_expect_parse(t, ".5", BMF_OK, 0x3F000000u, __LINE__);
    string_expect_parse(t, "5.", BMF_OK, 0x40A00000u, __LINE__);
    string_expect_parse(t, "0.1", BMF_OK, 0x3DCCCCCDu, __LINE__);
    string_expect_parse(t, "000123.4500", BMF_OK, bmf_f32_bits(123.45f), __LINE__);
    string_expect_parse(t, "1E3", BMF_OK, 0x447A0000u, __LINE__);
    string_expect_parse(t, "16777217", BMF_OK, 0x4B800000u, __LINE__);            // tie -> even (16777216)
    string_expect_parse(t, "16777219", BMF_OK, 0x4B800002u, __LINE__);            // tie -> even (16777220)
    string_expect_parse(t, "16777217.000000000000000000000000000000001", BMF_OK, 0x4B800001u, __LINE__);   // just above the tie
    string_expect_parse(t, "3.4028235e38", BMF_OK, 0x7F7FFFFFu, __LINE__);
    string_expect_parse(t, "3.4028235677973365e38", BMF_OK, 0x7F7FFFFFu, __LINE__);   // just below the overflow cut
    string_expect_parse(t, "340282356779733661637539395458142568448", BMF_ERR_RANGE, 0x7F800000u, __LINE__);   // exactly the cut: ties to even = Inf
    string_expect_parse(t, "1e39", BMF_ERR_RANGE, 0x7F800000u, __LINE__);
    string_expect_parse(t, "-1e400", BMF_ERR_RANGE, 0xFF800000u, __LINE__);
    string_expect_parse(t, "1.4e-45", BMF_OK, 0x00000001u, __LINE__);
    string_expect_parse(t, "7.006492321624085e-46", BMF_OK, 0x00000000u, __LINE__);   // below half the smallest denormal
    string_expect_parse(t, "7.0064923216240862e-46", BMF_OK, 0x00000001u, __LINE__);  // above it
    string_expect_parse(t, "1.17549435e-38", BMF_OK, 0x00800000u, __LINE__);
    string_expect_parse(t, "1e-50", BMF_OK, 0x00000000u, __LINE__);
    string_expect_parse(t, "0.10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001", BMF_OK, 0x3DCCCCCDu, __LINE__);
    string_expect_parse(t, "inf", BMF_OK, 0x7F800000u, __LINE__);
    string_expect_parse(t, "-Infinity", BMF_OK, 0xFF800000u, __LINE__);
    string_expect_parse(t, "NaN", BMF_OK, 0x7FC00000u, __LINE__);
    string_expect_parse(t, "", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "-", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, ".", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "1e", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "e5", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "1.2.3", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "--1", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, " 1", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "1x", BMF_ERR_INVALID, 0, __LINE__);
    string_expect_parse(t, "infinit", BMF_ERR_INVALID, 0, __LINE__);
}

static void test_string_parse_int(bmf_test* t) {
    i32 v = 7;
    bmf_check(t, bmf_parse_i32(str_c("2147483647"), &v) == BMF_OK && v == 2147483647, __FILE__, __LINE__, "i32 max");
    bmf_check(t, bmf_parse_i32(str_c("-2147483648"), &v) == BMF_OK && v == -2147483647 - 1, __FILE__, __LINE__, "i32 min");
    bmf_check(t, bmf_parse_i32(str_c("+0042"), &v) == BMF_OK && v == 42, __FILE__, __LINE__, "plus and leading zeros");
    v = 7;
    bmf_check(t, bmf_parse_i32(str_c("2147483648"), &v) == BMF_ERR_RANGE && v == 7, __FILE__, __LINE__, "i32 overflow");
    bmf_check(t, bmf_parse_i32(str_c("-2147483649"), &v) == BMF_ERR_RANGE, __FILE__, __LINE__, "i32 underflow");
    bmf_check(t, bmf_parse_i32(str_c("99999999999999999999999"), &v) == BMF_ERR_RANGE, __FILE__, __LINE__, "long overflow");
    bmf_check(t, bmf_parse_i32(str_c(""), &v) == BMF_ERR_INVALID && bmf_parse_i32(str_c("-"), &v) == BMF_ERR_INVALID, __FILE__, __LINE__, "empty");
    bmf_check(t, bmf_parse_i32(str_c("12a"), &v) == BMF_ERR_INVALID && bmf_parse_i32(str_c(" 1"), &v) == BMF_ERR_INVALID && v == 7, __FILE__, __LINE__, "garbage");
    i64 w = 0;
    bmf_check(t, bmf_parse_i64(str_c("-9223372036854775808"), &w) == BMF_OK && w == -9223372036854775807ll - 1, __FILE__, __LINE__, "i64 min");
    bmf_check(t, bmf_parse_i64(str_c("9223372036854775807"), &w) == BMF_OK && w == 9223372036854775807ll, __FILE__, __LINE__, "i64 max");
    bmf_check(t, bmf_parse_i64(str_c("9223372036854775808"), &w) == BMF_ERR_RANGE, __FILE__, __LINE__, "i64 overflow");
}

// ---------------------------------------------------------------- JSON writer
static void test_string_json_writer(bmf_test* t) {
    char buf[512];
    bmf_json_writer w;
    bmf_json_init(&w, buf, sizeof buf);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "ok"); bmf_json_i64(&w, 1);
    bmf_json_key(&w, "neg"); bmf_json_i64(&w, -9223372036854775807ll - 1);
    bmf_json_key(&w, "big"); bmf_json_u64(&w, 18446744073709551615ull);
    bmf_json_key(&w, "v"); bmf_json_array_begin(&w); bmf_json_f32(&w, 1.5f); bmf_json_f32(&w, 0.1f); bmf_json_array_end(&w);
    bmf_json_key(&w, "empty"); bmf_json_array_begin(&w); bmf_json_array_end(&w);
    bmf_json_key(&w, "o"); bmf_json_object_begin(&w); bmf_json_object_end(&w);
    bmf_json_key(&w, "s"); bmf_json_string(&w, "a\"b\\c\n\t\x01\xC3\xA9\xFF");
    bmf_json_key(&w, "flags"); bmf_json_array_begin(&w); bmf_json_bool(&w, 1); bmf_json_bool(&w, 0); bmf_json_null(&w); bmf_json_string(&w, 0); bmf_json_array_end(&w);
    bmf_json_key(&w, "special"); bmf_json_array_begin(&w);
    bmf_json_f32(&w, bmf_f32_from_bits(0x7FC00000u)); bmf_json_f32(&w, bmf_f32_from_bits(0xFF800000u)); bmf_json_f32(&w, -0.0f);
    bmf_json_array_end(&w);
    bmf_json_key(&w, "raw"); bmf_json_raw(&w, "{\"x\":1}");
    bmf_json_key(&w, "str"); bmf_json_str(&w, bmf_str_make("abcdef", 2));
    bmf_json_object_end(&w);
    const char* want = "{\"ok\":1,\"neg\":-9223372036854775808,\"big\":18446744073709551615,\"v\":[1.5,0.1],\"empty\":[],\"o\":{},"
                       "\"s\":\"a\\\"b\\\\c\\n\\t\\u0001\xC3\xA9\xEF\xBF\xBD\",\"flags\":[true,false,null,null],"
                       "\"special\":[\"NaN\",\"-Infinity\",-0],\"raw\":{\"x\":1},\"str\":\"ab\"}";
    string_expect(t, buf, w.len, want, __LINE__);
    bmf_check(t, !w.overflow && w.depth == 0, __FILE__, __LINE__, "no overflow, depth back to 0");
    // Overflow: never writes past capacity, stays NUL-terminated, sets the flag.
    char small[10];
    small[9] = 'Z';
    bmf_json_init(&w, small, 9);
    bmf_json_array_begin(&w);
    for (i32 i = 0; i < 10; i++) bmf_json_i64(&w, 12345);
    bmf_json_array_end(&w);
    bmf_check(t, w.overflow && w.len == 8 && small[8] == 0 && small[9] == 'Z', __FILE__, __LINE__, "overflow is bounded and flagged");
    // Top-level values (JSON lines) get no commas.
    bmf_json_init(&w, buf, sizeof buf);
    bmf_json_i64(&w, 1);
    bmf_json_i64(&w, 2);
    string_expect(t, buf, w.len, "12", __LINE__);
}
