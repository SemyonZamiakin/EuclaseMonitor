#include "render.h"
#include "util.h"
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <gdiplus.h>

using namespace Gdiplus;

namespace qm {

struct RenderImpl {
    ULONG_PTR token = 0;
    std::map<std::string, std::unique_ptr<Font>> fonts;
    std::map<std::wstring, std::unique_ptr<Bitmap>> images;   // detached copies: the file is never locked, so users can overwrite it while editing
    float fontScale = 0;
};

static Gdiplus::Color toG(const qm::Color& c, float opacity = 1.f) { return Gdiplus::Color((BYTE)(c.a * std::min(1.f, std::max(0.f, opacity))), c.r, c.g, c.b); }

static void roundedPath(GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0.5f) { p.AddRectangle(RectF(x, y, w, h)); return; }
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static Bitmap* loadDetached(const std::wstring& path) {
    std::string data;
    if (!readFile(path, data, 32u * 1024 * 1024)) return nullptr;
    IStream* st = SHCreateMemStream((const BYTE*)data.data(), (UINT)data.size());
    if (!st) return nullptr;
    Bitmap* src = Bitmap::FromStream(st);
    Bitmap* out = nullptr;
    if (src && src->GetLastStatus() == Ok && src->GetWidth() > 0 && src->GetHeight() > 0) {
        out = new Bitmap(src->GetWidth(), src->GetHeight(), PixelFormat32bppPARGB);
        Graphics g(out);
        g.DrawImage(src, 0, 0, (INT)src->GetWidth(), (INT)src->GetHeight());
    }
    delete src;
    st->Release();   // the stream may be released only after the source bitmap is gone
    return out;
}

static Bitmap* getImage(RenderImpl& d, const std::wstring& dir, const std::string& file) {
    std::wstring path = joinPath(dir, widen(file));
    auto it = d.images.find(path);
    if (it != d.images.end()) return it->second.get();
    // only allow files inside the skin folder: no "..\" tricks in downloaded skins
    wchar_t full[MAX_PATH * 2], base[MAX_PATH * 2];
    bool inside = GetFullPathNameW(path.c_str(), MAX_PATH * 2, full, nullptr) && GetFullPathNameW(dir.c_str(), MAX_PATH * 2, base, nullptr) &&
                  _wcsnicmp(full, base, wcslen(base)) == 0;
    Bitmap* b = inside ? loadDetached(path) : nullptr;
    if (!b) logLine("image not found or outside the skin folder: " + file);
    d.images[path].reset(b);
    return b;
}

static Font* getFont(RenderImpl& d, const Skin& s, const std::string& name, float scale) {
    std::string key = name + "@" + std::to_string(scale);
    auto it = d.fonts.find(key);
    if (it != d.fonts.end()) return it->second.get();
    auto f = s.fonts.find(name);
    FontSpec spec = f != s.fonts.end() ? f->second : FontSpec();
    int style = (spec.bold ? FontStyleBold : 0) | (spec.italic ? FontStyleItalic : 0);
    std::wstring fam = widen(spec.family);
    std::unique_ptr<FontFamily> ff(new FontFamily(fam.c_str()));
    if (!ff->IsAvailable()) ff.reset(new FontFamily(L"Segoe UI"));   // unknown family on this PC: fall back instead of drawing nothing
    Font* font = new Font(ff.get(), spec.size * scale, style, UnitPixel);
    d.fonts[key].reset(font);
    return font;
}

static void drawImageFit(Graphics& g, Bitmap* img, RectF box, const std::string& fit, float opacity) {
    float iw = (float)img->GetWidth(), ih = (float)img->GetHeight();
    RectF dst = box;
    if (fit != "stretch") {
        float s = fit == "cover" ? std::max(box.Width / iw, box.Height / ih) : std::min(box.Width / iw, box.Height / ih);
        dst = RectF(box.X + (box.Width - iw * s) / 2, box.Y + (box.Height - ih * s) / 2, iw * s, ih * s);
    }
    ImageAttributes ia;
    ColorMatrix cm = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, opacity, 0, 0, 0, 0, 0, 1};
    ia.SetColorMatrix(&cm);
    g.DrawImage(img, dst, 0, 0, iw, ih, UnitPixel, &ia);
}

Renderer::Renderer() : d_(new RenderImpl) {
    GdiplusStartupInput in;
    GdiplusStartup(&d_->token, &in, nullptr);
}
Renderer::~Renderer() {
    clearCaches();
    if (d_->token) GdiplusShutdown(d_->token);
}
void Renderer::clearCaches() { d_->fonts.clear(); d_->images.clear(); }

