// test_vtable_slots.c: host check that the hand-written COM vtables, struct sizes and GUIDs in
// src/platform/win32/win32_api.h match the Windows SDK headers (task 1A-14, risk K-09). Not part
// of the runtime or the unit-test runner: `build test` compiles this file twice and links both
// objects into out/test/win64-debug/vtable_slots.exe, then runs it before the unit tests.
//   -DVTABLE_SIDE_SDK  includes the SDK headers (needs the MSVC C headers, amendment A-01) and
//                      defines vtable_sdk_* tables. Data only: no code, no CRT.
//   (default)          includes win32_api.h, defines the same tables from our declarations, and
//                      the entry point that compares them and prints JSON lines.
// SDK and win32_api.h names collide (HANDLE, RECT, ...), hence two translation units.
// The value lists below are compiled on both sides from the same text, so the n-th entry means
// the same thing on both. Keep VTABLE_NAMES in the same order (the counts are asserted).
// Rule: every typed method added to a vtable in win32_api.h is added here (regenerate the lists:
// one entry per `(*Method)` member, in declaration order). WASAPI GUIDs are not checked here (C
// mode has no linkable definition for them); 1A-10 compared them once by hand.

#include <stddef.h>

#if defined(VTABLE_SIDE_SDK)
#define UNICODE 1
#define INITGUID 1
#include <windows.h>
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <dxgi1_5.h>
#include <d3dcommon.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <xinput.h>
typedef ID3D10BlobVtbl ID3DBlobVtbl;                 // d3dcommon.h names the blob ID3D10Blob
#define CONTEXT_VTBL ID3D11DeviceContext1Vtbl        // our context vtable includes the ...1 slots
#define VTABLE_VALUES vtable_sdk_values
#define VTABLE_SIZES vtable_sdk_sizes
#else
#include "../src/bmf.h"
#include "../src/base/base_builtins.c"
#include "../src/platform/win32/win32_api.h"
#define CONTEXT_VTBL ID3D11DeviceContextVtbl
#define VTABLE_VALUES vtable_bmf_values
#define VTABLE_SIZES vtable_bmf_sizes
#endif

// Slot index of every typed vtable member.
const unsigned int VTABLE_VALUES[] = {
    (unsigned int)(offsetof(IUnknownVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IUnknownVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IUnknownVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateBuffer) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateTexture2D) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateShaderResourceView) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateRenderTargetView) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateDepthStencilView) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateInputLayout) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateVertexShader) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreatePixelShader) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateBlendState) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateDepthStencilState) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateRasterizerState) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CreateSamplerState) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, CheckFeatureSupport) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, GetFeatureLevel) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DeviceVtbl, GetDeviceRemovedReason) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, Release) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, VSSetConstantBuffers) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, PSSetShaderResources) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, PSSetShader) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, PSSetSamplers) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, VSSetShader) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, DrawIndexed) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, Draw) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, Map) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, Unmap) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, PSSetConstantBuffers) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, IASetInputLayout) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, IASetVertexBuffers) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, IASetIndexBuffer) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, DrawIndexedInstanced) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, DrawInstanced) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, IASetPrimitiveTopology) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, VSSetShaderResources) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, VSSetSamplers) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, OMSetRenderTargets) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, OMSetBlendState) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, OMSetDepthStencilState) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, RSSetState) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, RSSetViewports) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, RSSetScissorRects) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, CopySubresourceRegion) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, CopyResource) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, UpdateSubresource) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, ClearRenderTargetView) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, ClearDepthStencilView) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, ClearState) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, Flush) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, UpdateSubresource1) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, VSSetConstantBuffers1) / sizeof(void*)),
    (unsigned int)(offsetof(CONTEXT_VTBL, PSSetConstantBuffers1) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11Texture2DVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11Texture2DVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11Texture2DVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11Texture2DVtbl, GetDesc) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DebugVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DebugVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DebugVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11DebugVtbl, ReportLiveDeviceObjects) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, SetMessageCountLimit) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, ClearStoredMessages) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, GetMessage) / sizeof(void*)),
    (unsigned int)(offsetof(ID3D11InfoQueueVtbl, GetNumStoredMessages) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIDeviceVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIDeviceVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIDeviceVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIDeviceVtbl, GetAdapter) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIAdapterVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIAdapterVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIAdapterVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIAdapterVtbl, GetParent) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIAdapterVtbl, GetDesc) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory2Vtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory2Vtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory2Vtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory2Vtbl, MakeWindowAssociation) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory2Vtbl, CreateSwapChainForHwnd) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory5Vtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory5Vtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory5Vtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGIFactory5Vtbl, CheckFeatureSupport) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, Present) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, GetBuffer) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, ResizeBuffers) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, SetMaximumFrameLatency) / sizeof(void*)),
    (unsigned int)(offsetof(IDXGISwapChain2Vtbl, GetFrameLatencyWaitableObject) / sizeof(void*)),
    (unsigned int)(offsetof(ID3DBlobVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(ID3DBlobVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(ID3DBlobVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(ID3DBlobVtbl, GetBufferPointer) / sizeof(void*)),
    (unsigned int)(offsetof(ID3DBlobVtbl, GetBufferSize) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceEnumeratorVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceEnumeratorVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceEnumeratorVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceEnumeratorVtbl, GetDefaultAudioEndpoint) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IMMDeviceVtbl, Activate) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, Initialize) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, GetBufferSize) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, GetStreamLatency) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, GetCurrentPadding) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, IsFormatSupported) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, GetMixFormat) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, GetDevicePeriod) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, Start) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, Stop) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, Reset) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, SetEventHandle) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioClientVtbl, GetService) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioRenderClientVtbl, QueryInterface) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioRenderClientVtbl, AddRef) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioRenderClientVtbl, Release) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioRenderClientVtbl, GetBuffer) / sizeof(void*)),
    (unsigned int)(offsetof(IAudioRenderClientVtbl, ReleaseBuffer) / sizeof(void*))
};

