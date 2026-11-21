// win32_api.h: hand-written declarations of the Win32 functions, structs, constants and COM
// interfaces bmf uses (task 1A-10, SPEC §9.1). No windows.h anywhere. x64 only: one calling
// convention, so no WINAPI annotations.
//
// Rules for this file:
// - Add only what is used (or what the next platform/GPU/audio task needs). Each declaration names
//   its MSDN entry in a trailing comment so it can be checked.
// - Types and struct fields keep their SDK names, so a field can be looked up on MSDN directly.
//   Constants get a WIN32_ prefix (the runtime is one unity build that the game is compiled into;
//   bare names like INFINITE or WM_SIZE would leak into game code).
// - Struct sizes are pinned with BMF_STATIC_ASSERT. COM vtables list every slot up to the last one
//   used, in SDK order: unused slots are `void* _<slot>_<Name>` placeholders, used slots are typed
//   function pointers with a `// <slot>` comment. Interfaces whose only calls are QueryInterface and
//   Release use the shared IUnknownVtbl.
// - Layouts, slots, constants and GUIDs were compared with the Windows SDK 10.0.26100 headers when
//   added (see docs/progress.md, 1A-10); tests/test_vtable_slots.c (1A-14) keeps checking them.
// - DLLs: kernel32 (linked), user32/ole32/avrt/synchronization (linked by the tasks that first call
//   them), d3d11/dxgi/xinput1_4/d3dcompiler_47 (loaded at runtime with LoadLibraryW: a --no-gpu run
//   must start where they are missing, and d3dcompiler_47 is build-time only).
#ifndef BMF_WIN32_API_H
#define BMF_WIN32_API_H

#include "../../base/base_types.h"

// ================================================================ basic types
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* HWND;
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HBRUSH;
typedef void* HMENU;
typedef void* HMONITOR;
typedef void* HRAWINPUT;
typedef u16   WCHAR;
typedef i32   HRESULT;                             // HRESULT: < 0 is failure
typedef i64   LRESULT;                             // LRESULT (LONG_PTR)
typedef u64   WPARAM;                              // WPARAM (UINT_PTR)
typedef i64   LPARAM;                              // LPARAM (LONG_PTR)

typedef struct GUID { u32 Data1; u16 Data2; u16 Data3; u8 Data4[8]; } GUID;   // GUID
typedef struct POINT { i32 x, y; } POINT;                                     // POINT
typedef struct RECT { i32 left, top, right, bottom; } RECT;                   // RECT (also D3D11_RECT)
typedef struct LUID { u32 LowPart; i32 HighPart; } LUID;                      // LUID
typedef struct FILETIME { u32 dwLowDateTime, dwHighDateTime; } FILETIME;      // FILETIME

#define WIN32_IMPORT __declspec(dllimport)
#define WIN32_INVALID_HANDLE_VALUE ((HANDLE)(i64)-1)
#define WIN32_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(i64)-4)   // DPI_AWARENESS_CONTEXT
#define WIN32_IDC_ARROW ((const WCHAR*)(u64)32512)                            // LoadCursorW (MAKEINTRESOURCE)
#define WIN32_HWND_TOP ((HWND)0)                                              // SetWindowPos
#define WIN32_D3D_COMPILE_STANDARD_FILE_INCLUDE ((void*)(u64)1)               // D3DCompile pInclude

// ================================================================ constants
// enum constants are int: values >= 2^31 are written as (i32)0x...u and need a (u32) cast where a
// u32 parameter takes them.

// ---------------------------------------------------------------- kernel32
enum {
    WIN32_STD_INPUT_HANDLE  = -10,                 // GetStdHandle
    WIN32_STD_OUTPUT_HANDLE = -11,
    WIN32_STD_ERROR_HANDLE  = -12,

    WIN32_GENERIC_READ  = (i32)0x80000000u,        // CreateFileW dwDesiredAccess
    WIN32_GENERIC_WRITE = 0x40000000,
    WIN32_FILE_SHARE_READ   = 0x1,
    WIN32_FILE_SHARE_WRITE  = 0x2,
    WIN32_FILE_SHARE_DELETE = 0x4,
    WIN32_CREATE_NEW    = 1,                       // CreateFileW dwCreationDisposition
    WIN32_CREATE_ALWAYS = 2,
    WIN32_OPEN_EXISTING = 3,
    WIN32_OPEN_ALWAYS   = 4,
    WIN32_FILE_ATTRIBUTE_READONLY  = 0x1,
    WIN32_FILE_ATTRIBUTE_HIDDEN    = 0x2,
    WIN32_FILE_ATTRIBUTE_DIRECTORY = 0x10,
    WIN32_FILE_ATTRIBUTE_NORMAL    = 0x80,
    WIN32_FILE_ATTRIBUTE_TEMPORARY = 0x100,
    WIN32_INVALID_FILE_ATTRIBUTES  = -1,
    WIN32_GetFileExInfoStandard = 0,               // GetFileAttributesExW (GET_FILEEX_INFO_LEVELS)
    WIN32_FILE_TYPE_DISK = 1,                      // GetFileType
    WIN32_FILE_TYPE_CHAR = 2,
    WIN32_FILE_TYPE_PIPE = 3,

    WIN32_MEM_COMMIT  = 0x1000,                    // VirtualAlloc
    WIN32_MEM_RESERVE = 0x2000,
    WIN32_MEM_DECOMMIT = 0x4000,
    WIN32_MEM_RELEASE = 0x8000,
    WIN32_PAGE_NOACCESS  = 0x01,
    WIN32_PAGE_READONLY  = 0x02,
    WIN32_PAGE_READWRITE = 0x04,
    WIN32_FILE_MAP_READ  = 0x4,                    // MapViewOfFile

    WIN32_CP_UTF8 = 65001,                         // MultiByteToWideChar, SetConsoleOutputCP
    WIN32_INFINITE = -1,                           // WaitForSingleObject
    WIN32_WAIT_OBJECT_0 = 0,
    WIN32_WAIT_ABANDONED = 0x80,
    WIN32_WAIT_TIMEOUT = 0x102,
    WIN32_WAIT_FAILED = -1,
    WIN32_STARTF_USESTDHANDLES = 0x100,            // STARTUPINFOW.dwFlags
    WIN32_HANDLE_FLAG_INHERIT = 0x1,               // SetHandleInformation
    WIN32_MOVEFILE_REPLACE_EXISTING = 0x1,         // MoveFileExW
    WIN32_MOVEFILE_WRITE_THROUGH = 0x8,
    WIN32_SEM_FAILCRITICALERRORS = 0x0001,         // SetErrorMode
    WIN32_SEM_NOGPFAULTERRORBOX = 0x0002,
    WIN32_SEM_NOOPENFILEERRORBOX = 0x8000,
    WIN32_LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x800,    // LoadLibraryExW dwFlags

    WIN32_ERROR_SUCCESS = 0,                       // GetLastError (System Error Codes)
    WIN32_ERROR_FILE_NOT_FOUND = 2,
    WIN32_ERROR_PATH_NOT_FOUND = 3,
    WIN32_ERROR_ACCESS_DENIED = 5,
    WIN32_ERROR_SHARING_VIOLATION = 32,
    WIN32_ERROR_BROKEN_PIPE = 109,
    WIN32_ERROR_INSUFFICIENT_BUFFER = 122,
    WIN32_ERROR_ALREADY_EXISTS = 183,
    WIN32_ERROR_NO_DATA = 232,
    WIN32_ERROR_DEVICE_NOT_CONNECTED = 1167,

    WIN32_CREATE_SUSPENDED = 0x4,                  // CreateThread dwCreationFlags
    WIN32_STACK_SIZE_PARAM_IS_A_RESERVATION = 0x10000,
    WIN32_THREAD_PRIORITY_NORMAL = 0,              // SetThreadPriority
    WIN32_THREAD_PRIORITY_ABOVE_NORMAL = 1,
    WIN32_THREAD_PRIORITY_HIGHEST = 2,
    WIN32_THREAD_PRIORITY_TIME_CRITICAL = 15,
    WIN32_ALL_PROCESSOR_GROUPS = 0xffff,           // GetActiveProcessorCount
    WIN32_CREATE_WAITABLE_TIMER_MANUAL_RESET = 0x1,      // CreateWaitableTimerExW dwFlags
    WIN32_CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x2,
    WIN32_TIMER_ALL_ACCESS = 0x1F0003,             // CreateWaitableTimerExW dwDesiredAccess
    WIN32_SEMAPHORE_ALL_ACCESS = 0x1F0003,         // CreateSemaphoreExW dwDesiredAccess
    WIN32_EVENT_ALL_ACCESS = 0x1F0003,

    WIN32_EXCEPTION_ACCESS_VIOLATION = (i32)0xC0000005u,     // EXCEPTION_RECORD.ExceptionCode
    WIN32_EXCEPTION_ILLEGAL_INSTRUCTION = (i32)0xC000001Du,  // ud2 (UBSan trap mode, __builtin_trap)
    WIN32_EXCEPTION_INT_DIVIDE_BY_ZERO = (i32)0xC0000094u,
    WIN32_EXCEPTION_STACK_OVERFLOW = (i32)0xC00000FDu,
    WIN32_EXCEPTION_BREAKPOINT = (i32)0x80000003u,
    WIN32_EXCEPTION_CONTINUE_SEARCH = 0,           // exception filter / vectored handler results
    WIN32_EXCEPTION_EXECUTE_HANDLER = 1,
    WIN32_EXCEPTION_CONTINUE_EXECUTION = -1,
};

// ---------------------------------------------------------------- user32
enum {
    WIN32_WM_CREATE = 0x0001,                      // Window messages
    WIN32_WM_DESTROY = 0x0002,
    WIN32_WM_MOVE = 0x0003,
    WIN32_WM_SIZE = 0x0005,
    WIN32_WM_ACTIVATE = 0x0006,
    WIN32_WM_SETFOCUS = 0x0007,
    WIN32_WM_KILLFOCUS = 0x0008,
    WIN32_WM_PAINT = 0x000F,
    WIN32_WM_CLOSE = 0x0010,
    WIN32_WM_QUIT = 0x0012,
    WIN32_WM_ERASEBKGND = 0x0014,
    WIN32_WM_ACTIVATEAPP = 0x001C,
    WIN32_WM_SETCURSOR = 0x0020,
    WIN32_WM_GETMINMAXINFO = 0x0024,
    WIN32_WM_INPUT = 0x00FF,
    WIN32_WM_KEYDOWN = 0x0100,
    WIN32_WM_KEYUP = 0x0101,
    WIN32_WM_CHAR = 0x0102,
    WIN32_WM_SYSKEYDOWN = 0x0104,
    WIN32_WM_SYSKEYUP = 0x0105,
    WIN32_WM_SYSCHAR = 0x0106,
    WIN32_WM_SYSCOMMAND = 0x0112,
    WIN32_WM_MOUSEMOVE = 0x0200,
    WIN32_WM_LBUTTONDOWN = 0x0201,
    WIN32_WM_LBUTTONUP = 0x0202,
    WIN32_WM_RBUTTONDOWN = 0x0204,
    WIN32_WM_RBUTTONUP = 0x0205,
    WIN32_WM_MBUTTONDOWN = 0x0207,
    WIN32_WM_MBUTTONUP = 0x0208,
    WIN32_WM_MOUSEWHEEL = 0x020A,
    WIN32_WM_XBUTTONDOWN = 0x020B,
    WIN32_WM_XBUTTONUP = 0x020C,
    WIN32_WM_MOUSEHWHEEL = 0x020E,
    WIN32_WM_ENTERSIZEMOVE = 0x0231,
    WIN32_WM_EXITSIZEMOVE = 0x0232,
    WIN32_WM_POINTERUPDATE = 0x0245,
    WIN32_WM_POINTERDOWN = 0x0246,
    WIN32_WM_POINTERUP = 0x0247,
    WIN32_WM_POINTERENTER = 0x0249,
    WIN32_WM_POINTERLEAVE = 0x024A,
    WIN32_WM_POINTERCAPTURECHANGED = 0x024C,
    WIN32_WM_DPICHANGED = 0x02E0,
    WIN32_WM_APP = 0x8000,

