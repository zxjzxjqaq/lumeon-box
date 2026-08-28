#pragma once
// 图形上下文：D3D → D2D → 原生 DirectComposition（dcomp.dll）。
// 注：本机系统缺失 Windows.UI.Composition（WinRT）组件（激活被拒/注册表与 DLL 被精简），
// 动效层改用 DComp——纯 COM、所有 Win10/11 系统自带，动画同样运行在 DWM 合成线程。
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_2.h>
#include <dcomp.h>
#include <dcompanimation.h>
#include <wrl/client.h>
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dcomp")

namespace lb {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// 进程内一份：D2D 设备 + DComp 设备 + 窗口目标 + 根视觉
struct GraphicsContext {
    ComPtr<ID2D1Device> d2d;
    ComPtr<IDCompositionDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> root;

    bool InitForWindow(HWND hwnd) {
        ComPtr<ID3D11Device> d3d;
        D3D_FEATURE_LEVEL fl{};
        if (FAILED(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, d3d.GetAddressOf(), &fl, nullptr)))
            return false;
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d.As(&dxgi))) return false;
        if (FAILED(::D2D1CreateDevice(dxgi.Get(), nullptr, d2d.GetAddressOf()))) return false;
        if (FAILED(::DCompositionCreateDevice2(
                d2d.Get(), IID_PPV_ARGS(device.ReleaseAndGetAddressOf()))))
            return false;
        if (FAILED(device->CreateTargetForHwnd(hwnd, FALSE, target.GetAddressOf())))
            return false;
        if (FAILED(device->CreateVisual(root.GetAddressOf()))) return false;
        target->SetRoot(root.Get());
        device->Commit();
        return true;
    }

    // 在新表面绘制：draw(ctx, dpi) 以 DIP 为单位作画
    template <typename DrawFn>
    ComPtr<IDCompositionSurface> DrawToSurface(float wDip, float hDip, float dpi,
                                               DrawFn&& draw) {
        ComPtr<IDCompositionSurface> surface;
        if (FAILED(device->CreateSurface(static_cast<UINT>(wDip * dpi),
                                         static_cast<UINT>(hDip * dpi),
                                         DXGI_FORMAT_B8G8R8A8_UNORM,
                                         DXGI_ALPHA_MODE_PREMULTIPLIED,
                                         surface.GetAddressOf())))
            return nullptr;
        ComPtr<ID2D1DeviceContext> ctx;
        POINT offset{};
        RECT update{0, 0, static_cast<LONG>(wDip * dpi), static_cast<LONG>(hDip * dpi)};
        if (FAILED(surface->BeginDraw(&update, IID_PPV_ARGS(ctx.ReleaseAndGetAddressOf()),
                                      &offset)))
            return nullptr;
        ctx->SetTransform(
            D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),
                                          static_cast<float>(offset.y)) *
            D2D1::Matrix3x2F::Scale(D2D1::SizeF(dpi, dpi), D2D1::Point2F(0, 0)));
        draw(ctx.Get(), dpi);
        surface->EndDraw();
        return surface;
    }
};

}  // namespace lb