// Size of every struct win32_api.h pins with BMF_STATIC_ASSERT.
const unsigned int VTABLE_SIZES[] = {
    (unsigned int)sizeof(GUID),
    (unsigned int)sizeof(RECT),
    (unsigned int)sizeof(POINT),
    (unsigned int)sizeof(SECURITY_ATTRIBUTES),
    (unsigned int)sizeof(STARTUPINFOW),
    (unsigned int)sizeof(PROCESS_INFORMATION),
    (unsigned int)sizeof(WIN32_FIND_DATAW),
    (unsigned int)sizeof(WIN32_FILE_ATTRIBUTE_DATA),
    (unsigned int)sizeof(SYSTEM_INFO),
    (unsigned int)sizeof(EXCEPTION_RECORD),
    (unsigned int)sizeof(EXCEPTION_POINTERS),
    (unsigned int)sizeof(WNDCLASSEXW),
    (unsigned int)sizeof(MSG),
    (unsigned int)sizeof(MONITORINFO),
    (unsigned int)sizeof(WINDOWPLACEMENT),
    (unsigned int)sizeof(RAWINPUTDEVICE),
    (unsigned int)sizeof(RAWINPUTHEADER),
    (unsigned int)sizeof(RAWMOUSE),
    (unsigned int)sizeof(RAWKEYBOARD),
    (unsigned int)sizeof(RAWINPUT),
    (unsigned int)sizeof(POINTER_INFO),
    (unsigned int)sizeof(POINTER_PEN_INFO),
    (unsigned int)sizeof(XINPUT_GAMEPAD),
    (unsigned int)sizeof(XINPUT_STATE),
    (unsigned int)sizeof(XINPUT_VIBRATION),
    (unsigned int)sizeof(WAVEFORMATEX),
    (unsigned int)sizeof(WAVEFORMATEXTENSIBLE),
    (unsigned int)sizeof(DXGI_SWAP_CHAIN_DESC1),
    (unsigned int)sizeof(DXGI_ADAPTER_DESC),
    (unsigned int)sizeof(D3D11_BUFFER_DESC),
    (unsigned int)sizeof(D3D11_TEXTURE2D_DESC),
    (unsigned int)sizeof(D3D11_SUBRESOURCE_DATA),
    (unsigned int)sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC),
    (unsigned int)sizeof(D3D11_RENDER_TARGET_VIEW_DESC),
    (unsigned int)sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC),
    (unsigned int)sizeof(D3D11_INPUT_ELEMENT_DESC),
    (unsigned int)sizeof(D3D11_BLEND_DESC),
    (unsigned int)sizeof(D3D11_RASTERIZER_DESC),
    (unsigned int)sizeof(D3D11_DEPTH_STENCIL_DESC),
    (unsigned int)sizeof(D3D11_SAMPLER_DESC),
    (unsigned int)sizeof(D3D11_VIEWPORT),
    (unsigned int)sizeof(D3D11_BOX),
    (unsigned int)sizeof(D3D11_MAPPED_SUBRESOURCE),
    (unsigned int)sizeof(D3D11_MESSAGE),
    (unsigned int)sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS)
};

