
#include "gui/app.hpp"

#include <cstdio>
#include <exception>

int main() {
    try {
        gui::App app;
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
