// Spike P0-08 host check: compare hand-declared slots with SDK d3d11.h (host tool, uses SDK headers).
#define COBJMACROS
#include <stddef.h>
#include <stdio.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#define S(T, m) printf("%s.%s %zu\n", #T, #m, offsetof(T##Vtbl, m) / sizeof(void*))
int main(void) {
    S(ID3D10Blob, GetBufferPointer); S(ID3D10Blob, GetBufferSize);
    S(ID3D11Device, Release); S(ID3D11Device, CreateTexture2D); S(ID3D11Device, CreateRenderTargetView);
    S(ID3D11Device, CreateVertexShader); S(ID3D11Device, CreatePixelShader);
    S(ID3D11DeviceContext, Release); S(ID3D11DeviceContext, PSSetShader); S(ID3D11DeviceContext, VSSetShader); S(ID3D11DeviceContext, Draw);
    S(ID3D11DeviceContext, Map); S(ID3D11DeviceContext, Unmap); S(ID3D11DeviceContext, IASetPrimitiveTopology);
    S(ID3D11DeviceContext, OMSetRenderTargets); S(ID3D11DeviceContext, RSSetViewports); S(ID3D11DeviceContext, CopyResource);
    S(ID3D11DeviceContext, ClearRenderTargetView);
    printf("sizeof.D3D11_TEXTURE2D_DESC %zu\nsizeof.D3D11_VIEWPORT %zu\nsizeof.D3D11_MAPPED_SUBRESOURCE %zu\n",
        sizeof(D3D11_TEXTURE2D_DESC), sizeof(D3D11_VIEWPORT), sizeof(D3D11_MAPPED_SUBRESOURCE));
    printf("const.WARP %d TEX_RT %d CPU_READ %d STAGING %d MAP_READ %d TRILIST %d R8G8B8A8 %d FL11 0x%x SDK %d BGRA %d\n",
        D3D_DRIVER_TYPE_WARP, D3D11_BIND_RENDER_TARGET, D3D11_CPU_ACCESS_READ, D3D11_USAGE_STAGING, D3D11_MAP_READ,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, DXGI_FORMAT_R8G8B8A8_UNORM, D3D_FEATURE_LEVEL_11_0, D3D11_SDK_VERSION, D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    printf("const.OPT3 0x%x\n", D3DCOMPILE_OPTIMIZATION_LEVEL3);
    return 0;
}