#if defined(VTABLE_SIDE_SDK)
const void* const vtable_sdk_guids[] = {
    &IID_ID3D11Texture2D,
    &IID_ID3D11DeviceContext1,
    &IID_ID3D11Debug,
    &IID_ID3D11InfoQueue,
    &IID_IDXGIDevice,
    &IID_IDXGIFactory2,
    &IID_IDXGIFactory5,
    &IID_IDXGISwapChain2
};
#else
static const void* const vtable_bmf_guids[] = {
    &WIN32_IID_ID3D11Texture2D,
    &WIN32_IID_ID3D11DeviceContext1,
    &WIN32_IID_ID3D11Debug,
    &WIN32_IID_ID3D11InfoQueue,
    &WIN32_IID_IDXGIDevice,
    &WIN32_IID_IDXGIFactory2,
    &WIN32_IID_IDXGIFactory5,
    &WIN32_IID_IDXGISwapChain2
};

static const char* const VTABLE_NAMES[] = {
    "IUnknownVtbl.QueryInterface",
    "IUnknownVtbl.AddRef",
    "IUnknownVtbl.Release",
    "ID3D11DeviceVtbl.QueryInterface",
    "ID3D11DeviceVtbl.AddRef",
    "ID3D11DeviceVtbl.Release",
    "ID3D11DeviceVtbl.CreateBuffer",
    "ID3D11DeviceVtbl.CreateTexture2D",
    "ID3D11DeviceVtbl.CreateShaderResourceView",
    "ID3D11DeviceVtbl.CreateRenderTargetView",
    "ID3D11DeviceVtbl.CreateDepthStencilView",
    "ID3D11DeviceVtbl.CreateInputLayout",
    "ID3D11DeviceVtbl.CreateVertexShader",
    "ID3D11DeviceVtbl.CreatePixelShader",
    "ID3D11DeviceVtbl.CreateBlendState",
    "ID3D11DeviceVtbl.CreateDepthStencilState",
    "ID3D11DeviceVtbl.CreateRasterizerState",
    "ID3D11DeviceVtbl.CreateSamplerState",
    "ID3D11DeviceVtbl.CheckFeatureSupport",
    "ID3D11DeviceVtbl.GetFeatureLevel",
    "ID3D11DeviceVtbl.GetDeviceRemovedReason",
    "ID3D11DeviceContextVtbl.QueryInterface",
    "ID3D11DeviceContextVtbl.AddRef",
    "ID3D11DeviceContextVtbl.Release",
    "ID3D11DeviceContextVtbl.VSSetConstantBuffers",
    "ID3D11DeviceContextVtbl.PSSetShaderResources",
    "ID3D11DeviceContextVtbl.PSSetShader",
    "ID3D11DeviceContextVtbl.PSSetSamplers",
    "ID3D11DeviceContextVtbl.VSSetShader",
    "ID3D11DeviceContextVtbl.DrawIndexed",
    "ID3D11DeviceContextVtbl.Draw",
    "ID3D11DeviceContextVtbl.Map",
    "ID3D11DeviceContextVtbl.Unmap",
    "ID3D11DeviceContextVtbl.PSSetConstantBuffers",
    "ID3D11DeviceContextVtbl.IASetInputLayout",
    "ID3D11DeviceContextVtbl.IASetVertexBuffers",
    "ID3D11DeviceContextVtbl.IASetIndexBuffer",
    "ID3D11DeviceContextVtbl.DrawIndexedInstanced",
    "ID3D11DeviceContextVtbl.DrawInstanced",
    "ID3D11DeviceContextVtbl.IASetPrimitiveTopology",
    "ID3D11DeviceContextVtbl.VSSetShaderResources",
    "ID3D11DeviceContextVtbl.VSSetSamplers",
    "ID3D11DeviceContextVtbl.OMSetRenderTargets",
    "ID3D11DeviceContextVtbl.OMSetBlendState",
    "ID3D11DeviceContextVtbl.OMSetDepthStencilState",
    "ID3D11DeviceContextVtbl.RSSetState",
    "ID3D11DeviceContextVtbl.RSSetViewports",
    "ID3D11DeviceContextVtbl.RSSetScissorRects",
    "ID3D11DeviceContextVtbl.CopySubresourceRegion",
    "ID3D11DeviceContextVtbl.CopyResource",
    "ID3D11DeviceContextVtbl.UpdateSubresource",
    "ID3D11DeviceContextVtbl.ClearRenderTargetView",
    "ID3D11DeviceContextVtbl.ClearDepthStencilView",
    "ID3D11DeviceContextVtbl.ClearState",
    "ID3D11DeviceContextVtbl.Flush",
    "ID3D11DeviceContextVtbl.UpdateSubresource1",
    "ID3D11DeviceContextVtbl.VSSetConstantBuffers1",
    "ID3D11DeviceContextVtbl.PSSetConstantBuffers1",
    "ID3D11Texture2DVtbl.QueryInterface",
    "ID3D11Texture2DVtbl.AddRef",
    "ID3D11Texture2DVtbl.Release",
    "ID3D11Texture2DVtbl.GetDesc",
    "ID3D11DebugVtbl.QueryInterface",
    "ID3D11DebugVtbl.AddRef",
    "ID3D11DebugVtbl.Release",
    "ID3D11DebugVtbl.ReportLiveDeviceObjects",
    "ID3D11InfoQueueVtbl.QueryInterface",
    "ID3D11InfoQueueVtbl.AddRef",
    "ID3D11InfoQueueVtbl.Release",
    "ID3D11InfoQueueVtbl.SetMessageCountLimit",
    "ID3D11InfoQueueVtbl.ClearStoredMessages",
    "ID3D11InfoQueueVtbl.GetMessage",
    "ID3D11InfoQueueVtbl.GetNumStoredMessages",
    "IDXGIDeviceVtbl.QueryInterface",
    "IDXGIDeviceVtbl.AddRef",
    "IDXGIDeviceVtbl.Release",
    "IDXGIDeviceVtbl.GetAdapter",
    "IDXGIAdapterVtbl.QueryInterface",
    "IDXGIAdapterVtbl.AddRef",
    "IDXGIAdapterVtbl.Release",
    "IDXGIAdapterVtbl.GetParent",
    "IDXGIAdapterVtbl.GetDesc",
    "IDXGIFactory2Vtbl.QueryInterface",
    "IDXGIFactory2Vtbl.AddRef",
    "IDXGIFactory2Vtbl.Release",
    "IDXGIFactory2Vtbl.MakeWindowAssociation",
    "IDXGIFactory2Vtbl.CreateSwapChainForHwnd",
    "IDXGIFactory5Vtbl.QueryInterface",
    "IDXGIFactory5Vtbl.AddRef",
    "IDXGIFactory5Vtbl.Release",
    "IDXGIFactory5Vtbl.CheckFeatureSupport",
    "IDXGISwapChain2Vtbl.QueryInterface",
    "IDXGISwapChain2Vtbl.AddRef",
    "IDXGISwapChain2Vtbl.Release",
    "IDXGISwapChain2Vtbl.Present",
    "IDXGISwapChain2Vtbl.GetBuffer",
    "IDXGISwapChain2Vtbl.ResizeBuffers",
    "IDXGISwapChain2Vtbl.SetMaximumFrameLatency",
    "IDXGISwapChain2Vtbl.GetFrameLatencyWaitableObject",
    "ID3DBlobVtbl.QueryInterface",
    "ID3DBlobVtbl.AddRef",
    "ID3DBlobVtbl.Release",
    "ID3DBlobVtbl.GetBufferPointer",
    "ID3DBlobVtbl.GetBufferSize",
    "IMMDeviceEnumeratorVtbl.QueryInterface",
    "IMMDeviceEnumeratorVtbl.AddRef",
    "IMMDeviceEnumeratorVtbl.Release",
    "IMMDeviceEnumeratorVtbl.GetDefaultAudioEndpoint",
    "IMMDeviceVtbl.QueryInterface",
    "IMMDeviceVtbl.AddRef",
    "IMMDeviceVtbl.Release",
    "IMMDeviceVtbl.Activate",
    "IAudioClientVtbl.QueryInterface",
    "IAudioClientVtbl.AddRef",
    "IAudioClientVtbl.Release",
    "IAudioClientVtbl.Initialize",
    "IAudioClientVtbl.GetBufferSize",
    "IAudioClientVtbl.GetStreamLatency",
    "IAudioClientVtbl.GetCurrentPadding",
    "IAudioClientVtbl.IsFormatSupported",
    "IAudioClientVtbl.GetMixFormat",
    "IAudioClientVtbl.GetDevicePeriod",
    "IAudioClientVtbl.Start",
    "IAudioClientVtbl.Stop",
    "IAudioClientVtbl.Reset",
    "IAudioClientVtbl.SetEventHandle",
    "IAudioClientVtbl.GetService",
    "IAudioRenderClientVtbl.QueryInterface",
    "IAudioRenderClientVtbl.AddRef",
    "IAudioRenderClientVtbl.Release",
    "IAudioRenderClientVtbl.GetBuffer",
    "IAudioRenderClientVtbl.ReleaseBuffer"
};
static const char* const VTABLE_SIZE_NAMES[] = {
    "sizeof(GUID)",
    "sizeof(RECT)",
    "sizeof(POINT)",
    "sizeof(SECURITY_ATTRIBUTES)",
    "sizeof(STARTUPINFOW)",
    "sizeof(PROCESS_INFORMATION)",
    "sizeof(WIN32_FIND_DATAW)",
    "sizeof(WIN32_FILE_ATTRIBUTE_DATA)",
    "sizeof(SYSTEM_INFO)",
    "sizeof(EXCEPTION_RECORD)",
    "sizeof(EXCEPTION_POINTERS)",
    "sizeof(WNDCLASSEXW)",
    "sizeof(MSG)",
    "sizeof(MONITORINFO)",
    "sizeof(WINDOWPLACEMENT)",
    "sizeof(RAWINPUTDEVICE)",
    "sizeof(RAWINPUTHEADER)",
    "sizeof(RAWMOUSE)",
    "sizeof(RAWKEYBOARD)",
    "sizeof(RAWINPUT)",
    "sizeof(POINTER_INFO)",
    "sizeof(POINTER_PEN_INFO)",
    "sizeof(XINPUT_GAMEPAD)",
    "sizeof(XINPUT_STATE)",
    "sizeof(XINPUT_VIBRATION)",
    "sizeof(WAVEFORMATEX)",
    "sizeof(WAVEFORMATEXTENSIBLE)",
    "sizeof(DXGI_SWAP_CHAIN_DESC1)",
    "sizeof(DXGI_ADAPTER_DESC)",
    "sizeof(D3D11_BUFFER_DESC)",
    "sizeof(D3D11_TEXTURE2D_DESC)",
    "sizeof(D3D11_SUBRESOURCE_DATA)",
    "sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC)",
    "sizeof(D3D11_RENDER_TARGET_VIEW_DESC)",
    "sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC)",
    "sizeof(D3D11_INPUT_ELEMENT_DESC)",
    "sizeof(D3D11_BLEND_DESC)",
    "sizeof(D3D11_RASTERIZER_DESC)",
    "sizeof(D3D11_DEPTH_STENCIL_DESC)",
    "sizeof(D3D11_SAMPLER_DESC)",
    "sizeof(D3D11_VIEWPORT)",
    "sizeof(D3D11_BOX)",
    "sizeof(D3D11_MAPPED_SUBRESOURCE)",
    "sizeof(D3D11_MESSAGE)",
    "sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS)"
};
static const char* const VTABLE_GUID_NAMES[] = {
    "IID_ID3D11Texture2D",
    "IID_ID3D11DeviceContext1",
    "IID_ID3D11Debug",
    "IID_ID3D11InfoQueue",
    "IID_IDXGIDevice",
    "IID_IDXGIFactory2",
    "IID_IDXGIFactory5",
    "IID_IDXGISwapChain2"
};

