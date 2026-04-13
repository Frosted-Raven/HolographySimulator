#include "gui/ftxui/app.hpp"
#include <cstdlib>
#include <stdexcept>
#include <cstdio>

int main() {
    try {
        gui::ftxui_app::App app;
        app.run();
    } catch(const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
