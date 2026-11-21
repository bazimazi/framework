typedef float f4 __attribute__((ext_vector_type(4)));
__attribute__((export_name("m"))) f4 m(f4 a, f4 b, f4 c){ return a*b+c; }
