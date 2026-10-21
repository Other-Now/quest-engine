// The one shared game-state store: named integer variables.
//
// Everything that matters for progression lives here: flags (gate_open),
// counters, and inventory (has_<item> = 1). C++ rules, Ink dialogue, the save
// file, the replay hash and the puzzle solver all read and write this same
// store, which is why the solver's answer applies to the shipped game.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace qe {

class Store {
public:
    // Adds a variable (or returns the existing id; the first initial wins).
    int define(const std::string& name, int initial = 0);
    int id(const std::string& name) const;  // -1 if unknown

    int get(int id) const { return values_[size_t(id)]; }
    void set(int id, int v) { values_[size_t(id)] = v; }
    int get(const std::string& name) const;  // throws on unknown
    void set(const std::string& name, int v);

    size_t size() const { return values_.size(); }
    const std::string& name(int id) const { return names_[size_t(id)]; }
    const std::vector<int>& values() const { return values_; }
    void setValues(const std::vector<int>& v) { values_ = v; }
    void resetToDefaults() { values_ = defaults_; }
    int defaultValue(int id) const { return defaults_[size_t(id)]; }

    uint64_t hash() const;

private:
    std::vector<std::string> names_;
    std::vector<int> values_, defaults_;
    std::unordered_map<std::string, int> index_;
};

// FNV-1a, used for state hashes and content hashes.
struct Fnv1a {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    }
    template <class T>
    void pod(const T& v) { bytes(&v, sizeof v); }
    void str(const std::string& s) { pod(uint64_t(s.size())); bytes(s.data(), s.size()); }
};

}  // namespace qe