void Renderer::draw(void* bits, int pxW, int pxH, const Skin& s, const Metrics& m, float scale) {
    Bitmap surface(pxW, pxH, pxW * 4, PixelFormat32bppPARGB, (BYTE*)bits);
    Graphics g(&surface);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);   // grayscale AA: ClearType is wrong on a transparent surface
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.ScaleTransform(scale, scale);
    const float W = s.width, H = s.height;

    // ---- background
    {
        GraphicsPath bg;
        roundedPath(bg, 0, 0, W, H, s.bgRadius);
        if (s.bgGradient) {
            double a = s.bgAngle * 3.14159265 / 180.0;
            PointF p0(W / 2 - (float)cos(a) * W / 2, H / 2 - (float)sin(a) * H / 2), p1(W / 2 + (float)cos(a) * W / 2, H / 2 + (float)sin(a) * H / 2);
            LinearGradientBrush br(p0, p1, toG(s.bgColor), toG(s.bgGradientTo));
            g.FillPath(&br, &bg);
        } else {
            SolidBrush br(toG(s.bgColor));
            g.FillPath(&br, &bg);
        }
        if (!s.bgImage.empty()) {
            if (Bitmap* img = getImage(*d_, s.dir, s.bgImage)) {
                GraphicsState st = g.Save();
                g.SetClip(&bg, CombineModeIntersect);
                drawImageFit(g, img, RectF(0, 0, W, H), s.bgFit, s.bgImageOpacity);
                g.Restore(st);
            }
        }
        if (s.bgBorderWidth > 0 && s.bgBorder.a) {
            Pen pen(toG(s.bgBorder), s.bgBorderWidth);
            GraphicsPath inner;
            roundedPath(inner, s.bgBorderWidth / 2, s.bgBorderWidth / 2, W - s.bgBorderWidth, H - s.bgBorderWidth, s.bgRadius);
            g.DrawPath(&pen, &inner);
        }
    }

    // ---- widgets
    for (const Widget& w : s.widgets) {
        if (w.hasVisible) {
            double v = w.visible.eval(m);
            if (std::isnan(v) || v == 0) continue;
        }
        const float op = w.opacity;
        switch (w.type) {
            case WType::Rect: {
                GraphicsPath p;
                roundedPath(p, w.x, w.y, w.w, w.h, w.radius);
                if (w.hasColor2) {
                    LinearGradientBrush br(PointF(w.x, w.y), PointF(w.x, w.y + w.h), toG(w.color, op), toG(w.color2, op));
                    g.FillPath(&br, &p);
                } else {
                    SolidBrush br(toG(w.color, op));
                    g.FillPath(&br, &p);
                }
                if (w.borderWidth > 0) { Pen pen(toG(w.border, op), w.borderWidth); g.DrawPath(&pen, &p); }
                break;
            }
            case WType::Line: {
                Pen pen(toG(w.color, op), w.lineWidth);
                pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
                g.DrawLine(&pen, w.x, w.y, w.x2, w.y2);
                break;
            }
            case WType::Image: {
                if (Bitmap* img = getImage(*d_, s.dir, w.file)) {
                    GraphicsState st = g.Save();
                    GraphicsPath clip;
                    roundedPath(clip, w.x, w.y, w.w, w.h, w.radius);
                    g.SetClip(&clip, CombineModeIntersect);
                    drawImageFit(g, img, RectF(w.x, w.y, w.w, w.h), w.fit, op);
                    g.Restore(st);
                }
                break;
            }
            case WType::Text: {
                std::wstring text = widen(w.text.render(m));
                Font* font = getFont(*d_, s, w.font, scale);
                StringFormat sf;
                sf.SetAlignment(w.align == HAlign::Center ? StringAlignmentCenter : w.align == HAlign::Right ? StringAlignmentFar : StringAlignmentNear);
                sf.SetLineAlignment(w.valign == VAlign::Middle ? StringAlignmentCenter : w.valign == VAlign::Bottom ? StringAlignmentFar : StringAlignmentNear);
                sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
                if (w.w > 0) sf.SetTrimming(StringTrimmingEllipsisCharacter);
                SolidBrush br(toG(w.color, op));
                // text is drawn in device space so glyph hinting matches the scaled font
                GraphicsState st = g.Save();
                g.ResetTransform();
                float bw = w.w > 0 ? w.w * scale : 4000.f, bh = w.h > 0 ? w.h * scale : 4000.f;
                float bx = w.x * scale, by = w.y * scale;
                if (w.w <= 0) { if (w.align == HAlign::Center) bx -= bw / 2; else if (w.align == HAlign::Right) bx -= bw; }
                if (w.h <= 0) { if (w.valign == VAlign::Middle) by -= bh / 2; else if (w.valign == VAlign::Bottom) by -= bh; }
                g.DrawString(text.c_str(), (INT)text.size(), font, RectF(bx, by, bw, bh), &sf, &br);
                g.Restore(st);
                break;
            }
            case WType::Bar: {
                double v = w.value.eval(m);
                float frac = std::isnan(v) ? 0.f : (float)std::min(1.0, std::max(0.0, (v - w.min) / (w.max - w.min)));
                GraphicsPath track;
                roundedPath(track, w.x, w.y, w.w, w.h, w.radius);
                SolidBrush tb(toG(w.track, op));
                g.FillPath(&tb, &track);
                if (frac > 0) {
                    qm::Color c = pickThreshold(w, v);
                    GraphicsState st = g.Save();
                    g.SetClip(&track, CombineModeIntersect);
                    RectF fill = w.vertical ? RectF(w.x, w.y + w.h * (1 - frac), w.w, w.h * frac) : RectF(w.x, w.y, w.w * frac, w.h);
                    GraphicsPath fp;
                    roundedPath(fp, fill.X, fill.Y, fill.Width, fill.Height, w.radius);
                    if (w.hasColor2 && w.thresholds.empty()) {
                        LinearGradientBrush br(w.vertical ? PointF(w.x, w.y + w.h) : PointF(w.x, w.y), w.vertical ? PointF(w.x, w.y) : PointF(w.x + w.w, w.y), toG(w.color, op), toG(w.color2, op));
                        g.FillPath(&br, &fp);
                    } else {
                        SolidBrush br(toG(c, op));
                        g.FillPath(&br, &fp);
                    }
                    g.Restore(st);
                }
                if (w.borderWidth > 0) { Pen pen(toG(w.border, op), w.borderWidth); g.DrawPath(&pen, &track); }
                break;
            }
            case WType::Graph: {
                GraphicsPath box;
                roundedPath(box, w.x, w.y, w.w, w.h, w.radius);
                SolidBrush tb(toG(w.track, op));
                g.FillPath(&tb, &box);
                if (w.hist.size() < 2) break;
                double last = w.hist.back();
                qm::Color c = pickThreshold(w, last);
                std::vector<PointF> pts;
                float step = w.w / (float)(w.history - 1);
                float x0 = w.x + w.w - step * (float)(w.hist.size() - 1);
                GraphicsState st = g.Save();
                g.SetClip(&box, CombineModeIntersect);
                std::vector<std::vector<PointF>> runs(1);
                for (size_t i = 0; i < w.hist.size(); ++i) {
                    float v = w.hist[i];
                    if (std::isnan(v)) { if (!runs.back().empty()) runs.emplace_back(); continue; }
                    float f = (float)std::min(1.0, std::max(0.0, (v - w.min) / (w.max - w.min)));
                    runs.back().push_back(PointF(x0 + step * i, w.y + w.h - 1 - f * (w.h - 2)));
                }
                for (auto& r : runs) {
                    if (r.size() < 2) continue;
                    if (w.fill) {
                        std::vector<PointF> poly = r;
                        poly.push_back(PointF(r.back().X, w.y + w.h));
                        poly.push_back(PointF(r.front().X, w.y + w.h));
                        LinearGradientBrush fb(PointF(0, w.y), PointF(0, w.y + w.h), Gdiplus::Color((BYTE)(c.a * 0.38f * op), c.r, c.g, c.b), Gdiplus::Color(0, c.r, c.g, c.b));
                        g.FillPolygon(&fb, poly.data(), (INT)poly.size());
                    }
                    Pen pen(toG(c, op), w.lineWidth);
                    pen.SetLineJoin(LineJoinRound);
                    g.DrawLines(&pen, r.data(), (INT)r.size());
                }
                g.Restore(st);
                break;
            }
            case WType::Ring: {
                double v = w.value.eval(m);
                float frac = std::isnan(v) ? 0.f : (float)std::min(1.0, std::max(0.0, (v - w.min) / (w.max - w.min)));
                float side = std::min(w.w, w.h), t = w.thickness;
                RectF r(w.x + t / 2, w.y + t / 2, side - t, side - t);
                Pen tp(toG(w.track, op), t);
                tp.SetStartCap(LineCapRound); tp.SetEndCap(LineCapRound);
                g.DrawArc(&tp, r, w.startAngle, w.sweep);
                if (frac > 0.001f) {
                    Pen vp(toG(pickThreshold(w, v), op), t);
                    vp.SetStartCap(LineCapRound); vp.SetEndCap(LineCapRound);
                    g.DrawArc(&vp, r, w.startAngle, w.sweep * frac);
                }
                break;
            }
        }
    }
}

static bool encoderClsid(const WCHAR* mime, CLSID* clsid) {
    UINT n = 0, sz = 0;
    GetImageEncodersSize(&n, &sz);
    if (!sz) return false;
    std::vector<char> buf(sz);
    ImageCodecInfo* info = (ImageCodecInfo*)buf.data();
    GetImageEncoders(n, sz, info);
    for (UINT i = 0; i < n; ++i) if (!wcscmp(info[i].MimeType, mime)) { *clsid = info[i].Clsid; return true; }
    return false;
}

bool Renderer::drawToPng(const std::wstring& pngPath, const Skin& s, const Metrics& m, float scale) {
    int pxW = (int)std::ceil(s.width * scale), pxH = (int)std::ceil(s.height * scale);
    std::vector<unsigned char> bits((size_t)pxW * pxH * 4, 0);
    draw(bits.data(), pxW, pxH, s, m, scale);
    Bitmap bmp(pxW, pxH, pxW * 4, PixelFormat32bppPARGB, bits.data());
    CLSID png;
    if (!encoderClsid(L"image/png", &png)) return false;
    return bmp.Save(pngPath.c_str(), &png, nullptr) == Ok;
}

}  // namespace qm
