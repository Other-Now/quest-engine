// A minimal test harness: TEST(name) { CHECK(...); }
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace qt {
struct Case {
    const char* name;
    std::function<void()> fn;
};
std::vector<Case>& cases();
extern int failures, checks;
struct Reg {
    Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};
inline std::string data(const std::string& rel) { return std::string(QE_SOURCE_DIR) + "/" + rel; }
}  // namespace qt

#define QT_CAT2(a, b) a##b
#define QT_CAT(a, b) QT_CAT2(a, b)
#define TEST(name)                                                        \
    static void QT_CAT(test_, __LINE__)();                                \
    static qt::Reg QT_CAT(reg_, __LINE__)(name, QT_CAT(test_, __LINE__)); \
    static void QT_CAT(test_, __LINE__)()
#define CHECK(...)                                                            \
    do {                                                                      \
        ++qt::checks;                                                         \
        if (!(__VA_ARGS__)) {                                                 \
            ++qt::failures;                                                   \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
        }                                                                     \
    } while (0)
