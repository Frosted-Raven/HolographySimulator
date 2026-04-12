#pragma once

#include "world.hpp"

struct GLFWwindow;

namespace gui {

class App {
public:
    App();
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    void run();

private:
    GLFWwindow*      window_ = nullptr;
    world::world_model model_;

    void dispatch(world::actions a);
    void render_frame();
    void render_sidebar();
    void render_field();

    static void on_glfw_error(int error, const char* desc);
};

} // namespace gui
