#include "skin.h"
#include "json.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace qm {

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseColor(const std::string& text, const std::map<std::string, std::string>& vars, Color& out) {
    std::string t = text;
    for (int depth = 0; !t.empty() && t[0] == '$' && depth < 4; ++depth) {   // $var (variables may reference variables)
        auto it = vars.find(t.substr(1));
        if (it == vars.end()) return false;
        t = it->second;
    }
    if (t == "transparent") { out = Color{0, 0, 0, 0}; return true; }
    if (t == "white") { out = Color{255, 255, 255, 255}; return true; }
    if (t == "black") { out = Color{0, 0, 0, 255}; return true; }
    if (t.size() < 2 || t[0] != '#') return false;
    std::string h = t.substr(1);
    for (char c : h) if (hexv(c) < 0) return false;
    auto b2 = [&](size_t i) { return (uint8_t)(hexv(h[i]) * 16 + hexv(h[i + 1])); };
    auto b1 = [&](size_t i) { return (uint8_t)(hexv(h[i]) * 17); };
    if (h.size() == 3) out = Color{b1(0), b1(1), b1(2), 255};
    else if (h.size() == 4) out = Color{b1(0), b1(1), b1(2), b1(3)};
    else if (h.size() == 6) out = Color{b2(0), b2(2), b2(4), 255};
    else if (h.size() == 8) out = Color{b2(0), b2(2), b2(4), b2(6)};
    else return false;
    return true;
}

Color pickThreshold(const Widget& w, double v) {
    Color c = w.color;
    double best = -INFINITY;
    for (const Threshold& t : w.thresholds)
        if (!std::isnan(v) && v >= t.above && t.above >= best) { best = t.above; c = t.color; }
    return c;
}

namespace {

struct Loader {
    Skin& s;
    std::map<std::string, std::string> vars;
    std::string ctx;
    explicit Loader(Skin& sk) : s(sk) {}

    void warn(const std::string& m) { s.warnings.push_back((ctx.empty() ? "" : ctx + ": ") + m); }

    Color color(const Json& j, const std::string& key, Color def) {
        const Json* v = j.find(key);
        if (!v) return def;
        Color c;
        if (v->isStr() && parseColor(v->s, vars, c)) return c;
        warn("bad colour for \"" + key + "\" (use #RRGGBB, #RRGGBBAA or $variable)");
        return def;
    }
    float flt(const Json& j, const std::string& key, float def) { return (float)j.num(key, def); }

    static void substitute(Json& j, int i) {   // $i1 -> i+1, $i -> i inside a repeat template
        if (j.isStr()) {
            auto rep = [&](const std::string& tok, const std::string& val) {
                for (size_t p = 0; (p = j.s.find(tok, p)) != std::string::npos;) {
                    size_t e = p + tok.size();
                    if (e < j.s.size() && (isalnum((unsigned char)j.s[e]) || j.s[e] == '_')) { p = e; continue; }
                    j.s.replace(p, tok.size(), val);
                    p += val.size();
                }
            };
            rep("$i1", std::to_string(i + 1));
            rep("$i", std::to_string(i));
        }
        for (auto& x : j.a) substitute(x, i);
        for (auto& kv : j.o) substitute(kv.second, i);
    }

    HAlign halign(const std::string& v) { return v == "center" ? HAlign::Center : v == "right" ? HAlign::Right : HAlign::Left; }
    VAlign valign(const std::string& v) { return v == "middle" || v == "center" ? VAlign::Middle : v == "bottom" ? VAlign::Bottom : VAlign::Top; }