    WIN32_WM_MENUCHAR = 0x0120,
    WIN32_MNC_CLOSE = 1,                           // WM_MENUCHAR result (high word)
    WIN32_SC_KEYMENU = 0xF100,                     // WM_SYSCOMMAND wParam (& 0xFFF0)
    WIN32_VK_PAUSE = 0x13,                         // Virtual-Key codes (only the ones whose scan code is ambiguous)
    WIN32_VK_SNAPSHOT = 0x2C,
    WIN32_VK_NUMLOCK = 0x90,
    WIN32_MAPVK_VK_TO_VSC_EX = 4,                  // MapVirtualKeyW uMapType
    WIN32_SIZE_RESTORED = 0,                       // WM_SIZE wParam
    WIN32_SIZE_MINIMIZED = 1,
    WIN32_SIZE_MAXIMIZED = 2,
    WIN32_WA_INACTIVE = 0,                         // WM_ACTIVATE wParam (low word)
    WIN32_WHEEL_DELTA = 120,                       // WM_MOUSEWHEEL
    WIN32_XBUTTON1 = 0x0001,                       // WM_XBUTTONDOWN wParam (high word)
    WIN32_XBUTTON2 = 0x0002,

    WIN32_CS_VREDRAW = 0x0001,                     // WNDCLASSEXW.style
    WIN32_CS_HREDRAW = 0x0002,
    WIN32_CS_OWNDC = 0x0020,
    WIN32_WS_OVERLAPPEDWINDOW = 0x00CF0000,        // CreateWindowExW dwStyle
    WIN32_WS_POPUP = (i32)0x80000000u,
    WIN32_WS_VISIBLE = 0x10000000,
    WIN32_WS_EX_APPWINDOW = 0x00040000,            // CreateWindowExW dwExStyle
    WIN32_WS_EX_NOREDIRECTIONBITMAP = 0x00200000,
    WIN32_CW_USEDEFAULT = (i32)0x80000000u,        // CreateWindowExW x, y, w, h
    WIN32_SW_HIDE = 0,                             // ShowWindow
    WIN32_SW_SHOWNORMAL = 1,
    WIN32_SW_SHOWNOACTIVATE = 4,
    WIN32_SW_SHOW = 5,
    WIN32_SW_MINIMIZE = 6,
    WIN32_SW_RESTORE = 9,
    WIN32_PM_REMOVE = 0x0001,                      // PeekMessageW
    WIN32_GWL_STYLE = -16,                         // SetWindowLongPtrW
    WIN32_GWLP_USERDATA = -21,
    WIN32_SWP_NOSIZE = 0x0001,                     // SetWindowPos
    WIN32_SWP_NOMOVE = 0x0002,
    WIN32_SWP_NOZORDER = 0x0004,
    WIN32_SWP_NOACTIVATE = 0x0010,
    WIN32_SWP_FRAMECHANGED = 0x0020,
    WIN32_SWP_NOOWNERZORDER = 0x0200,
    WIN32_MONITOR_DEFAULTTONEAREST = 0x00000002,   // MonitorFromWindow
    WIN32_USER_DEFAULT_SCREEN_DPI = 96,            // GetDpiForWindow

    WIN32_RIDEV_REMOVE = 0x00000001,               // RAWINPUTDEVICE.dwFlags
    WIN32_RIDEV_INPUTSINK = 0x00000100,
    WIN32_RIDEV_DEVNOTIFY = 0x00002000,
    WIN32_RID_INPUT = 0x10000003,                  // GetRawInputData uiCommand
    WIN32_RIM_TYPEMOUSE = 0,                       // RAWINPUTHEADER.dwType
    WIN32_RIM_TYPEKEYBOARD = 1,
    WIN32_MOUSE_MOVE_RELATIVE = 0x00,              // RAWMOUSE.usFlags
    WIN32_MOUSE_MOVE_ABSOLUTE = 0x01,
    WIN32_HID_USAGE_PAGE_GENERIC = 0x01,           // RAWINPUTDEVICE.usUsagePage / usUsage
    WIN32_HID_USAGE_GENERIC_MOUSE = 0x02,

    WIN32_PT_POINTER = 1,                          // POINTER_INPUT_TYPE
    WIN32_PT_TOUCH = 2,
    WIN32_PT_PEN = 3,
    WIN32_PT_MOUSE = 4,
    WIN32_POINTER_FLAG_NEW = 0x00000001,           // POINTER_INFO.pointerFlags
    WIN32_POINTER_FLAG_INRANGE = 0x00000002,
    WIN32_POINTER_FLAG_INCONTACT = 0x00000004,
    WIN32_POINTER_FLAG_FIRSTBUTTON = 0x00000010,
    WIN32_POINTER_FLAG_SECONDBUTTON = 0x00000020,
    WIN32_POINTER_FLAG_PRIMARY = 0x00002000,
    WIN32_POINTER_FLAG_CANCELED = 0x00008000,
    WIN32_POINTER_FLAG_DOWN = 0x00010000,
    WIN32_POINTER_FLAG_UPDATE = 0x00020000,
    WIN32_POINTER_FLAG_UP = 0x00040000,
};

// ---------------------------------------------------------------- xinput1_4 (XInput.h)
enum {
    WIN32_XUSER_MAX_COUNT = 4,
    WIN32_XINPUT_GAMEPAD_DPAD_UP = 0x0001,         // XINPUT_GAMEPAD.wButtons
    WIN32_XINPUT_GAMEPAD_DPAD_DOWN = 0x0002,
    WIN32_XINPUT_GAMEPAD_DPAD_LEFT = 0x0004,
    WIN32_XINPUT_GAMEPAD_DPAD_RIGHT = 0x0008,
    WIN32_XINPUT_GAMEPAD_START = 0x0010,
    WIN32_XINPUT_GAMEPAD_BACK = 0x0020,
    WIN32_XINPUT_GAMEPAD_LEFT_THUMB = 0x0040,
    WIN32_XINPUT_GAMEPAD_RIGHT_THUMB = 0x0080,
    WIN32_XINPUT_GAMEPAD_LEFT_SHOULDER = 0x0100,
    WIN32_XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200,
    WIN32_XINPUT_GAMEPAD_A = 0x1000,
    WIN32_XINPUT_GAMEPAD_B = 0x2000,
    WIN32_XINPUT_GAMEPAD_X = 0x4000,
    WIN32_XINPUT_GAMEPAD_Y = 0x8000,
    WIN32_XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE = 7849,
    WIN32_XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE = 8689,
    WIN32_XINPUT_GAMEPAD_TRIGGER_THRESHOLD = 30,
};

// ---------------------------------------------------------------- COM, ole32, WASAPI, avrt
enum {
    WIN32_S_OK = 0,                                // HRESULT values
    WIN32_S_FALSE = 1,
    WIN32_E_NOINTERFACE = (i32)0x80004002u,
    WIN32_E_POINTER = (i32)0x80004003u,
    WIN32_E_FAIL = (i32)0x80004005u,
    WIN32_E_OUTOFMEMORY = (i32)0x8007000Eu,
    WIN32_E_INVALIDARG = (i32)0x80070057u,
    WIN32_RPC_E_CHANGED_MODE = (i32)0x80010106u,

    WIN32_COINIT_MULTITHREADED = 0x0,              // CoInitializeEx
    WIN32_CLSCTX_ALL = 0x17,                       // CoCreateInstance, IMMDevice::Activate (CLSCTX_ALL)
    WIN32_eRender = 0,                             // EDataFlow
    WIN32_eConsole = 0,                            // ERole
    WIN32_eMultimedia = 1,
    WIN32_AUDCLNT_SHAREMODE_SHARED = 0,            // AUDCLNT_SHAREMODE
    WIN32_AUDCLNT_STREAMFLAGS_EVENTCALLBACK = 0x00040000,
    WIN32_AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY = 0x08000000,
    WIN32_AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM = (i32)0x80000000u,
    WIN32_AUDCLNT_BUFFERFLAGS_SILENT = 0x2,        // IAudioRenderClient::ReleaseBuffer
    WIN32_AUDCLNT_E_DEVICE_INVALIDATED = (i32)0x88890004u,
    WIN32_WAVE_FORMAT_IEEE_FLOAT = 0x0003,         // WAVEFORMATEX.wFormatTag
    WIN32_WAVE_FORMAT_EXTENSIBLE = 0xFFFE,
    WIN32_SPEAKER_FRONT_LEFT = 0x1,                // WAVEFORMATEXTENSIBLE.dwChannelMask
    WIN32_SPEAKER_FRONT_RIGHT = 0x2,
};

// ---------------------------------------------------------------- DXGI
enum {
    WIN32_DXGI_FORMAT_UNKNOWN = 0,                 // DXGI_FORMAT
    WIN32_DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    WIN32_DXGI_FORMAT_R32G32B32_FLOAT = 6,
    WIN32_DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    WIN32_DXGI_FORMAT_R32G32_FLOAT = 16,
    WIN32_DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    WIN32_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
    WIN32_DXGI_FORMAT_R8G8B8A8_UINT = 30,
    WIN32_DXGI_FORMAT_R16G16_FLOAT = 34,
    WIN32_DXGI_FORMAT_R32_FLOAT = 41,
    WIN32_DXGI_FORMAT_R32_UINT = 42,
    WIN32_DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
    WIN32_DXGI_FORMAT_R16_UINT = 57,
    WIN32_DXGI_FORMAT_R8_UNORM = 61,
    WIN32_DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    WIN32_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,

    WIN32_DXGI_USAGE_SHADER_INPUT = 0x10,          // DXGI_SWAP_CHAIN_DESC1.BufferUsage
    WIN32_DXGI_USAGE_RENDER_TARGET_OUTPUT = 0x20,
    WIN32_DXGI_SCALING_STRETCH = 0,                // DXGI_SCALING
    WIN32_DXGI_SCALING_NONE = 1,
    WIN32_DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL = 3,    // DXGI_SWAP_EFFECT
    WIN32_DXGI_SWAP_EFFECT_FLIP_DISCARD = 4,
    WIN32_DXGI_ALPHA_MODE_UNSPECIFIED = 0,         // DXGI_ALPHA_MODE
    WIN32_DXGI_ALPHA_MODE_IGNORE = 3,
    WIN32_DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 0x2,           // DXGI_SWAP_CHAIN_FLAG
    WIN32_DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT = 0x40,
    WIN32_DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING = 0x800,
    WIN32_DXGI_PRESENT_TEST = 0x1,                 // IDXGISwapChain::Present Flags
    WIN32_DXGI_PRESENT_DO_NOT_WAIT = 0x8,
    WIN32_DXGI_PRESENT_ALLOW_TEARING = 0x200,
    WIN32_DXGI_FEATURE_PRESENT_ALLOW_TEARING = 0,  // DXGI_FEATURE (IDXGIFactory5::CheckFeatureSupport)
    WIN32_DXGI_MWA_NO_WINDOW_CHANGES = 0x1,        // IDXGIFactory::MakeWindowAssociation
    WIN32_DXGI_MWA_NO_ALT_ENTER = 0x2,
    WIN32_DXGI_CREATE_FACTORY_DEBUG = 0x1,         // CreateDXGIFactory2

    WIN32_DXGI_STATUS_OCCLUDED = 0x087A0001,       // DXGI_STATUS / DXGI_ERROR (HRESULT)
    WIN32_DXGI_ERROR_INVALID_CALL = (i32)0x887A0001u,
    WIN32_DXGI_ERROR_NOT_FOUND = (i32)0x887A0002u,
    WIN32_DXGI_ERROR_UNSUPPORTED = (i32)0x887A0004u,
    WIN32_DXGI_ERROR_DEVICE_REMOVED = (i32)0x887A0005u,
    WIN32_DXGI_ERROR_DEVICE_HUNG = (i32)0x887A0006u,
    WIN32_DXGI_ERROR_DEVICE_RESET = (i32)0x887A0007u,
    WIN32_DXGI_ERROR_WAS_STILL_DRAWING = (i32)0x887A000Au,
    WIN32_DXGI_ERROR_DRIVER_INTERNAL_ERROR = (i32)0x887A0020u,
    WIN32_DXGI_ERROR_SDK_COMPONENT_MISSING = (i32)0x887A002Du,
};

// ---------------------------------------------------------------- D3D11, D3DCompile
enum {
    WIN32_D3D_DRIVER_TYPE_HARDWARE = 1,            // D3D_DRIVER_TYPE
    WIN32_D3D_DRIVER_TYPE_WARP = 5,
    WIN32_D3D_FEATURE_LEVEL_10_0 = 0xa000,         // D3D_FEATURE_LEVEL
    WIN32_D3D_FEATURE_LEVEL_11_0 = 0xb000,
    WIN32_D3D_FEATURE_LEVEL_11_1 = 0xb100,
    WIN32_D3D11_SDK_VERSION = 7,                   // D3D11CreateDevice SDKVersion
    WIN32_D3D11_CREATE_DEVICE_SINGLETHREADED = 0x1,     // D3D11_CREATE_DEVICE_FLAG
    WIN32_D3D11_CREATE_DEVICE_DEBUG = 0x2,
    WIN32_D3D11_CREATE_DEVICE_BGRA_SUPPORT = 0x20,

