typedef unsigned long long u64;
__attribute__((export_name("f128"))) double f128(double a) { long double x = a; return (double)(x * 3.0L); }
