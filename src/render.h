// GDI+ renderer: draws a Skin with the current Metrics into a premultiplied-ARGB bitmap.
#pragma once
#include "skin.h"
#include <memory>
#include <string>

namespace qm {

struct RenderImpl;

class Renderer {
public:
    Renderer();
    ~Renderer();
    // pxW/pxH: bitmap size in device pixels; scale: device pixels per skin unit (dpi/96 * skin scale).
    // `bits` points to pxW*pxH*4 bytes, 32bpp premultiplied ARGB, stride = pxW*4 (top-down).
    void draw(void* bits, int pxW, int pxH, const Skin& s, const Metrics& m, float scale);
    void clearCaches();   // call after a skin reload

    // Convenience for tools/tests: renders to a PNG file. Returns false on failure.
    bool drawToPng(const std::wstring& pngPath, const Skin& s, const Metrics& m, float scale);

private:
    std::unique_ptr<RenderImpl> d_;
};

}  // namespace qm
