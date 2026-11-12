// Expressions used in skins: metric names, numbers, + - * / %, comparisons, && || !, and functions
// min max clamp abs round floor ceil if(cond,a,b). Parsed once at skin load, evaluated each frame.
// Text templates: "CPU {cpu.load:0}%  {net.down:rate}"  ({{ and }} are literal braces).
#pragma once
#include "metrics.h"
#include <memory>
#include <string>
#include <vector>

namespace qm {

class Expr {
public:
    bool parse(const std::string& text, std::string& err);
    double eval(const Metrics& m) const;     // NaN when a metric is missing
    bool valid() const { return !nodes_.empty(); }
    const std::string& source() const { return src_; }
    void collectNames(std::vector<std::string>& out) const;   // metric names this expression reads

    struct Node { int op = 0; double num = 0; std::string name; int a = -1, b = -1, c = -1; std::vector<int> args; };

private:
    friend struct ExprParser;
    std::vector<Node> nodes_;
    int root_ = -1;
    std::string src_;
    double evalNode(int idx, const Metrics& m, int depth) const;
};

std::string formatNumber(double v, const std::string& fmt);

class Template {
public:
    bool parse(const std::string& text, std::string& err);
    std::string render(const Metrics& m) const;
    bool hasDynamic() const { return dynamic_; }
    void collectNames(std::vector<std::string>& out) const;

private:
    struct Seg { std::string lit; Expr expr; std::string fmt; std::string bareName; bool isExpr = false; };
    std::vector<Seg> segs_;
    bool dynamic_ = false;
};

}  // namespace qm
