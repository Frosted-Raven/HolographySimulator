#include "gui/app.hpp"
#include "gui/theme.hpp"
#include "gui/panels/grid_panel.hpp"
#include "gui/panels/object_panel.hpp"
#include "gui/panels/transducer_panel.hpp"
#include "gui/panels/field_panel.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <stdexcept>
#include <cstdio>

namespace gui {

static constexpr int   WIN_W     = 1600;
static constexpr int   WIN_H     = 900;
static constexpr float SIDEBAR_W = 330.0f;

// ---------------------------------------------------------------------------

void App::on_glfw_error(int error, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

App::App() {
    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(WIN_W, WIN_H, "HOLOGRAPHY SIM", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        throw std::runtime_error("gladLoadGLLoader failed");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr; // disable imgui.ini

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    theme::apply();

    // Sensible default world state
    model_.grid.grid      = {64, 64, 64};
    model_.grid.cell_size = 0.003; // 3 mm — λ/10 at 40 kHz in air
}

App::~App() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::dispatch(world::actions a) {
    model_ = world::update(std::move(model_), std::move(a));
}

// ---------------------------------------------------------------------------

void App::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_frame();

        ImGui::Render();

        int fb_w, fb_h;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.02f, 0.02f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }
}

// ---------------------------------------------------------------------------

void App::render_frame() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    // ---- Sidebar ----
    ImGui::SetNextWindowPos ({0.0f, 0.0f});
    ImGui::SetNextWindowSize({SIDEBAR_W, display.y});

    constexpr ImGuiWindowFlags sidebar_flags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_Border, theme::color::border_bright);
    ImGui::Begin("##sidebar", nullptr, sidebar_flags);
    ImGui::PopStyleColor();
    render_sidebar();
    ImGui::End();

    // ---- Field view ----
    ImGui::SetNextWindowPos ({SIDEBAR_W, 0.0f});
    ImGui::SetNextWindowSize({display.x - SIDEBAR_W, display.y});

    constexpr ImGuiWindowFlags field_flags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar   | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##field_win", nullptr, field_flags);
    render_field();
    ImGui::End();
}

void App::render_sidebar() {
    // Title block
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::cyan_bright);
    ImGui::SetWindowFontScale(1.25f);
    ImGui::Text("HOLOGRAPHY SIM");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    ImGui::Text("acoustic field engine  v0.1");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, theme::color::cyan_dim);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Build the dispatch lambda once and share across panels
    auto d = [this](world::actions a){ dispatch(std::move(a)); };

    // Tabbed panels
    ImGui::PushStyleColor(ImGuiCol_Tab,        {0.02f, 0.10f, 0.18f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_TabActive,  {0.00f, 0.40f, 0.60f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_TabHovered, {0.00f, 0.52f, 0.74f, 0.80f});

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("GRID")) {
            ImGui::Spacing();
            panels::render_grid_panel(model_.grid, d);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("OBJECTS")) {
            ImGui::Spacing();
            panels::render_object_panel(model_.grid, d);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("TRANSDUCERS")) {
            ImGui::Spacing();
            panels::render_transducer_panel(model_.transducers, d);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(3);
}

void App::render_field() {
    panels::render_field_panel(model_.grid);
}

} // namespace gui
