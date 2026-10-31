// Documentation must not rot: every ```json skin / ```json widgets block in docs/CUSTOMIZE.md is loaded by the real
// skin loader and must produce no error and no warning.
#include "../src/skin.h"
#include "../src/util.h"
#include <windows.h>
#include <cstdio>
#include <string>

static int g_run = 0, g_fail = 0;
using namespace qm;

int main(int argc, char** argv) {
    std::string docPath = argc > 1 ? argv[1] : "docs\\CUSTOMIZE.md";
    std::string text;
    if (!readFile(widen(docPath), text)) { printf("cannot read %s\n", docPath.c_str()); return 2; }

    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    std::wstring dir = std::wstring(t) + L"qm_docs_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);

    size_t pos = 0; int blocks = 0;
    while ((pos = text.find("```json", pos)) != std::string::npos) {
        size_t eol = text.find('\n', pos);
        std::string tag = text.substr(pos + 7, eol - pos - 7);
        while (!tag.empty() && (tag.back() == '\r' || tag.back() == ' ')) tag.pop_back();
        while (!tag.empty() && tag[0] == ' ') tag.erase(0, 1);
        size_t end = text.find("```", eol);
        if (end == std::string::npos) break;
        std::string body = text.substr(eol + 1, end - eol - 1);
        pos = end + 3;
        if (tag != "skin" && tag != "widgets") continue;     // plain ```json blocks (settings file) are not skins
        ++blocks;
        std::string json = tag == "widgets" ? "{ \"name\": \"snippet\", \"window\": { \"width\": 400, \"height\": 300 }, \"widgets\": " + body + " }" : body;
        writeFileAtomic(joinPath(dir, L"skin.json"), json);
        Skin s; std::string err;
        ++g_run;
        bool ok = loadSkin(dir, s, err);
        if (!ok) { ++g_fail; printf("FAIL block #%d (%s): %s\n--- %s\n", blocks, tag.c_str(), err.c_str(), body.substr(0, 200).c_str()); continue; }
        if (!s.warnings.empty()) { ++g_fail; printf("FAIL block #%d has warnings: %s\n", blocks, s.warnings[0].c_str()); continue; }
        if (tag == "skin" && s.widgets.empty() && body.find("\"widgets\": []") == std::string::npos) { ++g_fail; printf("FAIL block #%d loaded zero widgets\n", blocks); }
    }
    printf("%d doc snippets checked, %d failed\n", g_run, g_fail);
    if (g_run < 15) { printf("suspiciously few snippets found\n"); return 1; }
    return g_fail ? 1 : 0;
}