    WIN32_D3D11_USAGE_DEFAULT = 0,                 // D3D11_USAGE
    WIN32_D3D11_USAGE_IMMUTABLE = 1,
    WIN32_D3D11_USAGE_DYNAMIC = 2,
    WIN32_D3D11_USAGE_STAGING = 3,
    WIN32_D3D11_BIND_VERTEX_BUFFER = 0x1,          // D3D11_BIND_FLAG
    WIN32_D3D11_BIND_INDEX_BUFFER = 0x2,
    WIN32_D3D11_BIND_CONSTANT_BUFFER = 0x4,
    WIN32_D3D11_BIND_SHADER_RESOURCE = 0x8,
    WIN32_D3D11_BIND_RENDER_TARGET = 0x20,
    WIN32_D3D11_BIND_DEPTH_STENCIL = 0x40,
    WIN32_D3D11_BIND_UNORDERED_ACCESS = 0x80,
    WIN32_D3D11_CPU_ACCESS_WRITE = 0x10000,        // D3D11_CPU_ACCESS_FLAG
    WIN32_D3D11_CPU_ACCESS_READ = 0x20000,
    WIN32_D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 0x40,   // D3D11_RESOURCE_MISC_FLAG
    WIN32_D3D11_MAP_READ = 1,                      // D3D11_MAP
    WIN32_D3D11_MAP_WRITE = 2,
    WIN32_D3D11_MAP_READ_WRITE = 3,
    WIN32_D3D11_MAP_WRITE_DISCARD = 4,
    WIN32_D3D11_MAP_WRITE_NO_OVERWRITE = 5,
    WIN32_D3D11_SRV_DIMENSION_TEXTURE2D = 4,       // D3D11_SRV_DIMENSION
    WIN32_D3D11_RTV_DIMENSION_TEXTURE2D = 4,       // D3D11_RTV_DIMENSION
    WIN32_D3D11_DSV_DIMENSION_TEXTURE2D = 3,       // D3D11_DSV_DIMENSION
    WIN32_D3D11_INPUT_PER_VERTEX_DATA = 0,         // D3D11_INPUT_CLASSIFICATION
    WIN32_D3D11_INPUT_PER_INSTANCE_DATA = 1,
    WIN32_D3D11_APPEND_ALIGNED_ELEMENT = -1,       // 0xffffffff
    WIN32_D3D11_PRIMITIVE_TOPOLOGY_POINTLIST = 1,  // D3D11_PRIMITIVE_TOPOLOGY
    WIN32_D3D11_PRIMITIVE_TOPOLOGY_LINELIST = 2,
    WIN32_D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP = 3,
    WIN32_D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4,
    WIN32_D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5,
    WIN32_D3D11_BLEND_ZERO = 1,                    // D3D11_BLEND
    WIN32_D3D11_BLEND_ONE = 2,
    WIN32_D3D11_BLEND_SRC_COLOR = 3,
    WIN32_D3D11_BLEND_INV_SRC_COLOR = 4,
    WIN32_D3D11_BLEND_SRC_ALPHA = 5,
    WIN32_D3D11_BLEND_INV_SRC_ALPHA = 6,
    WIN32_D3D11_BLEND_DEST_ALPHA = 7,
    WIN32_D3D11_BLEND_INV_DEST_ALPHA = 8,
    WIN32_D3D11_BLEND_DEST_COLOR = 9,
    WIN32_D3D11_BLEND_INV_DEST_COLOR = 10,
    WIN32_D3D11_BLEND_OP_ADD = 1,                  // D3D11_BLEND_OP
    WIN32_D3D11_BLEND_OP_SUBTRACT = 2,
    WIN32_D3D11_BLEND_OP_REV_SUBTRACT = 3,
    WIN32_D3D11_BLEND_OP_MIN = 4,
    WIN32_D3D11_BLEND_OP_MAX = 5,
    WIN32_D3D11_COLOR_WRITE_ENABLE_ALL = 0xF,      // D3D11_COLOR_WRITE_ENABLE
    WIN32_D3D11_FILL_WIREFRAME = 2,                // D3D11_FILL_MODE
    WIN32_D3D11_FILL_SOLID = 3,
    WIN32_D3D11_CULL_NONE = 1,                     // D3D11_CULL_MODE
    WIN32_D3D11_CULL_FRONT = 2,
    WIN32_D3D11_CULL_BACK = 3,
    WIN32_D3D11_COMPARISON_NEVER = 1,              // D3D11_COMPARISON_FUNC
    WIN32_D3D11_COMPARISON_LESS = 2,
    WIN32_D3D11_COMPARISON_EQUAL = 3,
    WIN32_D3D11_COMPARISON_LESS_EQUAL = 4,
    WIN32_D3D11_COMPARISON_GREATER = 5,
    WIN32_D3D11_COMPARISON_NOT_EQUAL = 6,
    WIN32_D3D11_COMPARISON_GREATER_EQUAL = 7,
    WIN32_D3D11_COMPARISON_ALWAYS = 8,
    WIN32_D3D11_DEPTH_WRITE_MASK_ZERO = 0,         // D3D11_DEPTH_WRITE_MASK
    WIN32_D3D11_DEPTH_WRITE_MASK_ALL = 1,
    WIN32_D3D11_STENCIL_OP_KEEP = 1,               // D3D11_STENCIL_OP
    WIN32_D3D11_FILTER_MIN_MAG_MIP_POINT = 0,      // D3D11_FILTER
    WIN32_D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT = 0x14,
    WIN32_D3D11_FILTER_MIN_MAG_MIP_LINEAR = 0x15,
    WIN32_D3D11_FILTER_ANISOTROPIC = 0x55,
    WIN32_D3D11_TEXTURE_ADDRESS_WRAP = 1,          // D3D11_TEXTURE_ADDRESS_MODE
    WIN32_D3D11_TEXTURE_ADDRESS_MIRROR = 2,
    WIN32_D3D11_TEXTURE_ADDRESS_CLAMP = 3,
    WIN32_D3D11_TEXTURE_ADDRESS_BORDER = 4,
    WIN32_D3D11_CLEAR_DEPTH = 0x1,                 // D3D11_CLEAR_FLAG
    WIN32_D3D11_CLEAR_STENCIL = 0x2,
    WIN32_D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION = 16384,
    WIN32_D3D11_RLDO_SUMMARY = 0x1,                // D3D11_RLDO_FLAGS (ID3D11Debug::ReportLiveDeviceObjects)
    WIN32_D3D11_RLDO_DETAIL = 0x2,
    WIN32_D3D11_RLDO_IGNORE_INTERNAL = 0x4,
    WIN32_D3D11_MESSAGE_SEVERITY_CORRUPTION = 0,   // D3D11_MESSAGE_SEVERITY
    WIN32_D3D11_MESSAGE_SEVERITY_ERROR = 1,
    WIN32_D3D11_MESSAGE_SEVERITY_WARNING = 2,
    WIN32_D3D11_MESSAGE_SEVERITY_INFO = 3,
    WIN32_D3D11_MESSAGE_SEVERITY_MESSAGE = 4,

    WIN32_D3DCOMPILE_DEBUG = 0x1,                  // D3DCompile Flags1
    WIN32_D3DCOMPILE_SKIP_OPTIMIZATION = 0x4,
    WIN32_D3DCOMPILE_ENABLE_STRICTNESS = 0x800,
    WIN32_D3DCOMPILE_OPTIMIZATION_LEVEL3 = 0x8000,
    WIN32_D3DCOMPILE_WARNINGS_ARE_ERRORS = 0x40000,
};

