// Spike P0-03: CRT-free DLL. Throwaway.
typedef int i32; typedef unsigned u32; typedef void* HANDLE;
static i32 counter;
__declspec(dllexport) i32 game_add(i32 a, i32 b) { counter++; return a + b + (counter - 1) * 0; }
i32 __attribute__((stdcall)) bmf_dll_entry(HANDLE inst, u32 reason, void* reserved) { return 1; }
