// Developer tool: renders a skin to a PNG using LIVE metrics from this PC (also used for the screenshots in the docs).
//   render_skin <skin folder> <out.png> [scale] [samples]
#include "../src/render.h"
#include "../src/samplers.h"
#include "../src/util.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: render_skin <skin folder> <out.png> [scale=1] [samples=40]\n"); return 2; }
    std::wstring dir = qm::widen(argv[1]), out = qm::widen(argv[2]);
    float scale = argc > 3 ? (float)atof(argv[3]) : 1.f;
    int samples = argc > 4 ? atoi(argv[4]) : 40;

    qm::Skin skin;
    std::string err;
    if (!qm::loadSkin(dir, skin, err)) { printf("ERROR: %s\n", err.c_str()); return 1; }
    for (auto& w : skin.warnings) printf("warning: %s\n", w.c_str());

    qm::Renderer r;
    auto samplers = qm::createSamplers();
    qm::Metrics m;
    for (int i = 0; i < samples; ++i) {
        for (auto& s : samplers) s->sample(m, 0.25);
        qm::updateSkinState(skin, m);
        Sleep(120);
    }
    if (!r.drawToPng(out, skin, m, scale)) { printf("ERROR: could not write PNG\n"); return 1; }
    printf("rendered '%s' (%d widgets) -> %s\n", skin.name.c_str(), (int)skin.widgets.size(), argv[2]);
    return 0;
}
