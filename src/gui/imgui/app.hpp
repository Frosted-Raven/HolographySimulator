#pragma once

#include "simulation_model.hpp"
#include "simulation/field_query.hpp"
#include "simulation/inverse_solver.hpp"
#include "world.hpp"
#include "space/grid.hpp"
#include "simulation/simulation_state.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <string>
#include <vector>
#include <chrono>

namespace gui::imgui_app {

class App {
public:
    App();
    ~App();
    void run();

private:
    // ── Window ────────────────────────────────────────────────────────────────
    GLFWwindow* window_  = nullptr;
    int         win_w_   = 1400;
    int         win_h_   = 900;

    // ── Fonts ─────────────────────────────────────────────────────────────────
    ImFont* font_body_  = nullptr;   // JetBrains Mono 13px
    ImFont* font_value_ = nullptr;   // JetBrains Mono 15px

    // ── Model ─────────────────────────────────────────────────────────────────
    simulate::sim_base::simulation_model model_;
    double last_solve_ms_ = 0.0;

    void dispatch(simulate::sim_base::actions a);
    void dispatch_world(world::actions a)              { dispatch(std::move(a)); }
    void dispatch_grid(space::grid::actions a)         { dispatch(world::actions{std::move(a)}); }
    void dispatch_state(simulate::sim_state::actions a){ dispatch(std::move(a)); }
    void run_forward_solver();
    void run_inverse_solver();

    // ── GPU — shaders ─────────────────────────────────────────────────────────
    GLuint line_prog_ = 0;
    GLuint quad_prog_ = 0;

    // ── GPU — FBO (3D scene offscreen target) ─────────────────────────────────
    GLuint fbo_       = 0;
    GLuint fbo_color_ = 0;
    GLuint fbo_depth_ = 0;
    int    fbo_w_     = 0;
    int    fbo_h_     = 0;

    // ── GPU — field slice texture ─────────────────────────────────────────────
    GLuint field_tex_ = 0;

    // ── GPU — geometry ────────────────────────────────────────────────────────
    GLuint floor_vao_    = 0;
    GLuint floor_vbo_    = 0;
    int    floor_vcount_ = 0;

    GLuint floor_major_vao_    = 0;
    GLuint floor_major_vbo_    = 0;
    int    floor_major_vcount_ = 0;

    GLuint axis_vao_  = 0;
    GLuint axis_vbo_  = 0;

    GLuint slice_vao_ = 0;
    GLuint slice_vbo_ = 0;
    GLuint slice_ebo_ = 0;

    GLuint tran_vao_    = 0;
    GLuint tran_vbo_    = 0;
    int    tran_vcount_ = 0;

    GLuint box_vao_  = 0;
    GLuint box_vbo_  = 0;
    GLuint box_ebo_  = 0;

    GLuint obj_vao_    = 0;
    GLuint obj_vbo_    = 0;
    int    obj_vcount_ = 0;

    // ── Camera ────────────────────────────────────────────────────────────────
    struct Camera {
        float azimuth   =  45.0f;
        float elevation =  30.0f;
        float distance  =   1.0f;
        float min_dist  =   0.1f;
        float max_dist  =  10.0f;
    } cam_;

    // ── UI state ──────────────────────────────────────────────────────────────
    bool field_dirty_ = false;
    bool grid_dirty_  = true;

    int  sel_array_   = -1;
    int  sel_tran_    = -1;
    int  sel_obj_     = -1;

    // Section collapse state
    bool sec_scene_open_  = true;
    bool sec_world_open_  = false;
    bool sec_obj_open_    = false;
    bool sec_tran_open_   = true;
    bool sec_solver_open_ = true;
    bool sec_view_open_   = false;

    simulate::field_query::slice_axis slice_axis_ = simulate::field_query::slice_axis::XY;
    int  slice_idx_    = 0;
    int  overlay_mode_ = 0;   // 0=magnitude  1=phase  2=intensity
    int  inv_method_   = 0;   // 0=backprop  1=GS

    bool show_floor_  = true;
    bool show_axes_   = true;

    std::string scene_name_ = "UNNAMED_PROGRAM";

    // Inverse solver
    std::vector<simulate::inverse_solver::target_point> inv_targets_;
    simulate::inverse_solver::params inv_params_;
    int sel_target_ = -1;

    // ── Animations ────────────────────────────────────────────────────────────
    struct Anim {
        float   time      = 0.f;     // wall clock accumulator (seconds)
        float   tl_ctrl   = 0.f;     // traveling light — control panel perimeter position
        float   tl_viz    = 0.f;     // traveling light — viz panel perimeter position
        float   sweep     = -1.f;    // solve sweep: -1 = idle, 0..1 = progress
        bool    sweeping  = false;
        float   valid_flash = 0.f;   // brief flash after solve completes, 0..1 fades
    } anim_;

    // ── Viz drag ──────────────────────────────────────────────────────────────
    bool  viz_hovered_     = false;
    ImVec2 drag_last_      = {};
    bool   dragging_       = false;

    // ── GL resource management ────────────────────────────────────────────────
    void init_gl();
    void rebuild_fbo(int w, int h);
    void rebuild_floor_mesh();
    void rebuild_axis_mesh();
    void rebuild_slice_quad();
    void rebuild_tran_mesh();
    void rebuild_box_mesh();
    void rebuild_obj_mesh();
    void upload_field_slice();

    // ── Render pipeline ───────────────────────────────────────────────────────
    void render_frame(float dt);
    void render_3d_scene(int w, int h);

    // ── ImGui panels ──────────────────────────────────────────────────────────
    void panel_header(float header_h);
    void panel_control();
    void panel_visualization();
    void panel_status(float status_h);

    // Control panel sections
    void section_scene();
    void section_world();
    void section_objects();
    void section_transducers();
    void section_solver();
    void section_view();

    // Section header helper — returns true if expanded
    bool section_header(const char* label, bool& open);

    // Visualization overlays (ImDrawList)
    void draw_field_status(ImDrawList* dl, ImVec2 pos, ImVec2 sz);
    void draw_gizmo(ImDrawList* dl, ImVec2 center);
    void draw_solve_sweep(ImDrawList* dl, ImVec2 pos, ImVec2 sz);

    void handle_keyboard();
};

} // namespace gui::imgui_app
