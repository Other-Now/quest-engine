#include <cstring>
#include <exception>

#include "check.hpp"

namespace qt {
std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
int failures = 0, checks = 0;
}  // namespace qt

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& c : qt::cases()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = qt::failures;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++qt::failures;
            std::printf("  EXCEPTION %s\n", e.what());
        }
        std::printf("%s %s\n", qt::failures == before ? "ok  " : "FAIL", c.name);
        ++run;
    }
    std::printf("%d tests, %d checks, %d failures\n", run, qt::checks, qt::failures);
    return qt::failures ? 1 : 0;
}