// ================================================================ structs
// ---------------------------------------------------------------- kernel32
typedef struct SECURITY_ATTRIBUTES {               // SECURITY_ATTRIBUTES
    u32   nLength;
    void* lpSecurityDescriptor;
    i32   bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct STARTUPINFOW {                      // STARTUPINFOW
    u32    cb;
    WCHAR* lpReserved;
    WCHAR* lpDesktop;
    WCHAR* lpTitle;
    u32    dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    u16    wShowWindow, cbReserved2;
    u8*    lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;

typedef struct PROCESS_INFORMATION {               // PROCESS_INFORMATION
    HANDLE hProcess, hThread;
    u32    dwProcessId, dwThreadId;
} PROCESS_INFORMATION;

typedef struct WIN32_FIND_DATAW {                  // WIN32_FIND_DATAW
    u32      dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    u32      nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR    cFileName[260];
    WCHAR    cAlternateFileName[14];               // (the SDK's _MAC-only fields are not present on Windows)
} WIN32_FIND_DATAW;

typedef struct WIN32_FILE_ATTRIBUTE_DATA {         // WIN32_FILE_ATTRIBUTE_DATA (GetFileAttributesExW)
    u32      dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    u32      nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct SYSTEM_INFO {                       // SYSTEM_INFO (the dwOemId union is the first two u16)
    u16   wProcessorArchitecture;
    u16   wReserved;
    u32   dwPageSize;
    void* lpMinimumApplicationAddress;
    void* lpMaximumApplicationAddress;
    u64   dwActiveProcessorMask;
    u32   dwNumberOfProcessors;
    u32   dwProcessorType;
    u32   dwAllocationGranularity;
    u16   wProcessorLevel;
    u16   wProcessorRevision;
} SYSTEM_INFO;

typedef struct CONTEXT CONTEXT;                    // CONTEXT: opaque here (only passed by pointer)
typedef struct EXCEPTION_RECORD {                  // EXCEPTION_RECORD
    u32   ExceptionCode;
    u32   ExceptionFlags;
    struct EXCEPTION_RECORD* ExceptionRecord;
    void* ExceptionAddress;
    u32   NumberParameters;
    u64   ExceptionInformation[15];
} EXCEPTION_RECORD;
typedef struct EXCEPTION_POINTERS {                // EXCEPTION_POINTERS
    EXCEPTION_RECORD* ExceptionRecord;
    CONTEXT* ContextRecord;
} EXCEPTION_POINTERS;

typedef u32 (*LPTHREAD_START_ROUTINE)(void* parameter);                 // ThreadProc
typedef i32 (*PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS* info);   // VectoredHandler
typedef i32 (*LPTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS* info);  // UnhandledExceptionFilter

// ---------------------------------------------------------------- user32
typedef LRESULT (*WNDPROC)(HWND hwnd, u32 message, WPARAM wparam, LPARAM lparam);   // WindowProc

typedef struct WNDCLASSEXW {                       // WNDCLASSEXW
    u32          cbSize;
    u32          style;
    WNDPROC      lpfnWndProc;
    i32          cbClsExtra;
    i32          cbWndExtra;
    HINSTANCE    hInstance;
    HICON        hIcon;
    HCURSOR      hCursor;
    HBRUSH       hbrBackground;
    const WCHAR* lpszMenuName;
    const WCHAR* lpszClassName;
    HICON        hIconSm;
} WNDCLASSEXW;

typedef struct MSG {                               // MSG
    HWND   hwnd;
    u32    message;
    WPARAM wParam;
    LPARAM lParam;
    u32    time;
    POINT  pt;                                     // (the SDK's _MAC-only lPrivate is not present on Windows)
} MSG;

typedef struct MONITORINFO {                       // MONITORINFO
    u32  cbSize;
    RECT rcMonitor;
    RECT rcWork;
    u32  dwFlags;
} MONITORINFO;

typedef struct WINDOWPLACEMENT {                   // WINDOWPLACEMENT
    u32   length;
    u32   flags;
    u32   showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT  rcNormalPosition;
} WINDOWPLACEMENT;

typedef struct RAWINPUTDEVICE {                    // RAWINPUTDEVICE
    u16  usUsagePage;
    u16  usUsage;
    u32  dwFlags;
    HWND hwndTarget;
} RAWINPUTDEVICE;

typedef struct RAWINPUTHEADER {                    // RAWINPUTHEADER
    u32    dwType;
    u32    dwSize;
    HANDLE hDevice;
    WPARAM wParam;
} RAWINPUTHEADER;

typedef struct RAWMOUSE {                          // RAWMOUSE
    u16 usFlags;
    union {
        u32 ulButtons;
        struct { u16 usButtonFlags; u16 usButtonData; };
    };
    u32 ulRawButtons;
    i32 lLastX;
    i32 lLastY;
    u32 ulExtraInformation;
} RAWMOUSE;

typedef struct RAWKEYBOARD {                       // RAWKEYBOARD
    u16 MakeCode;
    u16 Flags;
    u16 Reserved;
    u16 VKey;
    u32 Message;
    u32 ExtraInformation;
} RAWKEYBOARD;

typedef struct RAWINPUT {                          // RAWINPUT (the RAWHID member is not used)
    RAWINPUTHEADER header;
    union {
        RAWMOUSE    mouse;
        RAWKEYBOARD keyboard;
    } data;
} RAWINPUT;

typedef struct POINTER_INFO {                      // POINTER_INFO
    u32    pointerType;                            // WIN32_PT_*
    u32    pointerId;
    u32    frameId;
    u32    pointerFlags;                           // WIN32_POINTER_FLAG_*
    HANDLE sourceDevice;
    HWND   hwndTarget;
    POINT  ptPixelLocation;
    POINT  ptHimetricLocation;
    POINT  ptPixelLocationRaw;
    POINT  ptHimetricLocationRaw;
    u32    dwTime;
    u32    historyCount;
    i32    InputData;
    u32    dwKeyStates;
    u64    PerformanceCount;
    u32    ButtonChangeType;
} POINTER_INFO;

typedef struct POINTER_PEN_INFO {                  // POINTER_PEN_INFO
    POINTER_INFO pointerInfo;
    u32 penFlags;
    u32 penMask;
    u32 pressure;                                  // 0..1024
    u32 rotation;
    i32 tiltX;
    i32 tiltY;
} POINTER_PEN_INFO;

// ---------------------------------------------------------------- xinput1_4
typedef struct XINPUT_GAMEPAD {                    // XINPUT_GAMEPAD
    u16 wButtons;
    u8  bLeftTrigger;
    u8  bRightTrigger;
    i16 sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_GAMEPAD;
typedef struct XINPUT_STATE { u32 dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;      // XINPUT_STATE
typedef struct XINPUT_VIBRATION { u16 wLeftMotorSpeed, wRightMotorSpeed; } XINPUT_VIBRATION;  // XINPUT_VIBRATION

// ---------------------------------------------------------------- WASAPI (packed to 1 byte like mmreg.h)
#pragma pack(push, 1)
typedef struct WAVEFORMATEX {                      // WAVEFORMATEX
    u16 wFormatTag;
    u16 nChannels;
    u32 nSamplesPerSec;
    u32 nAvgBytesPerSec;
    u16 nBlockAlign;
    u16 wBitsPerSample;
    u16 cbSize;
} WAVEFORMATEX;
typedef struct WAVEFORMATEXTENSIBLE {              // WAVEFORMATEXTENSIBLE
    WAVEFORMATEX Format;
    union { u16 wValidBitsPerSample; u16 wSamplesPerBlock; u16 wReserved; } Samples;
    u32  dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE;
#pragma pack(pop)

// ---------------------------------------------------------------- DXGI
typedef struct DXGI_SAMPLE_DESC { u32 Count; u32 Quality; } DXGI_SAMPLE_DESC;   // DXGI_SAMPLE_DESC

typedef struct DXGI_SWAP_CHAIN_DESC1 {             // DXGI_SWAP_CHAIN_DESC1
    u32 Width;
    u32 Height;
    u32 Format;                                    // WIN32_DXGI_FORMAT_*
    i32 Stereo;
    DXGI_SAMPLE_DESC SampleDesc;
    u32 BufferUsage;
    u32 BufferCount;
    u32 Scaling;
    u32 SwapEffect;
    u32 AlphaMode;
    u32 Flags;
} DXGI_SWAP_CHAIN_DESC1;

typedef struct DXGI_ADAPTER_DESC {                 // DXGI_ADAPTER_DESC
    WCHAR  Description[128];
    u32    VendorId;
    u32    DeviceId;
    u32    SubSysId;
    u32    Revision;
    size_t DedicatedVideoMemory;
    size_t DedicatedSystemMemory;
    size_t SharedSystemMemory;
    LUID   AdapterLuid;
} DXGI_ADAPTER_DESC;

// ---------------------------------------------------------------- D3D11
typedef struct D3D11_BUFFER_DESC {                 // D3D11_BUFFER_DESC
    u32 ByteWidth;
    u32 Usage;
    u32 BindFlags;
    u32 CPUAccessFlags;
    u32 MiscFlags;
    u32 StructureByteStride;
} D3D11_BUFFER_DESC;

typedef struct D3D11_TEXTURE2D_DESC {              // D3D11_TEXTURE2D_DESC
    u32 Width;
    u32 Height;
    u32 MipLevels;
    u32 ArraySize;
    u32 Format;
    DXGI_SAMPLE_DESC SampleDesc;
    u32 Usage;
    u32 BindFlags;
    u32 CPUAccessFlags;
    u32 MiscFlags;
} D3D11_TEXTURE2D_DESC;

typedef struct D3D11_SUBRESOURCE_DATA {            // D3D11_SUBRESOURCE_DATA
    const void* pSysMem;
    u32 SysMemPitch;
    u32 SysMemSlicePitch;
} D3D11_SUBRESOURCE_DATA;

// View descriptions: only the Texture2D member of the SDK's union is declared; the padding keeps
// the union at its SDK size.
typedef struct D3D11_SHADER_RESOURCE_VIEW_DESC {   // D3D11_SHADER_RESOURCE_VIEW_DESC
    u32 Format;
    u32 ViewDimension;                             // WIN32_D3D11_SRV_DIMENSION_*
    union {
        struct { u32 MostDetailedMip; u32 MipLevels; } Texture2D;   // D3D11_TEX2D_SRV
        u32 _union_size[4];
    };
} D3D11_SHADER_RESOURCE_VIEW_DESC;

typedef struct D3D11_RENDER_TARGET_VIEW_DESC {     // D3D11_RENDER_TARGET_VIEW_DESC
    u32 Format;
    u32 ViewDimension;                             // WIN32_D3D11_RTV_DIMENSION_*
    union {
        struct { u32 MipSlice; } Texture2D;        // D3D11_TEX2D_RTV
        u32 _union_size[3];
    };
} D3D11_RENDER_TARGET_VIEW_DESC;

typedef struct D3D11_DEPTH_STENCIL_VIEW_DESC {     // D3D11_DEPTH_STENCIL_VIEW_DESC
    u32 Format;
    u32 ViewDimension;                             // WIN32_D3D11_DSV_DIMENSION_*
    u32 Flags;
    union {
        struct { u32 MipSlice; } Texture2D;        // D3D11_TEX2D_DSV
        u32 _union_size[3];
    };
} D3D11_DEPTH_STENCIL_VIEW_DESC;

typedef struct D3D11_INPUT_ELEMENT_DESC {          // D3D11_INPUT_ELEMENT_DESC
    const char* SemanticName;
    u32 SemanticIndex;
    u32 Format;
    u32 InputSlot;
    u32 AlignedByteOffset;
    u32 InputSlotClass;                            // WIN32_D3D11_INPUT_PER_*
    u32 InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;

typedef struct D3D11_RENDER_TARGET_BLEND_DESC {    // D3D11_RENDER_TARGET_BLEND_DESC
    i32 BlendEnable;
    u32 SrcBlend;
    u32 DestBlend;
    u32 BlendOp;
    u32 SrcBlendAlpha;
    u32 DestBlendAlpha;
    u32 BlendOpAlpha;
    u8  RenderTargetWriteMask;
} D3D11_RENDER_TARGET_BLEND_DESC;

typedef struct D3D11_BLEND_DESC {                  // D3D11_BLEND_DESC
    i32 AlphaToCoverageEnable;
    i32 IndependentBlendEnable;
    D3D11_RENDER_TARGET_BLEND_DESC RenderTarget[8];
} D3D11_BLEND_DESC;

typedef struct D3D11_RASTERIZER_DESC {             // D3D11_RASTERIZER_DESC
    u32 FillMode;
    u32 CullMode;
    i32 FrontCounterClockwise;
    i32 DepthBias;
    f32 DepthBiasClamp;
    f32 SlopeScaledDepthBias;
    i32 DepthClipEnable;
    i32 ScissorEnable;
    i32 MultisampleEnable;
    i32 AntialiasedLineEnable;
} D3D11_RASTERIZER_DESC;

typedef struct D3D11_DEPTH_STENCILOP_DESC {        // D3D11_DEPTH_STENCILOP_DESC
    u32 StencilFailOp;
    u32 StencilDepthFailOp;
    u32 StencilPassOp;
    u32 StencilFunc;
} D3D11_DEPTH_STENCILOP_DESC;

typedef struct D3D11_DEPTH_STENCIL_DESC {          // D3D11_DEPTH_STENCIL_DESC
    i32 DepthEnable;
    u32 DepthWriteMask;
    u32 DepthFunc;
    i32 StencilEnable;
    u8  StencilReadMask;
    u8  StencilWriteMask;
    D3D11_DEPTH_STENCILOP_DESC FrontFace;
    D3D11_DEPTH_STENCILOP_DESC BackFace;
} D3D11_DEPTH_STENCIL_DESC;

typedef struct D3D11_SAMPLER_DESC {                // D3D11_SAMPLER_DESC
    u32 Filter;
    u32 AddressU;
    u32 AddressV;
    u32 AddressW;
    f32 MipLODBias;
    u32 MaxAnisotropy;
    u32 ComparisonFunc;
    f32 BorderColor[4];
    f32 MinLOD;
    f32 MaxLOD;                                    // D3D11_FLOAT32_MAX: use BMF_F32_MAX
} D3D11_SAMPLER_DESC;

typedef struct D3D11_VIEWPORT {                    // D3D11_VIEWPORT
    f32 TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth;
} D3D11_VIEWPORT;

typedef struct D3D11_BOX { u32 left, top, front, right, bottom, back; } D3D11_BOX;   // D3D11_BOX

typedef struct D3D11_MAPPED_SUBRESOURCE {          // D3D11_MAPPED_SUBRESOURCE
    void* pData;
    u32   RowPitch;
    u32   DepthPitch;
} D3D11_MAPPED_SUBRESOURCE;

typedef struct D3D11_MESSAGE {                     // D3D11_MESSAGE (ID3D11InfoQueue::GetMessage)
    u32 Category;
    u32 Severity;                                  // WIN32_D3D11_MESSAGE_SEVERITY_*
    u32 ID;
    const char* pDescription;
    size_t DescriptionByteLength;
} D3D11_MESSAGE;

BMF_STATIC_ASSERT(sizeof(GUID) == 16 && sizeof(RECT) == 16 && sizeof(POINT) == 8, "basic struct layout");
BMF_STATIC_ASSERT(sizeof(SECURITY_ATTRIBUTES) == 24 && sizeof(STARTUPINFOW) == 104 && sizeof(PROCESS_INFORMATION) == 24, "kernel32 struct layout");
BMF_STATIC_ASSERT(sizeof(WIN32_FIND_DATAW) == 592 && sizeof(WIN32_FILE_ATTRIBUTE_DATA) == 36 && sizeof(SYSTEM_INFO) == 48, "kernel32 struct layout");
BMF_STATIC_ASSERT(sizeof(EXCEPTION_RECORD) == 152 && sizeof(EXCEPTION_POINTERS) == 16, "kernel32 struct layout");
BMF_STATIC_ASSERT(sizeof(WNDCLASSEXW) == 80 && sizeof(MSG) == 48 && sizeof(MONITORINFO) == 40 && sizeof(WINDOWPLACEMENT) == 44, "user32 struct layout");
BMF_STATIC_ASSERT(sizeof(RAWINPUTDEVICE) == 16 && sizeof(RAWINPUTHEADER) == 24 && sizeof(RAWMOUSE) == 24 && sizeof(RAWKEYBOARD) == 16, "user32 struct layout");
BMF_STATIC_ASSERT(sizeof(RAWINPUT) == 48 && sizeof(POINTER_INFO) == 96 && sizeof(POINTER_PEN_INFO) == 120, "user32 struct layout");
BMF_STATIC_ASSERT(sizeof(XINPUT_GAMEPAD) == 12 && sizeof(XINPUT_STATE) == 16 && sizeof(XINPUT_VIBRATION) == 4, "xinput struct layout");
BMF_STATIC_ASSERT(sizeof(WAVEFORMATEX) == 18 && sizeof(WAVEFORMATEXTENSIBLE) == 40, "WASAPI struct layout");
BMF_STATIC_ASSERT(sizeof(DXGI_SWAP_CHAIN_DESC1) == 48 && sizeof(DXGI_ADAPTER_DESC) == 304, "DXGI struct layout");
BMF_STATIC_ASSERT(sizeof(D3D11_BUFFER_DESC) == 24 && sizeof(D3D11_TEXTURE2D_DESC) == 44 && sizeof(D3D11_SUBRESOURCE_DATA) == 16, "D3D11 struct layout");
BMF_STATIC_ASSERT(sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC) == 24 && sizeof(D3D11_RENDER_TARGET_VIEW_DESC) == 20 && sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC) == 24, "D3D11 struct layout");
BMF_STATIC_ASSERT(sizeof(D3D11_INPUT_ELEMENT_DESC) == 32 && sizeof(D3D11_BLEND_DESC) == 264 && sizeof(D3D11_RASTERIZER_DESC) == 40, "D3D11 struct layout");
BMF_STATIC_ASSERT(sizeof(D3D11_DEPTH_STENCIL_DESC) == 52 && sizeof(D3D11_SAMPLER_DESC) == 52 && sizeof(D3D11_VIEWPORT) == 24, "D3D11 struct layout");
BMF_STATIC_ASSERT(sizeof(D3D11_BOX) == 24 && sizeof(D3D11_MAPPED_SUBRESOURCE) == 16 && sizeof(D3D11_MESSAGE) == 32, "D3D11 struct layout");

// ================================================================ kernel32.dll
WIN32_IMPORT __attribute__((noreturn)) void ExitProcess(u32 uExitCode);                         // ExitProcess
WIN32_IMPORT u32    GetLastError(void);                                                         // GetLastError
WIN32_IMPORT u32    SetErrorMode(u32 uMode);                                                    // SetErrorMode
WIN32_IMPORT HANDLE GetStdHandle(u32 nStdHandle);                                               // GetStdHandle
WIN32_IMPORT WCHAR* GetCommandLineW(void);                                                      // GetCommandLineW
WIN32_IMPORT i32    WriteFile(HANDLE h, const void* buf, u32 n, u32* written, void* overlapped); // WriteFile
WIN32_IMPORT i32    ReadFile(HANDLE h, void* buf, u32 n, u32* read, void* overlapped);           // ReadFile
WIN32_IMPORT HANDLE CreateFileW(const WCHAR* name, u32 access, u32 share, SECURITY_ATTRIBUTES* sa,
                                u32 disposition, u32 flags, HANDLE template_file);              // CreateFileW
WIN32_IMPORT i32    CloseHandle(HANDLE h);                                                      // CloseHandle
WIN32_IMPORT i32    GetFileSizeEx(HANDLE h, i64* size);                                         // GetFileSizeEx
WIN32_IMPORT i32    FlushFileBuffers(HANDLE h);                                                 // FlushFileBuffers
WIN32_IMPORT u32    GetFileType(HANDLE h);                                                      // GetFileType
WIN32_IMPORT u32    GetFileAttributesW(const WCHAR* name);                                      // GetFileAttributesW
WIN32_IMPORT i32    GetFileAttributesExW(const WCHAR* name, i32 info_level, void* info);        // GetFileAttributesExW
WIN32_IMPORT i32    SetFileAttributesW(const WCHAR* name, u32 attributes);                      // SetFileAttributesW
WIN32_IMPORT i32    CreateDirectoryW(const WCHAR* name, SECURITY_ATTRIBUTES* sa);               // CreateDirectoryW
WIN32_IMPORT i32    RemoveDirectoryW(const WCHAR* name);                                        // RemoveDirectoryW
WIN32_IMPORT i32    DeleteFileW(const WCHAR* name);                                             // DeleteFileW
WIN32_IMPORT i32    MoveFileExW(const WCHAR* from, const WCHAR* to, u32 flags);                 // MoveFileExW
WIN32_IMPORT i32    CopyFileW(const WCHAR* from, const WCHAR* to, i32 fail_if_exists);          // CopyFileW
WIN32_IMPORT HANDLE FindFirstFileW(const WCHAR* pattern, WIN32_FIND_DATAW* data);               // FindFirstFileW
WIN32_IMPORT i32    FindNextFileW(HANDLE find, WIN32_FIND_DATAW* data);                         // FindNextFileW
WIN32_IMPORT i32    FindClose(HANDLE find);                                                     // FindClose
WIN32_IMPORT HANDLE CreateFileMappingW(HANDLE file, SECURITY_ATTRIBUTES* sa, u32 protect,
                                       u32 size_high, u32 size_low, const WCHAR* name);         // CreateFileMappingW
WIN32_IMPORT void*  MapViewOfFile(HANDLE mapping, u32 access, u32 offset_high, u32 offset_low,
                                  size_t bytes);                                                // MapViewOfFile
WIN32_IMPORT i32    UnmapViewOfFile(const void* base);                                          // UnmapViewOfFile
WIN32_IMPORT i32    PeekNamedPipe(HANDLE pipe, void* buf, u32 size, u32* read, u32* available,
                                  u32* left_this_message);                                      // PeekNamedPipe
WIN32_IMPORT i32    GetConsoleMode(HANDLE console, u32* mode);                                  // GetConsoleMode
WIN32_IMPORT i32    SetConsoleOutputCP(u32 code_page);                                          // SetConsoleOutputCP
WIN32_IMPORT u32    GetFullPathNameW(const WCHAR* name, u32 cap, WCHAR* out, WCHAR** file_part); // GetFullPathNameW
WIN32_IMPORT u32    GetModuleFileNameW(HMODULE mod, WCHAR* out, u32 cap);                       // GetModuleFileNameW
WIN32_IMPORT u32    SearchPathW(const WCHAR* path, const WCHAR* name, const WCHAR* ext, u32 cap,
                                WCHAR* out, WCHAR** file_part);                                 // SearchPathW
WIN32_IMPORT u32    GetEnvironmentVariableW(const WCHAR* name, WCHAR* out, u32 cap);            // GetEnvironmentVariableW
WIN32_IMPORT i32    MultiByteToWideChar(u32 cp, u32 flags, const char* s, i32 n, WCHAR* out, i32 cap);  // MultiByteToWideChar
WIN32_IMPORT i32    WideCharToMultiByte(u32 cp, u32 flags, const WCHAR* s, i32 n, char* out, i32 cap,
                                        const char* default_char, i32* used_default);           // WideCharToMultiByte
WIN32_IMPORT i32    CreateProcessW(const WCHAR* app, WCHAR* cmdline, SECURITY_ATTRIBUTES* psa,
                                   SECURITY_ATTRIBUTES* tsa, i32 inherit, u32 flags, void* env,
                                   const WCHAR* cwd, STARTUPINFOW* si, PROCESS_INFORMATION* pi); // CreateProcessW
WIN32_IMPORT HANDLE GetCurrentProcess(void);                                                    // GetCurrentProcess
WIN32_IMPORT u32    GetCurrentProcessId(void);                                                  // GetCurrentProcessId
WIN32_IMPORT i32    TerminateProcess(HANDLE process, u32 exit_code);                            // TerminateProcess
WIN32_IMPORT i32    GetExitCodeProcess(HANDLE process, u32* code);                              // GetExitCodeProcess
WIN32_IMPORT i32    CreatePipe(HANDLE* read, HANDLE* write, SECURITY_ATTRIBUTES* sa, u32 size); // CreatePipe
WIN32_IMPORT i32    SetHandleInformation(HANDLE h, u32 mask, u32 flags);                        // SetHandleInformation
WIN32_IMPORT i32    IsDebuggerPresent(void);                                                    // IsDebuggerPresent
// Memory and system info
WIN32_IMPORT void*  VirtualAlloc(void* addr, size_t size, u32 type, u32 protect);              // VirtualAlloc
WIN32_IMPORT i32    VirtualFree(void* addr, size_t size, u32 type);                             // VirtualFree
WIN32_IMPORT void   GetSystemInfo(SYSTEM_INFO* info);                                           // GetSystemInfo
WIN32_IMPORT u32    GetActiveProcessorCount(u16 group);                                         // GetActiveProcessorCount
// Time and waiting
WIN32_IMPORT i32    QueryPerformanceCounter(i64* count);                                        // QueryPerformanceCounter
WIN32_IMPORT i32    QueryPerformanceFrequency(i64* freq);                                       // QueryPerformanceFrequency
WIN32_IMPORT void   Sleep(u32 ms);                                                              // Sleep
WIN32_IMPORT HANDLE CreateWaitableTimerExW(SECURITY_ATTRIBUTES* sa, const WCHAR* name, u32 flags,
                                           u32 access);                                         // CreateWaitableTimerExW
WIN32_IMPORT i32    SetWaitableTimer(HANDLE timer, const i64* due_time, i32 period_ms, void* completion,
                                     void* completion_arg, i32 resume);                         // SetWaitableTimer
WIN32_IMPORT u32    WaitForSingleObject(HANDLE h, u32 ms);                                      // WaitForSingleObject
WIN32_IMPORT u32    WaitForSingleObjectEx(HANDLE h, u32 ms, i32 alertable);                     // WaitForSingleObjectEx
WIN32_IMPORT u32    WaitForMultipleObjects(u32 count, const HANDLE* handles, i32 wait_all, u32 ms); // WaitForMultipleObjects
// Threads and synchronization
WIN32_IMPORT HANDLE CreateThread(SECURITY_ATTRIBUTES* sa, size_t stack_size, LPTHREAD_START_ROUTINE start,
                                 void* parameter, u32 flags, u32* thread_id);                   // CreateThread
WIN32_IMPORT HANDLE GetCurrentThread(void);                                                     // GetCurrentThread
WIN32_IMPORT u32    GetCurrentThreadId(void);                                                   // GetCurrentThreadId
WIN32_IMPORT i32    SetThreadPriority(HANDLE thread, i32 priority);                             // SetThreadPriority
WIN32_IMPORT HRESULT SetThreadDescription(HANDLE thread, const WCHAR* description);             // SetThreadDescription
WIN32_IMPORT i32    SetThreadStackGuarantee(u32* stack_size_in_bytes);                          // SetThreadStackGuarantee
WIN32_IMPORT i32    SwitchToThread(void);                                                       // SwitchToThread
WIN32_IMPORT HANDLE CreateSemaphoreExW(SECURITY_ATTRIBUTES* sa, i32 initial, i32 maximum, const WCHAR* name,
                                       u32 flags, u32 access);                                  // CreateSemaphoreExW
WIN32_IMPORT i32    ReleaseSemaphore(HANDLE semaphore, i32 release_count, i32* previous_count); // ReleaseSemaphore
WIN32_IMPORT HANDLE CreateEventW(SECURITY_ATTRIBUTES* sa, i32 manual_reset, i32 initial_state,
                                 const WCHAR* name);                                            // CreateEventW
WIN32_IMPORT i32    SetEvent(HANDLE event);                                                     // SetEvent
WIN32_IMPORT i32    ResetEvent(HANDLE event);                                                   // ResetEvent
// DLLs
WIN32_IMPORT HMODULE LoadLibraryW(const WCHAR* name);                                           // LoadLibraryW
WIN32_IMPORT HMODULE LoadLibraryExW(const WCHAR* name, HANDLE reserved, u32 flags);             // LoadLibraryExW
WIN32_IMPORT i32    FreeLibrary(HMODULE mod);                                                   // FreeLibrary
WIN32_IMPORT HMODULE GetModuleHandleW(const WCHAR* name);                                       // GetModuleHandleW
WIN32_IMPORT void*  GetProcAddress(HMODULE mod, const char* name);                              // GetProcAddress
// Crash handling
WIN32_IMPORT void*  AddVectoredExceptionHandler(u32 first, PVECTORED_EXCEPTION_HANDLER handler); // AddVectoredExceptionHandler
WIN32_IMPORT LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);  // SetUnhandledExceptionFilter
WIN32_IMPORT u16    RtlCaptureStackBackTrace(u32 frames_to_skip, u32 frames_to_capture, void** back_trace,
                                             u32* back_trace_hash);                             // RtlCaptureStackBackTrace

// ================================================================ synchronization.lib (api-ms-win-core-synch-l1-2-0)
WIN32_IMPORT i32    WaitOnAddress(volatile void* address, void* compare_address, size_t size, u32 ms);  // WaitOnAddress
WIN32_IMPORT void   WakeByAddressSingle(void* address);                                         // WakeByAddressSingle
WIN32_IMPORT void   WakeByAddressAll(void* address);                                            // WakeByAddressAll

// ================================================================ user32.dll
WIN32_IMPORT i32    SetProcessDpiAwarenessContext(HANDLE context);                              // SetProcessDpiAwarenessContext
WIN32_IMPORT u32    GetDpiForWindow(HWND hwnd);                                                 // GetDpiForWindow
WIN32_IMPORT i32    AdjustWindowRectExForDpi(RECT* rect, u32 style, i32 menu, u32 ex_style, u32 dpi);  // AdjustWindowRectExForDpi
WIN32_IMPORT u16    RegisterClassExW(const WNDCLASSEXW* wc);                                    // RegisterClassExW
WIN32_IMPORT HWND   CreateWindowExW(u32 ex_style, const WCHAR* class_name, const WCHAR* title, u32 style,
                                    i32 x, i32 y, i32 width, i32 height, HWND parent, HMENU menu,
                                    HINSTANCE instance, void* param);                           // CreateWindowExW
WIN32_IMPORT i32    DestroyWindow(HWND hwnd);                                                   // DestroyWindow
WIN32_IMPORT i32    ShowWindow(HWND hwnd, i32 cmd);                                             // ShowWindow
WIN32_IMPORT i32    IsIconic(HWND hwnd);                                                        // IsIconic
WIN32_IMPORT i32    SetWindowTextW(HWND hwnd, const WCHAR* text);                               // SetWindowTextW
WIN32_IMPORT LRESULT DefWindowProcW(HWND hwnd, u32 message, WPARAM wparam, LPARAM lparam);      // DefWindowProcW
WIN32_IMPORT i32    PeekMessageW(MSG* msg, HWND hwnd, u32 filter_min, u32 filter_max, u32 remove); // PeekMessageW
WIN32_IMPORT i32    TranslateMessage(const MSG* msg);                                           // TranslateMessage
WIN32_IMPORT LRESULT DispatchMessageW(const MSG* msg);                                          // DispatchMessageW
WIN32_IMPORT LRESULT SendMessageW(HWND hwnd, u32 message, WPARAM wparam, LPARAM lparam);          // SendMessageW
WIN32_IMPORT i32    PostMessageW(HWND hwnd, u32 message, WPARAM wparam, LPARAM lparam);         // PostMessageW
WIN32_IMPORT void   PostQuitMessage(i32 exit_code);                                             // PostQuitMessage
WIN32_IMPORT i64    SetWindowLongPtrW(HWND hwnd, i32 index, i64 value);                         // SetWindowLongPtrW
WIN32_IMPORT i64    GetWindowLongPtrW(HWND hwnd, i32 index);                                    // GetWindowLongPtrW
WIN32_IMPORT i32    SetWindowPos(HWND hwnd, HWND insert_after, i32 x, i32 y, i32 cx, i32 cy, u32 flags); // SetWindowPos
WIN32_IMPORT i32    GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT* placement);                  // GetWindowPlacement
WIN32_IMPORT i32    SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT* placement);            // SetWindowPlacement
WIN32_IMPORT i32    GetClientRect(HWND hwnd, RECT* rect);                                       // GetClientRect
WIN32_IMPORT i32    GetWindowRect(HWND hwnd, RECT* rect);                                       // GetWindowRect
WIN32_IMPORT i32    ClientToScreen(HWND hwnd, POINT* point);                                    // ClientToScreen
WIN32_IMPORT i32    ScreenToClient(HWND hwnd, POINT* point);                                    // ScreenToClient
WIN32_IMPORT HMONITOR MonitorFromWindow(HWND hwnd, u32 flags);                                  // MonitorFromWindow
WIN32_IMPORT i32    GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);                       // GetMonitorInfoW
WIN32_IMPORT HCURSOR LoadCursorW(HINSTANCE instance, const WCHAR* name);                        // LoadCursorW
WIN32_IMPORT HCURSOR SetCursor(HCURSOR cursor);                                                 // SetCursor
WIN32_IMPORT i32    ShowCursor(i32 show);                                                       // ShowCursor
WIN32_IMPORT i32    ClipCursor(const RECT* rect);                                               // ClipCursor
WIN32_IMPORT i32    GetCursorPos(POINT* point);                                                 // GetCursorPos
WIN32_IMPORT HWND   SetCapture(HWND hwnd);                                                      // SetCapture
WIN32_IMPORT i32    ReleaseCapture(void);                                                       // ReleaseCapture
WIN32_IMPORT u32    MapVirtualKeyW(u32 code, u32 map_type);                                     // MapVirtualKeyW
WIN32_IMPORT i32    RegisterRawInputDevices(const RAWINPUTDEVICE* devices, u32 count, u32 size); // RegisterRawInputDevices
WIN32_IMPORT u32    GetRawInputData(HRAWINPUT raw, u32 command, void* data, u32* size, u32 header_size);  // GetRawInputData
WIN32_IMPORT i32    EnableMouseInPointer(i32 enable);                                           // EnableMouseInPointer
WIN32_IMPORT i32    GetPointerType(u32 pointer_id, u32* pointer_type);                          // GetPointerType
WIN32_IMPORT i32    GetPointerInfo(u32 pointer_id, POINTER_INFO* info);                         // GetPointerInfo
WIN32_IMPORT i32    GetPointerPenInfo(u32 pointer_id, POINTER_PEN_INFO* info);                  // GetPointerPenInfo

// ================================================================ ole32.dll, avrt.dll
WIN32_IMPORT HRESULT CoInitializeEx(void* reserved, u32 coinit);                                // CoInitializeEx
WIN32_IMPORT void    CoUninitialize(void);                                                      // CoUninitialize
WIN32_IMPORT HRESULT CoCreateInstance(const GUID* clsid, void* outer, u32 cls_context, const GUID* iid,
                                      void** out);                                              // CoCreateInstance
WIN32_IMPORT void    CoTaskMemFree(void* memory);                                               // CoTaskMemFree
WIN32_IMPORT HANDLE  AvSetMmThreadCharacteristicsW(const WCHAR* task_name, u32* task_index);    // AvSetMmThreadCharacteristicsW
WIN32_IMPORT i32     AvRevertMmThreadCharacteristics(HANDLE avrt_handle);                       // AvRevertMmThreadCharacteristics

// ================================================================ COM interfaces
// Calls go through the vtable: device->lpVtbl->CreateBuffer(device, &desc, 0, &buffer).
typedef struct IUnknownVtbl {                      // IUnknown (self is untyped: shared by many interfaces)
    HRESULT (*QueryInterface)(void* self, const GUID* iid, void** out);   // 0
    u32     (*AddRef)(void* self);                                        // 1
    u32     (*Release)(void* self);                                       // 2
} IUnknownVtbl;

// Interfaces with only IUnknown calls.
typedef struct IUnknown                 { const IUnknownVtbl* lpVtbl; } IUnknown;
typedef struct ID3D11Resource           { const IUnknownVtbl* lpVtbl; } ID3D11Resource;      // base of buffers/textures
typedef struct ID3D11Buffer             { const IUnknownVtbl* lpVtbl; } ID3D11Buffer;
typedef struct ID3D11ShaderResourceView { const IUnknownVtbl* lpVtbl; } ID3D11ShaderResourceView;
typedef struct ID3D11RenderTargetView   { const IUnknownVtbl* lpVtbl; } ID3D11RenderTargetView;
typedef struct ID3D11DepthStencilView   { const IUnknownVtbl* lpVtbl; } ID3D11DepthStencilView;
typedef struct ID3D11InputLayout        { const IUnknownVtbl* lpVtbl; } ID3D11InputLayout;
typedef struct ID3D11VertexShader       { const IUnknownVtbl* lpVtbl; } ID3D11VertexShader;
typedef struct ID3D11PixelShader        { const IUnknownVtbl* lpVtbl; } ID3D11PixelShader;
typedef struct ID3D11BlendState         { const IUnknownVtbl* lpVtbl; } ID3D11BlendState;
typedef struct ID3D11RasterizerState    { const IUnknownVtbl* lpVtbl; } ID3D11RasterizerState;
typedef struct ID3D11DepthStencilState  { const IUnknownVtbl* lpVtbl; } ID3D11DepthStencilState;
typedef struct ID3D11SamplerState       { const IUnknownVtbl* lpVtbl; } ID3D11SamplerState;
typedef struct IDXGISwapChain1          { const IUnknownVtbl* lpVtbl; } IDXGISwapChain1;     // QueryInterface to IDXGISwapChain2

typedef struct ID3D11Device ID3D11Device;
typedef struct ID3D11DeviceContext ID3D11DeviceContext;
typedef struct ID3D11Texture2D ID3D11Texture2D;
typedef struct ID3D11Debug ID3D11Debug;
typedef struct ID3D11InfoQueue ID3D11InfoQueue;
typedef struct IDXGIDevice IDXGIDevice;
typedef struct IDXGIAdapter IDXGIAdapter;
typedef struct IDXGIFactory2 IDXGIFactory2;
typedef struct IDXGIFactory5 IDXGIFactory5;
typedef struct IDXGISwapChain2 IDXGISwapChain2;
typedef struct ID3DBlob ID3DBlob;
typedef struct IMMDeviceEnumerator IMMDeviceEnumerator;
typedef struct IMMDevice IMMDevice;
typedef struct IAudioClient IAudioClient;
typedef struct IAudioRenderClient IAudioRenderClient;

// ---------------------------------------------------------------- D3D11 (d3d11.h, d3d11sdklayers.h)
typedef struct ID3D11DeviceVtbl {                  // ID3D11Device
    HRESULT (*QueryInterface)(ID3D11Device* self, const GUID* iid, void** out);                              // 0
    u32     (*AddRef)(ID3D11Device* self);                                                                   // 1
    u32     (*Release)(ID3D11Device* self);                                                                  // 2
    HRESULT (*CreateBuffer)(ID3D11Device* self, const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial,
                            ID3D11Buffer** out);                                                             // 3
    void* _4_CreateTexture1D;
    HRESULT (*CreateTexture2D)(ID3D11Device* self, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial,
                               ID3D11Texture2D** out);                                                       // 5
    void* _6_CreateTexture3D;
    HRESULT (*CreateShaderResourceView)(ID3D11Device* self, ID3D11Resource* resource,
                                        const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, ID3D11ShaderResourceView** out); // 7
    void* _8_CreateUnorderedAccessView;
    HRESULT (*CreateRenderTargetView)(ID3D11Device* self, ID3D11Resource* resource,
                                      const D3D11_RENDER_TARGET_VIEW_DESC* desc, ID3D11RenderTargetView** out);      // 9
    HRESULT (*CreateDepthStencilView)(ID3D11Device* self, ID3D11Resource* resource,
                                      const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView** out);      // 10
    HRESULT (*CreateInputLayout)(ID3D11Device* self, const D3D11_INPUT_ELEMENT_DESC* elements, u32 element_count,
                                 const void* vs_bytecode, size_t vs_bytecode_length, ID3D11InputLayout** out); // 11
    HRESULT (*CreateVertexShader)(ID3D11Device* self, const void* bytecode, size_t bytecode_length,
                                  void* class_linkage, ID3D11VertexShader** out);                            // 12
    void* _13_CreateGeometryShader;
    void* _14_CreateGeometryShaderWithStreamOutput;
    HRESULT (*CreatePixelShader)(ID3D11Device* self, const void* bytecode, size_t bytecode_length,
                                 void* class_linkage, ID3D11PixelShader** out);                              // 15
    void* _16_CreateHullShader;
    void* _17_CreateDomainShader;
    void* _18_CreateComputeShader;
    void* _19_CreateClassLinkage;
    HRESULT (*CreateBlendState)(ID3D11Device* self, const D3D11_BLEND_DESC* desc, ID3D11BlendState** out);  // 20
    HRESULT (*CreateDepthStencilState)(ID3D11Device* self, const D3D11_DEPTH_STENCIL_DESC* desc,
                                       ID3D11DepthStencilState** out);                                       // 21
    HRESULT (*CreateRasterizerState)(ID3D11Device* self, const D3D11_RASTERIZER_DESC* desc,
                                     ID3D11RasterizerState** out);                                           // 22
    HRESULT (*CreateSamplerState)(ID3D11Device* self, const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** out); // 23
    void* _24_CreateQuery;
    void* _25_CreatePredicate;
    void* _26_CreateCounter;
    void* _27_CreateDeferredContext;
    void* _28_OpenSharedResource;
    void* _29_CheckFormatSupport;
    void* _30_CheckMultisampleQualityLevels;
    void* _31_CheckCounterInfo;
    void* _32_CheckCounter;
    void* _33_CheckFeatureSupport;
    void* _34_GetPrivateData;
    void* _35_SetPrivateData;
    void* _36_SetPrivateDataInterface;
    u32     (*GetFeatureLevel)(ID3D11Device* self);                                                          // 37
    void* _38_GetCreationFlags;
    HRESULT (*GetDeviceRemovedReason)(ID3D11Device* self);                                                   // 39
} ID3D11DeviceVtbl;
struct ID3D11Device { const ID3D11DeviceVtbl* lpVtbl; };

typedef struct ID3D11DeviceContextVtbl {           // ID3D11DeviceContext
    HRESULT (*QueryInterface)(ID3D11DeviceContext* self, const GUID* iid, void** out);                       // 0
    u32     (*AddRef)(ID3D11DeviceContext* self);                                                            // 1
    u32     (*Release)(ID3D11DeviceContext* self);                                                           // 2
    void* _3_GetDevice;
    void* _4_GetPrivateData;
    void* _5_SetPrivateData;
    void* _6_SetPrivateDataInterface;
    void    (*VSSetConstantBuffers)(ID3D11DeviceContext* self, u32 start_slot, u32 count, ID3D11Buffer* const* buffers);   // 7
    void    (*PSSetShaderResources)(ID3D11DeviceContext* self, u32 start_slot, u32 count,
                                    ID3D11ShaderResourceView* const* views);                                 // 8
    void    (*PSSetShader)(ID3D11DeviceContext* self, ID3D11PixelShader* shader, void* const* class_instances,
                           u32 class_instance_count);                                                        // 9
    void    (*PSSetSamplers)(ID3D11DeviceContext* self, u32 start_slot, u32 count, ID3D11SamplerState* const* samplers); // 10
    void    (*VSSetShader)(ID3D11DeviceContext* self, ID3D11VertexShader* shader, void* const* class_instances,
                           u32 class_instance_count);                                                        // 11
    void    (*DrawIndexed)(ID3D11DeviceContext* self, u32 index_count, u32 start_index, i32 base_vertex);   // 12
    void    (*Draw)(ID3D11DeviceContext* self, u32 vertex_count, u32 start_vertex);                         // 13
    HRESULT (*Map)(ID3D11DeviceContext* self, ID3D11Resource* resource, u32 subresource, u32 map_type, u32 map_flags,
                   D3D11_MAPPED_SUBRESOURCE* mapped);                                                        // 14
    void    (*Unmap)(ID3D11DeviceContext* self, ID3D11Resource* resource, u32 subresource);                 // 15
    void    (*PSSetConstantBuffers)(ID3D11DeviceContext* self, u32 start_slot, u32 count, ID3D11Buffer* const* buffers);   // 16
    void    (*IASetInputLayout)(ID3D11DeviceContext* self, ID3D11InputLayout* layout);                      // 17
    void    (*IASetVertexBuffers)(ID3D11DeviceContext* self, u32 start_slot, u32 count, ID3D11Buffer* const* buffers,
                                  const u32* strides, const u32* offsets);                                   // 18
    void    (*IASetIndexBuffer)(ID3D11DeviceContext* self, ID3D11Buffer* buffer, u32 format, u32 offset);   // 19
    void    (*DrawIndexedInstanced)(ID3D11DeviceContext* self, u32 index_count_per_instance, u32 instance_count,
                                    u32 start_index, i32 base_vertex, u32 start_instance);                   // 20
    void    (*DrawInstanced)(ID3D11DeviceContext* self, u32 vertex_count_per_instance, u32 instance_count,
                             u32 start_vertex, u32 start_instance);                                          // 21
    void* _22_GSSetConstantBuffers;
    void* _23_GSSetShader;
    void    (*IASetPrimitiveTopology)(ID3D11DeviceContext* self, u32 topology);                             // 24
    void    (*VSSetShaderResources)(ID3D11DeviceContext* self, u32 start_slot, u32 count,
                                    ID3D11ShaderResourceView* const* views);                                 // 25
    void    (*VSSetSamplers)(ID3D11DeviceContext* self, u32 start_slot, u32 count, ID3D11SamplerState* const* samplers); // 26
    void* _27_Begin;
    void* _28_End;
    void* _29_GetData;
    void* _30_SetPredication;
    void* _31_GSSetShaderResources;
    void* _32_GSSetSamplers;
    void    (*OMSetRenderTargets)(ID3D11DeviceContext* self, u32 count, ID3D11RenderTargetView* const* views,
                                  ID3D11DepthStencilView* depth_view);                                       // 33
    void* _34_OMSetRenderTargetsAndUnorderedAccessViews;
    void    (*OMSetBlendState)(ID3D11DeviceContext* self, ID3D11BlendState* state, const f32 blend_factor[4],
                               u32 sample_mask);                                                             // 35
    void    (*OMSetDepthStencilState)(ID3D11DeviceContext* self, ID3D11DepthStencilState* state, u32 stencil_ref);  // 36
    void* _37_SOSetTargets;
    void* _38_DrawAuto;
    void* _39_DrawIndexedInstancedIndirect;
    void* _40_DrawInstancedIndirect;
    void* _41_Dispatch;
    void* _42_DispatchIndirect;
    void    (*RSSetState)(ID3D11DeviceContext* self, ID3D11RasterizerState* state);                         // 43
    void    (*RSSetViewports)(ID3D11DeviceContext* self, u32 count, const D3D11_VIEWPORT* viewports);       // 44
    void    (*RSSetScissorRects)(ID3D11DeviceContext* self, u32 count, const RECT* rects);                  // 45
    void    (*CopySubresourceRegion)(ID3D11DeviceContext* self, ID3D11Resource* dst, u32 dst_subresource,
                                     u32 dst_x, u32 dst_y, u32 dst_z, ID3D11Resource* src, u32 src_subresource,
                                     const D3D11_BOX* src_box);                                              // 46
    void    (*CopyResource)(ID3D11DeviceContext* self, ID3D11Resource* dst, ID3D11Resource* src);           // 47
    void    (*UpdateSubresource)(ID3D11DeviceContext* self, ID3D11Resource* dst, u32 dst_subresource,
                                 const D3D11_BOX* dst_box, const void* src_data, u32 src_row_pitch,
                                 u32 src_depth_pitch);                                                       // 48
    void* _49_CopyStructureCount;
    void    (*ClearRenderTargetView)(ID3D11DeviceContext* self, ID3D11RenderTargetView* view, const f32 color[4]);  // 50
    void* _51_ClearUnorderedAccessViewUint;
    void* _52_ClearUnorderedAccessViewFloat;
    void    (*ClearDepthStencilView)(ID3D11DeviceContext* self, ID3D11DepthStencilView* view, u32 clear_flags,
                                     f32 depth, u8 stencil);                                                 // 53
    void* _54_GenerateMips;
    void* _55_SetResourceMinLOD;
    void* _56_GetResourceMinLOD;
    void* _57_ResolveSubresource;
    void* _58_ExecuteCommandList;
    void* _59_HSSetShaderResources;
    void* _60_HSSetShader;
    void* _61_HSSetSamplers;
    void* _62_HSSetConstantBuffers;
    void* _63_DSSetShaderResources;
    void* _64_DSSetShader;
    void* _65_DSSetSamplers;
    void* _66_DSSetConstantBuffers;
    void* _67_CSSetShaderResources;
    void* _68_CSSetUnorderedAccessViews;
    void* _69_CSSetShader;
    void* _70_CSSetSamplers;
    void* _71_CSSetConstantBuffers;
    void* _72_VSGetConstantBuffers;
    void* _73_PSGetShaderResources;
    void* _74_PSGetShader;
    void* _75_PSGetSamplers;
    void* _76_VSGetShader;
    void* _77_PSGetConstantBuffers;
    void* _78_IAGetInputLayout;
    void* _79_IAGetVertexBuffers;
    void* _80_IAGetIndexBuffer;
    void* _81_GSGetConstantBuffers;
    void* _82_GSGetShader;
    void* _83_IAGetPrimitiveTopology;
    void* _84_VSGetShaderResources;
    void* _85_VSGetSamplers;
    void* _86_GetPredication;
    void* _87_GSGetShaderResources;
    void* _88_GSGetSamplers;
    void* _89_OMGetRenderTargets;
    void* _90_OMGetRenderTargetsAndUnorderedAccessViews;
    void* _91_OMGetBlendState;
    void* _92_OMGetDepthStencilState;
    void* _93_SOGetTargets;
    void* _94_RSGetState;
    void* _95_RSGetViewports;
    void* _96_RSGetScissorRects;
    void* _97_HSGetShaderResources;
    void* _98_HSGetShader;
    void* _99_HSGetSamplers;
    void* _100_HSGetConstantBuffers;
    void* _101_DSGetShaderResources;
    void* _102_DSGetShader;
    void* _103_DSGetSamplers;
    void* _104_DSGetConstantBuffers;
    void* _105_CSGetShaderResources;
    void* _106_CSGetUnorderedAccessViews;
    void* _107_CSGetShader;
    void* _108_CSGetSamplers;
    void* _109_CSGetConstantBuffers;
    void    (*ClearState)(ID3D11DeviceContext* self);                                                        // 110
    void    (*Flush)(ID3D11DeviceContext* self);                                                             // 111
} ID3D11DeviceContextVtbl;
struct ID3D11DeviceContext { const ID3D11DeviceContextVtbl* lpVtbl; };

typedef struct ID3D11Texture2DVtbl {               // ID3D11Texture2D
    HRESULT (*QueryInterface)(ID3D11Texture2D* self, const GUID* iid, void** out);                           // 0
    u32     (*AddRef)(ID3D11Texture2D* self);                                                                // 1
    u32     (*Release)(ID3D11Texture2D* self);                                                               // 2
    void* _3_GetDevice;
    void* _4_GetPrivateData;
    void* _5_SetPrivateData;
    void* _6_SetPrivateDataInterface;
    void* _7_GetType;
    void* _8_SetEvictionPriority;
    void* _9_GetEvictionPriority;
    void    (*GetDesc)(ID3D11Texture2D* self, D3D11_TEXTURE2D_DESC* desc);                                   // 10
} ID3D11Texture2DVtbl;
struct ID3D11Texture2D { const ID3D11Texture2DVtbl* lpVtbl; };

typedef struct ID3D11DebugVtbl {                   // ID3D11Debug (debug layer only)
    HRESULT (*QueryInterface)(ID3D11Debug* self, const GUID* iid, void** out);                               // 0
    u32     (*AddRef)(ID3D11Debug* self);                                                                    // 1
    u32     (*Release)(ID3D11Debug* self);                                                                   // 2
    void* _3_SetFeatureMask;
    void* _4_GetFeatureMask;
    void* _5_SetPresentPerRenderOpDelay;
    void* _6_GetPresentPerRenderOpDelay;
    void* _7_SetSwapChain;
    void* _8_GetSwapChain;
    void* _9_ValidateContext;
    HRESULT (*ReportLiveDeviceObjects)(ID3D11Debug* self, u32 flags);                                        // 10
} ID3D11DebugVtbl;
struct ID3D11Debug { const ID3D11DebugVtbl* lpVtbl; };

typedef struct ID3D11InfoQueueVtbl {               // ID3D11InfoQueue (debug layer only)
    HRESULT (*QueryInterface)(ID3D11InfoQueue* self, const GUID* iid, void** out);                           // 0
    u32     (*AddRef)(ID3D11InfoQueue* self);                                                                // 1
    u32     (*Release)(ID3D11InfoQueue* self);                                                               // 2
    HRESULT (*SetMessageCountLimit)(ID3D11InfoQueue* self, u64 limit);                                       // 3
    void    (*ClearStoredMessages)(ID3D11InfoQueue* self);                                                   // 4
    HRESULT (*GetMessage)(ID3D11InfoQueue* self, u64 index, D3D11_MESSAGE* message, size_t* message_length);  // 5
    void* _6_GetNumMessagesAllowedByStorageFilter;
    void* _7_GetNumMessagesDeniedByStorageFilter;
    u64     (*GetNumStoredMessages)(ID3D11InfoQueue* self);                                                  // 8
} ID3D11InfoQueueVtbl;
struct ID3D11InfoQueue { const ID3D11InfoQueueVtbl* lpVtbl; };

// ---------------------------------------------------------------- DXGI (dxgi.h, dxgi1_2.h, dxgi1_3.h, dxgi1_5.h)
typedef struct IDXGIDeviceVtbl {                   // IDXGIDevice
    HRESULT (*QueryInterface)(IDXGIDevice* self, const GUID* iid, void** out);                               // 0
    u32     (*AddRef)(IDXGIDevice* self);                                                                    // 1
    u32     (*Release)(IDXGIDevice* self);                                                                   // 2
    void* _3_SetPrivateData;
    void* _4_SetPrivateDataInterface;
    void* _5_GetPrivateData;
    void* _6_GetParent;
    HRESULT (*GetAdapter)(IDXGIDevice* self, IDXGIAdapter** adapter);                                        // 7
} IDXGIDeviceVtbl;
struct IDXGIDevice { const IDXGIDeviceVtbl* lpVtbl; };

typedef struct IDXGIAdapterVtbl {                  // IDXGIAdapter
    HRESULT (*QueryInterface)(IDXGIAdapter* self, const GUID* iid, void** out);                              // 0
    u32     (*AddRef)(IDXGIAdapter* self);                                                                   // 1
    u32     (*Release)(IDXGIAdapter* self);                                                                  // 2
    void* _3_SetPrivateData;
    void* _4_SetPrivateDataInterface;
    void* _5_GetPrivateData;
    HRESULT (*GetParent)(IDXGIAdapter* self, const GUID* iid, void** parent);                                // 6
    void* _7_EnumOutputs;
    HRESULT (*GetDesc)(IDXGIAdapter* self, DXGI_ADAPTER_DESC* desc);                                         // 8
} IDXGIAdapterVtbl;
struct IDXGIAdapter { const IDXGIAdapterVtbl* lpVtbl; };

typedef struct IDXGIFactory2Vtbl {                 // IDXGIFactory2
    HRESULT (*QueryInterface)(IDXGIFactory2* self, const GUID* iid, void** out);                             // 0
    u32     (*AddRef)(IDXGIFactory2* self);                                                                  // 1
    u32     (*Release)(IDXGIFactory2* self);                                                                 // 2
    void* _3_SetPrivateData;
    void* _4_SetPrivateDataInterface;
    void* _5_GetPrivateData;
    void* _6_GetParent;
    void* _7_EnumAdapters;
    HRESULT (*MakeWindowAssociation)(IDXGIFactory2* self, HWND hwnd, u32 flags);                             // 8
    void* _9_GetWindowAssociation;
    void* _10_CreateSwapChain;
    void* _11_CreateSoftwareAdapter;
    void* _12_EnumAdapters1;
    void* _13_IsCurrent;
    void* _14_IsWindowedStereoEnabled;
    HRESULT (*CreateSwapChainForHwnd)(IDXGIFactory2* self, void* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
                                      const void* fullscreen_desc, void* restrict_to_output,
                                      IDXGISwapChain1** swapchain);                                          // 15
} IDXGIFactory2Vtbl;
struct IDXGIFactory2 { const IDXGIFactory2Vtbl* lpVtbl; };

typedef struct IDXGIFactory5Vtbl {                 // IDXGIFactory5 (only for the tearing check)
    HRESULT (*QueryInterface)(IDXGIFactory5* self, const GUID* iid, void** out);                             // 0
    u32     (*AddRef)(IDXGIFactory5* self);                                                                  // 1
    u32     (*Release)(IDXGIFactory5* self);                                                                 // 2
    void* _3_SetPrivateData;
    void* _4_SetPrivateDataInterface;
    void* _5_GetPrivateData;
    void* _6_GetParent;
    void* _7_EnumAdapters;
    void* _8_MakeWindowAssociation;
    void* _9_GetWindowAssociation;
    void* _10_CreateSwapChain;
    void* _11_CreateSoftwareAdapter;
    void* _12_EnumAdapters1;
    void* _13_IsCurrent;
    void* _14_IsWindowedStereoEnabled;
    void* _15_CreateSwapChainForHwnd;
    void* _16_CreateSwapChainForCoreWindow;
    void* _17_GetSharedResourceAdapterLuid;
    void* _18_RegisterStereoStatusWindow;
    void* _19_RegisterStereoStatusEvent;
    void* _20_UnregisterStereoStatus;
    void* _21_RegisterOcclusionStatusWindow;
    void* _22_RegisterOcclusionStatusEvent;
    void* _23_UnregisterOcclusionStatus;
    void* _24_CreateSwapChainForComposition;
    void* _25_GetCreationFlags;
    void* _26_EnumAdapterByLuid;
    void* _27_EnumWarpAdapter;
    HRESULT (*CheckFeatureSupport)(IDXGIFactory5* self, u32 feature, void* support_data, u32 support_data_size); // 28
} IDXGIFactory5Vtbl;
struct IDXGIFactory5 { const IDXGIFactory5Vtbl* lpVtbl; };

typedef struct IDXGISwapChain2Vtbl {               // IDXGISwapChain2
    HRESULT (*QueryInterface)(IDXGISwapChain2* self, const GUID* iid, void** out);                           // 0
    u32     (*AddRef)(IDXGISwapChain2* self);                                                                // 1
    u32     (*Release)(IDXGISwapChain2* self);                                                               // 2
    void* _3_SetPrivateData;
    void* _4_SetPrivateDataInterface;
    void* _5_GetPrivateData;
    void* _6_GetParent;
    void* _7_GetDevice;
    HRESULT (*Present)(IDXGISwapChain2* self, u32 sync_interval, u32 flags);                                 // 8
    HRESULT (*GetBuffer)(IDXGISwapChain2* self, u32 buffer, const GUID* iid, void** surface);                // 9
    void* _10_SetFullscreenState;
    void* _11_GetFullscreenState;
    void* _12_GetDesc;
    HRESULT (*ResizeBuffers)(IDXGISwapChain2* self, u32 buffer_count, u32 width, u32 height, u32 format,
                             u32 swapchain_flags);                                                           // 13
    void* _14_ResizeTarget;
    void* _15_GetContainingOutput;
    void* _16_GetFrameStatistics;
    void* _17_GetLastPresentCount;
    void* _18_GetDesc1;
    void* _19_GetFullscreenDesc;
    void* _20_GetHwnd;
    void* _21_GetCoreWindow;
    void* _22_Present1;
    void* _23_IsTemporaryMonoSupported;
    void* _24_GetRestrictToOutput;
    void* _25_SetBackgroundColor;
    void* _26_GetBackgroundColor;
    void* _27_SetRotation;
    void* _28_GetRotation;
    void* _29_SetSourceSize;
    void* _30_GetSourceSize;
    HRESULT (*SetMaximumFrameLatency)(IDXGISwapChain2* self, u32 max_latency);                               // 31
    void* _32_GetMaximumFrameLatency;
    HANDLE  (*GetFrameLatencyWaitableObject)(IDXGISwapChain2* self);                                         // 33
} IDXGISwapChain2Vtbl;
struct IDXGISwapChain2 { const IDXGISwapChain2Vtbl* lpVtbl; };

// ---------------------------------------------------------------- D3DCompile output (d3dcommon.h ID3D10Blob)
typedef struct ID3DBlobVtbl {                      // ID3DBlob (ID3D10Blob)
    HRESULT (*QueryInterface)(ID3DBlob* self, const GUID* iid, void** out);                                  // 0
    u32     (*AddRef)(ID3DBlob* self);                                                                       // 1
    u32     (*Release)(ID3DBlob* self);                                                                      // 2
    void*   (*GetBufferPointer)(ID3DBlob* self);                                                             // 3
    size_t  (*GetBufferSize)(ID3DBlob* self);                                                                // 4
} ID3DBlobVtbl;
struct ID3DBlob { const ID3DBlobVtbl* lpVtbl; };

// ---------------------------------------------------------------- WASAPI (mmdeviceapi.h, audioclient.h)
typedef struct IMMDeviceEnumeratorVtbl {           // IMMDeviceEnumerator
    HRESULT (*QueryInterface)(IMMDeviceEnumerator* self, const GUID* iid, void** out);                       // 0
    u32     (*AddRef)(IMMDeviceEnumerator* self);                                                            // 1
    u32     (*Release)(IMMDeviceEnumerator* self);                                                           // 2
    void* _3_EnumAudioEndpoints;
    HRESULT (*GetDefaultAudioEndpoint)(IMMDeviceEnumerator* self, u32 data_flow, u32 role, IMMDevice** endpoint); // 4
} IMMDeviceEnumeratorVtbl;
struct IMMDeviceEnumerator { const IMMDeviceEnumeratorVtbl* lpVtbl; };

typedef struct IMMDeviceVtbl {                     // IMMDevice
    HRESULT (*QueryInterface)(IMMDevice* self, const GUID* iid, void** out);                                 // 0
    u32     (*AddRef)(IMMDevice* self);                                                                      // 1
    u32     (*Release)(IMMDevice* self);                                                                     // 2
    HRESULT (*Activate)(IMMDevice* self, const GUID* iid, u32 cls_context, void* activation_params, void** out); // 3
} IMMDeviceVtbl;
struct IMMDevice { const IMMDeviceVtbl* lpVtbl; };

typedef struct IAudioClientVtbl {                  // IAudioClient (REFERENCE_TIME is i64 in 100 ns units)
    HRESULT (*QueryInterface)(IAudioClient* self, const GUID* iid, void** out);                              // 0
    u32     (*AddRef)(IAudioClient* self);                                                                   // 1
    u32     (*Release)(IAudioClient* self);                                                                  // 2
    HRESULT (*Initialize)(IAudioClient* self, u32 share_mode, u32 stream_flags, i64 buffer_duration,
                          i64 periodicity, const WAVEFORMATEX* format, const GUID* audio_session);           // 3
    HRESULT (*GetBufferSize)(IAudioClient* self, u32* buffer_frames);                                        // 4
    HRESULT (*GetStreamLatency)(IAudioClient* self, i64* latency);                                           // 5
    HRESULT (*GetCurrentPadding)(IAudioClient* self, u32* padding_frames);                                   // 6
    HRESULT (*IsFormatSupported)(IAudioClient* self, u32 share_mode, const WAVEFORMATEX* format,
                                 WAVEFORMATEX** closest_match);                                              // 7
    HRESULT (*GetMixFormat)(IAudioClient* self, WAVEFORMATEX** format);                                      // 8
    HRESULT (*GetDevicePeriod)(IAudioClient* self, i64* default_period, i64* minimum_period);                // 9
    HRESULT (*Start)(IAudioClient* self);                                                                    // 10
    HRESULT (*Stop)(IAudioClient* self);                                                                     // 11
    HRESULT (*Reset)(IAudioClient* self);                                                                    // 12
    HRESULT (*SetEventHandle)(IAudioClient* self, HANDLE event);                                             // 13
    HRESULT (*GetService)(IAudioClient* self, const GUID* iid, void** out);                                  // 14
} IAudioClientVtbl;
struct IAudioClient { const IAudioClientVtbl* lpVtbl; };

typedef struct IAudioRenderClientVtbl {            // IAudioRenderClient
    HRESULT (*QueryInterface)(IAudioRenderClient* self, const GUID* iid, void** out);                        // 0
    u32     (*AddRef)(IAudioRenderClient* self);                                                             // 1
    u32     (*Release)(IAudioRenderClient* self);                                                            // 2
    HRESULT (*GetBuffer)(IAudioRenderClient* self, u32 frames_requested, u8** data);                         // 3
    HRESULT (*ReleaseBuffer)(IAudioRenderClient* self, u32 frames_written, u32 flags);                       // 4
} IAudioRenderClientVtbl;
struct IAudioRenderClient { const IAudioRenderClientVtbl* lpVtbl; };

// ---------------------------------------------------------------- interface and class IDs
static const GUID WIN32_IID_ID3D11Texture2D     = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static const GUID WIN32_IID_ID3D11Debug         = { 0x79cf2233, 0x7536, 0x4948, { 0x9d, 0x36, 0x1e, 0x46, 0x92, 0xdc, 0x57, 0x60 } };
static const GUID WIN32_IID_ID3D11InfoQueue     = { 0x6543dbb6, 0x1b48, 0x42f5, { 0xab, 0x82, 0xe9, 0x7e, 0xc7, 0x43, 0x26, 0xf6 } };
static const GUID WIN32_IID_IDXGIDevice         = { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
static const GUID WIN32_IID_IDXGIFactory2       = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
static const GUID WIN32_IID_IDXGIFactory5       = { 0x7632e1f5, 0xee65, 0x4dca, { 0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d } };
static const GUID WIN32_IID_IDXGISwapChain2     = { 0xa8be2ac4, 0x199f, 0x4946, { 0xb3, 0x31, 0x79, 0x59, 0x9f, 0xb9, 0x8d, 0xe7 } };
static const GUID WIN32_CLSID_MMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const GUID WIN32_IID_IMMDeviceEnumerator = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const GUID WIN32_IID_IAudioClient        = { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
static const GUID WIN32_IID_IAudioRenderClient  = { 0xf294acfc, 0x3146, 0x4483, { 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2 } };
static const GUID WIN32_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

// ================================================================ runtime-loaded DLL entry points (GetProcAddress)
// d3d11.dll "D3D11CreateDevice"
typedef HRESULT (*D3D11CreateDevice_fn)(IDXGIAdapter* adapter, u32 driver_type, HMODULE software, u32 flags,
                                        const u32* feature_levels, u32 feature_level_count, u32 sdk_version,
                                        ID3D11Device** device, u32* feature_level, ID3D11DeviceContext** context);
// dxgi.dll "CreateDXGIFactory2"
typedef HRESULT (*CreateDXGIFactory2_fn)(u32 flags, const GUID* iid, void** factory);
// xinput1_4.dll "XInputGetState", "XInputSetState" (return a System Error Code, not an HRESULT)
typedef u32 (*XInputGetState_fn)(u32 user_index, XINPUT_STATE* state);
typedef u32 (*XInputSetState_fn)(u32 user_index, XINPUT_VIBRATION* vibration);
// d3dcompiler_47.dll "D3DCompile" (build time only: build.c, task 1A-15)
typedef HRESULT (*D3DCompile_fn)(const void* src_data, size_t src_data_size, const char* source_name,
                                 const void* defines, void* include, const char* entry_point, const char* target,
                                 u32 flags1, u32 flags2, ID3DBlob** code, ID3DBlob** error_messages);

#endif // BMF_WIN32_API_H