extern const unsigned int vtable_sdk_values[sizeof vtable_bmf_values / sizeof vtable_bmf_values[0]];
extern const unsigned int vtable_sdk_sizes[sizeof vtable_bmf_sizes / sizeof vtable_bmf_sizes[0]];
extern const void* const vtable_sdk_guids[sizeof vtable_bmf_guids / sizeof vtable_bmf_guids[0]];

BMF_STATIC_ASSERT(sizeof VTABLE_NAMES / sizeof VTABLE_NAMES[0] == sizeof vtable_bmf_values / sizeof vtable_bmf_values[0], "one name per slot");
BMF_STATIC_ASSERT(sizeof VTABLE_SIZE_NAMES / sizeof VTABLE_SIZE_NAMES[0] == sizeof vtable_bmf_sizes / sizeof vtable_bmf_sizes[0], "one name per size");
BMF_STATIC_ASSERT(sizeof VTABLE_GUID_NAMES / sizeof VTABLE_GUID_NAMES[0] == sizeof vtable_bmf_guids / sizeof vtable_bmf_guids[0], "one name per GUID");

static char vtable_line[512];
static i32 vtable_line_length;

static void vtable_put(const char* s) {
    while (*s && vtable_line_length < (i32)sizeof vtable_line - 2) vtable_line[vtable_line_length++] = *s++;
}