    void widget(const Json& j, float ox, float oy, std::vector<Widget>& out, int depth) {
        if (!j.isObj()) { warn("a widget must be an object { ... }"); return; }
        if (depth > 6) { warn("widgets nested too deeply"); return; }
        std::string type = j.str("type");
        ctx = "widget '" + type + "'" + (j.find("id") ? " (" + j.str("id") + ")" : "");

        if (type == "group" || type == "repeat") {
            float gx = ox + flt(j, "x", 0), gy = oy + flt(j, "y", 0);
            if (type == "group") {
                const Json* ch = j.find("children");
                if (!ch || !ch->isArr()) { warn("group needs \"children\": [ ... ]"); return; }
                size_t before = out.size();
                for (const Json& c : ch->a) widget(c, gx, gy, out, depth + 1);
                applyVisible(j, out, before);
            } else {
                const Json* tpl = j.find("template");
                int count = (int)j.num("count", 0);
                if (!tpl || count <= 0) { warn("repeat needs \"count\" and \"template\""); return; }
                if (count > 128) { warn("repeat count limited to 128"); count = 128; }
                size_t before = out.size();
                for (int i = 0; i < count; ++i) {
                    Json copy = *tpl;
                    substitute(copy, i);
                    widget(copy, gx + i * flt(j, "dx", 0), gy + i * flt(j, "dy", 0), out, depth + 1);
                }
                applyVisible(j, out, before);
            }
            return;
        }

        Widget w;
        if (type == "text") w.type = WType::Text;
        else if (type == "bar") w.type = WType::Bar;
        else if (type == "graph") w.type = WType::Graph;
        else if (type == "ring") w.type = WType::Ring;
        else if (type == "rect") w.type = WType::Rect;
        else if (type == "image") w.type = WType::Image;
        else if (type == "line") w.type = WType::Line;
        else { warn("unknown widget type (expected text, bar, graph, ring, rect, image, line, group, repeat)"); return; }

        w.x = ox + flt(j, "x", 0); w.y = oy + flt(j, "y", 0); w.w = flt(j, "w", 0); w.h = flt(j, "h", 0);
        w.x2 = ox + flt(j, "x2", 0); w.y2 = oy + flt(j, "y2", 0);
        w.opacity = std::min(1.f, std::max(0.f, flt(j, "opacity", 1)));
        w.radius = flt(j, "radius", 0);
        w.color = color(j, "color", w.type == WType::Text ? Color{232, 235, 245, 255} : w.color);
        w.track = color(j, "track", w.track);
        w.border = color(j, "border", Color{0, 0, 0, 0});
        w.borderWidth = flt(j, "borderWidth", w.border.a ? 1.f : 0.f);
        if (const Json* c2 = j.find("colorTo")) { Color c; if (c2->isStr() && parseColor(c2->s, vars, c)) { w.color2 = c; w.hasColor2 = true; } else warn("bad colour for \"colorTo\""); }

        if (const Json* th = j.find("colors")) {
            if (th->isArr()) for (const Json& t : th->a) {
                Color c;
                if (t.isObj() && t.find("above") && parseColor(t.str("color"), vars, c)) w.thresholds.push_back({t.num("above"), c});
                else warn("each entry of \"colors\" needs {\"above\": number, \"color\": \"#...\"}");
            } else warn("\"colors\" must be a list");
        }

        std::string err;
        if (const Json* v = j.find("visible")) {
            std::string src = v->isStr() ? v->s : (v->type == Json::Bool ? (v->b ? "1" : "0") : "1");
            if (w.visible.parse(src, err)) w.hasVisible = true; else warn("bad \"visible\" expression: " + err);
        }
        if (w.type == WType::Text) {
            w.font = j.str("font", "main");
            w.align = halign(j.str("align", "left"));
            w.valign = valign(j.str("valign", "top"));
            if (!w.text.parse(j.str("text", ""), err)) { warn(err); w.text.parse("", err); }
        }
        if (w.type == WType::Bar || w.type == WType::Graph || w.type == WType::Ring) {
            std::string src = j.find("value") && j.find("value")->isNum() ? std::to_string(j.num("value")) : j.str("value");
            if (src.empty()) { warn("needs a \"value\" (a metric name or an expression, e.g. \"cpu.load\")"); return; }
            if (!w.value.parse(src, err)) { warn("bad \"value\" expression '" + src + "': " + err); return; }
            w.min = j.num("min", 0); w.max = j.num("max", 100);
            if (w.max == w.min) { warn("\"min\" and \"max\" are equal"); w.max = w.min + 1; }
            w.vertical = j.boolean("vertical", false);
            w.history = std::max(2, std::min(600, (int)j.num("history", 60)));
            w.fill = j.boolean("fill", true);
            w.lineWidth = flt(j, "lineWidth", 2);
            w.thickness = flt(j, "thickness", 8);
            w.startAngle = flt(j, "startAngle", 135);
            w.sweep = flt(j, "sweep", 270);
        }
        if (w.type == WType::Image) { w.file = j.str("file"); w.fit = j.str("fit", "contain"); if (w.file.empty()) { warn("image needs \"file\""); return; } }
        if ((w.type == WType::Bar || w.type == WType::Graph || w.type == WType::Rect || w.type == WType::Image) && (w.w <= 0 || w.h <= 0)) { warn("needs positive \"w\" and \"h\""); return; }
        out.push_back(std::move(w));
    }

    void applyVisible(const Json& j, std::vector<Widget>& out, size_t from) {
        const Json* v = j.find("visible");
        if (!v || !v->isStr()) return;
        for (size_t i = from; i < out.size(); ++i) {   // a group's condition is ANDed into each child
            std::string err;
            Expr combined;
            std::string src = out[i].hasVisible ? "(" + v->s + ") && (" + out[i].visible.source() + ")" : v->s;
            if (combined.parse(src, err)) { out[i].visible = combined; out[i].hasVisible = true; } else warn("bad group \"visible\": " + err);
        }
    }
};

}  // namespace

