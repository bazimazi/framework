typedef unsigned long long u64;
__attribute__((export_name("mul128hi"))) u64 mul128hi(u64 a, u64 b) { return (u64)(((unsigned __int128)a * b) >> 64); }
__attribute__((export_name("div128"))) u64 div128(u64 a, u64 b) { return (u64)(((unsigned __int128)a << 64) / ((unsigned __int128)b | 1)); }
__attribute__((export_name("f128"))) double f128(double a) { long double x = a; return (double)(x * 3.0L); }
