// SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <type_traits>

// NSIS uses the 32-bit System plug-in; expose an undecorated WINAPI entry point.
#ifdef _M_IX86
#pragma comment(linker, "/export:createWizardBitmap=_createWizardBitmap@12")
#endif

namespace iconicfolders {

// The caller owns the returned HBITMAP and must release it with DeleteObject.
extern "C" __declspec(dllexport) HBITMAP WINAPI createWizardBitmap(
    const wchar_t *path, int width, int height) {
    if (!path || !*path || width <= 0 || height <= 0)
        return nullptr;

    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
        return nullptr;

    // Destroy all GDI+ objects before shutting down this invocation's session.
    HBITMAP bitmap = [&]() -> HBITMAP {
        Gdiplus::Bitmap source(path);
        if (source.GetLastStatus() != Gdiplus::Ok)
            return nullptr;
        Gdiplus::Bitmap target(width, height, PixelFormat32bppPARGB);
        if (target.GetLastStatus() != Gdiplus::Ok)
            return nullptr;
        const Gdiplus::Color background(255, 241, 242, 246);
        {
            Gdiplus::Graphics graphics(&target);
            if (graphics.GetLastStatus() != Gdiplus::Ok ||
                graphics.Clear(background) != Gdiplus::Ok ||
                graphics.SetInterpolationMode(
                    Gdiplus::InterpolationModeHighQualityBicubic) != Gdiplus::Ok ||
                graphics.DrawImage(&source, Gdiplus::Rect(0, 0, width, height),
                    0, 0, static_cast<INT>(source.GetWidth()),
                    static_cast<INT>(source.GetHeight()), Gdiplus::UnitPixel) != Gdiplus::Ok)
                return nullptr;
        }
        HBITMAP handle = nullptr;
        const auto status = target.GetHBITMAP(background, &handle);
        std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)>
            owned(handle, DeleteObject);
        return status == Gdiplus::Ok ? owned.release() : nullptr;
    }();
    Gdiplus::GdiplusShutdown(token);
    return bitmap;
}

} // namespace iconicfolders