bool loadSkin(const std::wstring& dir, Skin& out, std::string& fatal) {
    std::string text;
    std::wstring path = joinPath(dir, L"skin.json");
    if (!readFile(path, text, 1024 * 1024)) { fatal = "cannot read " + narrow(path); return false; }
    Json root;
    std::string err;
    if (!parseJson(text, root, err)) { fatal = "skin.json: " + err; return false; }
    if (!root.isObj()) { fatal = "skin.json must contain an object { ... }"; return false; }

    Skin s;
    s.dir = dir;
    Loader L(s);
    s.name = root.str("name", "Unnamed");
    s.author = root.str("author");

    if (const Json* v = root.find("vars")) if (v->isObj()) for (auto& kv : v->o) if (kv.second.isStr()) L.vars[kv.first] = kv.second.s;

    if (const Json* w = root.find("window")) if (w->isObj()) {
        L.ctx = "window";
        s.width = std::max(40.f, std::min(3000.f, (float)w->num("width", s.width)));
        s.height = std::max(24.f, std::min(3000.f, (float)w->num("height", s.height)));
        s.anchor = w->str("anchor", s.anchor);
        if (const Json* m = w->find("margin")) {
            if (m->isArr() && m->a.size() >= 2 && m->a[0].isNum() && m->a[1].isNum()) { s.marginX = (float)m->a[0].n; s.marginY = (float)m->a[1].n; }
            else if (m->isNum()) s.marginX = s.marginY = (float)m->n;
        }
        s.monitor = (int)w->num("monitor", 0);
        s.opacity = std::min(1.f, std::max(0.1f, (float)w->num("opacity", 1)));
        s.topmost = w->boolean("topmost", true);
        s.clickThrough = w->boolean("clickThrough", true);
        s.scale = std::min(4.f, std::max(0.5f, (float)w->num("scale", 1)));
    }
    s.updateMs = std::max(250, std::min(10000, (int)root.num("update", 1000)));

    if (const Json* bg = root.find("background")) if (bg->isObj()) {
        L.ctx = "background";
        s.bgColor = L.color(*bg, "color", s.bgColor);
        if (const Json* g = bg->find("gradient")) if (g->isObj()) {
            s.bgColor = L.color(*g, "from", s.bgColor);
            s.bgGradientTo = L.color(*g, "to", s.bgColor);
            s.bgAngle = (float)g->num("angle", 90);
            s.bgGradient = true;
        }
        s.bgImage = bg->str("image");
        s.bgFit = bg->str("fit", "cover");
        s.bgImageOpacity = std::min(1.f, std::max(0.f, (float)bg->num("imageOpacity", 1)));
        s.bgRadius = std::max(0.f, (float)bg->num("radius", s.bgRadius));
        if (const Json* b = bg->find("border")) if (b->isObj()) { s.bgBorder = L.color(*b, "color", s.bgBorder); s.bgBorderWidth = (float)b->num("width", 1); }
    }

    s.fonts["main"] = FontSpec();
    if (const Json* f = root.find("fonts")) if (f->isObj()) for (auto& kv : f->o) if (kv.second.isObj()) {
        FontSpec fs;
        fs.family = kv.second.str("family", fs.family);
        fs.size = std::max(4.f, std::min(300.f, (float)kv.second.num("size", fs.size)));
        fs.bold = kv.second.boolean("bold", false);
        fs.italic = kv.second.boolean("italic", false);
        s.fonts[kv.first] = fs;
    }

    const Json* ws = root.find("widgets");
    if (!ws || !ws->isArr()) { fatal = "skin.json needs a \"widgets\": [ ... ] list"; return false; }
    if (ws->a.size() > 500) { fatal = "too many widgets (limit 500)"; return false; }
    for (const Json& w : ws->a) L.widget(w, 0, 0, s.widgets, 0);
    if (s.widgets.size() > 1500) { fatal = "too many widgets after expanding repeat blocks (limit 1500)"; return false; }
    for (Widget& w : s.widgets) if (w.type == WType::Text && s.fonts.find(w.font) == s.fonts.end()) {
        s.warnings.push_back("text uses unknown font '" + w.font + "' (declare it under \"fonts\"); using \"main\"");
        w.font = "main";
    }
    out = std::move(s);
    return true;
}

void updateSkinState(Skin& s, const Metrics& m) {
    for (Widget& w : s.widgets) {
        if (w.type != WType::Graph) continue;
        double v = w.value.eval(m);
        w.hist.push_back(std::isnan(v) ? NAN : (float)v);
        while ((int)w.hist.size() > w.history) w.hist.erase(w.hist.begin());
    }
}

}  // namespace qm
