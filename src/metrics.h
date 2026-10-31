// Metric registry: flat name -> number or string. Samplers write it, skins read it through expressions.
#pragma once
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace qm {

class Metrics {
public:
    void set(const std::string& k, double v) { num_[k] = v; }
    void setStr(const std::string& k, std::string v) { str_[k] = std::move(v); }
    // NaN when the metric does not exist or is unavailable (a skin shows "n/a" for it).
    double get(const std::string& k) const {
        auto it = num_.find(k);
        return it == num_.end() ? NAN : it->second;
    }
    const std::string* getStr(const std::string& k) const {
        auto it = str_.find(k);
        return it == str_.end() ? nullptr : &it->second;
    }
    bool has(const std::string& k) const { return num_.count(k) || str_.count(k); }
    std::vector<std::string> names() const {
        std::vector<std::string> v;
        for (auto& kv : num_) v.push_back(kv.first);
        for (auto& kv : str_) v.push_back(kv.first);
        return v;
    }
    void clear() { num_.clear(); str_.clear(); }

private:
    std::unordered_map<std::string, double> num_;
    std::unordered_map<std::string, std::string> str_;
};

}  // namespace qm
