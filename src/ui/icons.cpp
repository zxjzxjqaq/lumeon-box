#include "ui/icons.hpp"
#include <shellapi.h>
#include <filesystem>
#include "core/strutil.hpp"
#pragma comment(lib, "shell32")

namespace lb {
namespace fs = std::filesystem;

static std::wstring CacheKey(const std::wstring& path) {
    std::wstring ext = LowerExt(fs::path(path).filename().wstring());
    if (ext == L"exe" || ext == L"msi" || ext == L"lnk")  // 每文件图标
        return L"@" + LowerExt(path);
    return ext;
}

// HICON → 32bpp 预乘 BGRA → D2D 位图。失败返回 nullptr。
static ComPtr<ID2D1Bitmap> IconToBitmap(ID2D1DeviceContext* ctx, HICON icon) {
    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii)) return nullptr;
    auto cleanup = [&] {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
    };

    BITMAP bm{};
    if (!ii.hbmColor || GetObjectW(ii.hbmColor, sizeof(bm), &bm) == 0 || bm.bmBitsPixel != 32) {
        cleanup();
        return nullptr;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> pixels(size_t(bm.bmWidth) * bm.bmHeight * 4);
    HDC hdc = GetDC(nullptr);
    int got = GetDIBits(hdc, ii.hbmColor, 0, bm.bmHeight, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    cleanup();
    if (got != bm.bmHeight) return nullptr;

    // straight alpha → premultiplied（D2D 要求）
    for (uint8_t* p = pixels.data(); p < pixels.data() + pixels.size(); p += 4) {
        uint32_t a = p[3];
        if (a < 255) {
            p[0] = uint8_t(p[0] * a / 255);
            p[1] = uint8_t(p[1] * a / 255);
            p[2] = uint8_t(p[2] * a / 255);
        }
    }

    D2D1_BITMAP_PROPERTIES props{
        D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
        96.f, 96.f};
    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(ctx->CreateBitmap(D2D1::SizeU(bm.bmWidth, bm.bmHeight), pixels.data(),
                                 bm.bmWidth * 4, props, bmp.GetAddressOf())))
        return nullptr;
    return bmp;
}

ID2D1Bitmap* IconCache::Get(ID2D1DeviceContext* ctx, const std::wstring& path) {
    std::wstring key = CacheKey(path);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second.Get();

    SHFILEINFOW sfi{};
    ComPtr<ID2D1Bitmap> bmp;
    // 深入格子目录取真实文件（目录参数用 FILE_ATTRIBUTE_NORMAL 避免拿到文件夹图标）
    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_LARGEICON)) {
        bmp = IconToBitmap(ctx, sfi.hIcon);
        DestroyIcon(sfi.hIcon);
    }
    ID2D1Bitmap* raw = bmp.Get();
    cache_[std::move(key)] = std::move(bmp);  // 失败也缓存 nullptr 避免反复重试
    return raw;
}

}  // namespace lb
