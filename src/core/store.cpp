#include "core/store.hpp"

#include <stdexcept>

namespace qe {

int Store::define(const std::string& name, int initial) {
    auto it = index_.find(name);
    if (it != index_.end()) return it->second;
    int id = int(names_.size());
    names_.push_back(name);
    values_.push_back(initial);
    defaults_.push_back(initial);
    index_.emplace(name, id);
    return id;
}

int Store::id(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? -1 : it->second;
}

int Store::get(const std::string& name) const {
    int i = id(name);
    if (i < 0) throw std::runtime_error("unknown variable: " + name);
    return get(i);
}

void Store::set(const std::string& name, int v) {
    int i = id(name);
    if (i < 0) throw std::runtime_error("unknown variable: " + name);
    set(i, v);
}

uint64_t Store::hash() const {
    Fnv1a f;
    for (int v : values_) f.pod(int32_t(v));
    return f.h;
}

}  // namespace qe
