// Skin model: everything a user can customise lives in skin.json (see docs/CUSTOMIZE.md).
// The loader never throws: problems become warnings; a skin that cannot be parsed at all is rejected
// and the previously loaded skin keeps running.
#pragma once
#include "expr.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace qm {

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct Threshold { double above; Color color; };

struct FontSpec { std::string family = "Segoe UI"; float size = 12; bool bold = false, italic = false; };

enum class WType { Text, Bar, Graph, Ring, Rect, Image, Line };
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct Widget {
    WType type = WType::Text;
    float x = 0, y = 0, w = 0, h = 0;      // device-independent units (1 unit = 1 px at 100% scale)
    float x2 = 0, y2 = 0;                   // line end
    bool hasVisible = false;
    Expr visible;

    // text
    Template text;
    std::string font = "main";
    HAlign align = HAlign::Left;
    VAlign valign = VAlign::Top;

    // value widgets (bar / graph / ring)
    Expr value;
    double min = 0, max = 100;
    bool vertical = false;
    std::vector<Threshold> thresholds;

    // look
    Color color{76, 141, 255, 255};
    Color color2;                           // gradient end (bar / rect)
    bool hasColor2 = false;
    Color track{255, 255, 255, 40};
    Color border;
    float borderWidth = 0;
    float radius = 0;
    float opacity = 1;

    // graph
    int history = 60;
    bool fill = true;
    float lineWidth = 2;

    // ring
    float thickness = 8, startAngle = 135, sweep = 270;

    // image
    std::string file;
    std::string fit = "contain";

    // runtime state
    std::vector<float> hist;
};

struct Skin {
    std::wstring dir;                       // folder containing skin.json (assets are relative to it)
    std::string name = "Unnamed", author;
    float width = 320, height = 180;
    std::string anchor = "top-right";
    float marginX = 24, marginY = 24;
    int monitor = 0;                        // 0 = primary
    float opacity = 1;
    bool topmost = true, clickThrough = true;
    float scale = 1;
    int updateMs = 1000;

    Color bgColor{16, 20, 36, 220};
    Color bgGradientTo; bool bgGradient = false; float bgAngle = 90;
    std::string bgImage; std::string bgFit = "cover"; float bgImageOpacity = 1;
    float bgRadius = 14;
    Color bgBorder{255, 255, 255, 30}; float bgBorderWidth = 1;

    std::map<std::string, FontSpec> fonts;
    std::vector<Widget> widgets;
    std::vector<std::string> warnings;
};

// dir = folder containing skin.json. On success `out` is replaced entirely.
bool loadSkin(const std::wstring& dir, Skin& out, std::string& fatalError);

// Evaluates graph inputs and pushes them into the widgets' history buffers. Call once per metrics update.
void updateSkinState(Skin& s, const Metrics& m);

// Every metric name the skin reads (texts, values, visibility conditions).
void collectMetricNames(const Skin& s, std::vector<std::string>& out);

// Pure helpers (also used by tests).
bool parseColor(const std::string& text, const std::map<std::string, std::string>& vars, Color& out);
Color pickThreshold(const Widget& w, double v);

}  // namespace qm
