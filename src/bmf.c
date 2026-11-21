// bmf.c: the unity root of every bmf binary (D-08). Each binary is one translation unit.
// build.c writes a generated root that defines exactly one build mode and then includes this
// file (and, depending on the mode, the game's game.c or the test runner after it):
//   BMF_BUILD_RUNTIME_EXE   the runtime exe; plus BMF_GAME_LINK_DLL (debug, D-13) or
//                           BMF_GAME_LINK_STATIC (release: game.c is included after this file)
//   BMF_BUILD_GAME_DLL      a game DLL for hot reload (game.c is included after this file)
//   BMF_BUILD_TESTS         the unit-test runner (tools/test/test_main.c provides the entry)

#if defined(BMF_BUILD_RUNTIME_EXE) + defined(BMF_BUILD_GAME_DLL) + defined(BMF_BUILD_TESTS) != 1
#error "bmf.c: define exactly one of BMF_BUILD_RUNTIME_EXE, BMF_BUILD_GAME_DLL, BMF_BUILD_TESTS (build.c does this)"
#endif
#if defined(BMF_BUILD_RUNTIME_EXE) && (defined(BMF_GAME_LINK_DLL) + defined(BMF_GAME_LINK_STATIC) != 1)
#error "bmf.c: the runtime exe needs exactly one of BMF_GAME_LINK_DLL, BMF_GAME_LINK_STATIC"
#endif

#include "base/base_types.h"
#include "base/base_builtins.c"
#include "base/base_memory.h"
#include "base/base_memory.c"
#include "base/base_simd.h"
#include "base/base_math.h"
#include "base/base_math.c"
#include "base/base_hash.h"
#include "base/base_hash.c"
#include "base/base_rng.h"
#include "base/base_sort.h"
#include "base/base_sort.c"
#include "base/base_string.h"
#include "base/base_string.c"

#include "platform/platform.h"
#include "platform/platform_common.c"
#if BMF_OS_WINDOWS
#include "platform/win32/win32_api.h"
#include "platform/win32/win32_core.c"
#include "platform/win32/win32_window.c"
#endif

#include "gpu/gpu.h"
#if BMF_OS_WINDOWS
#include "gpu/d3d11/gpu_d3d11.c"
#endif
#include "gpu/gpu.c"

#if defined(BMF_BUILD_RUNTIME_EXE) && BMF_OS_WINDOWS
#include "platform/win32/win32_main.c"
#endif

#if defined(BMF_BUILD_GAME_DLL) && BMF_OS_WINDOWS
// DLL entry (DllMain signature). The game DLL does no work at load or unload: the runtime calls
// bmf_game() after LoadLibrary (D-13, SPEC §8.5).
i32 bmf_dll_entry(void* instance, u32 reason, void* reserved);
i32 bmf_dll_entry(void* instance, u32 reason, void* reserved) { return 1; }
#endif