static void vtable_put_u32(u32 v) {
    char digits[12];
    i32 n = 0;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { char c[2] = { digits[--n], 0 }; vtable_put(c); }
}

static void vtable_flush(void) {
    vtable_line[vtable_line_length++] = '\n';
    u32 written = 0;
    WriteFile(GetStdHandle((u32)WIN32_STD_OUTPUT_HANDLE), vtable_line, (u32)vtable_line_length, &written, 0);
    vtable_line_length = 0;
}

static void vtable_mismatch(const char* name, u32 sdk, u32 ours) {
    vtable_put("{\"host_check\":\"vtable_slots\",\"mismatch\":\"");
    vtable_put(name);
    vtable_put("\",\"sdk\":");
    vtable_put_u32(sdk);
    vtable_put(",\"bmf\":");
    vtable_put_u32(ours);
    vtable_put("}");
    vtable_flush();
}

void vtable_check_entry(void);
void vtable_check_entry(void) {
    u32 mismatches = 0;
    u32 slot_count = (u32)(sizeof vtable_bmf_values / sizeof vtable_bmf_values[0]);
    u32 size_count = (u32)(sizeof vtable_bmf_sizes / sizeof vtable_bmf_sizes[0]);
    u32 guid_count = (u32)(sizeof vtable_bmf_guids / sizeof vtable_bmf_guids[0]);
    for (u32 i = 0; i < slot_count; i++) {
        if (vtable_sdk_values[i] == vtable_bmf_values[i]) continue;
        vtable_mismatch(VTABLE_NAMES[i], vtable_sdk_values[i], vtable_bmf_values[i]);
        mismatches++;
    }
    for (u32 i = 0; i < size_count; i++) {
        if (vtable_sdk_sizes[i] == vtable_bmf_sizes[i]) continue;
        vtable_mismatch(VTABLE_SIZE_NAMES[i], vtable_sdk_sizes[i], vtable_bmf_sizes[i]);
        mismatches++;
    }
    for (u32 i = 0; i < guid_count; i++) {
        const u8* sdk = (const u8*)vtable_sdk_guids[i];
        const u8* ours = (const u8*)vtable_bmf_guids[i];
        for (i32 b = 0; b < 16; b++) {
            if (sdk[b] == ours[b]) continue;
            vtable_mismatch(VTABLE_GUID_NAMES[i], sdk[b], ours[b]);
            mismatches++;
            break;
        }
    }
    vtable_put("{\"host_check\":\"vtable_slots\",\"ok\":");
    vtable_put(mismatches ? "0" : "1");
    vtable_put(",\"slots\":"); vtable_put_u32(slot_count);
    vtable_put(",\"structs\":"); vtable_put_u32(size_count);
    vtable_put(",\"guids\":"); vtable_put_u32(guid_count);
    vtable_put(",\"mismatches\":"); vtable_put_u32(mismatches);
    vtable_put("}");
    vtable_flush();
    ExitProcess(mismatches ? 1u : 0u);
}
#endif
