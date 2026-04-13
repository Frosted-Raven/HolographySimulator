#include "gui/ftxui/app.hpp"

#include "space/object/object_descriptor.hpp"
#include "space/object/object.hpp"
#include "transducer/transducer.hpp"
#include "transducer/transducer_array.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

using namespace ftxui;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <variant>

namespace gui::ftxui_app {

// ── Palette helpers ───────────────────────────────────────────────────────────

static const Color VOID_C            = Color::RGB(0x00, 0x00, 0x00);
static const Color GRID_DARK_C   = Color::RGB(0x05, 0x0A, 0x0F);
static const Color LINE_ACTIVE_C = Color::RGB(0x00, 0xC8, 0xFF);
static const Color LINE_BRIGHT_C = Color::RGB(0x40, 0xDF, 0xFF);
static const Color LINE_DIM_C    = Color::RGB(0x0D, 0x20, 0x35);
static const Color TEXT_PRIMARY_C   = Color::RGB(0xC0, 0xE8, 0xF8);
static const Color TEXT_SECONDARY_C = Color::RGB(0x50, 0x90, 0xB0);
static const Color TEXT_GHOST_C     = Color::RGB(0x1A, 0x40, 0x60);
static const Color TEXT_BRIGHT_C    = Color::RGB(0xFF, 0xFF, 0xFF);
static const Color STATUS_VALID_C   = Color::RGB(0x00, 0xFF, 0x9F);
static const Color STATUS_STALE_C   = Color::RGB(0xFF, 0x8C, 0x00);
static const Color STATUS_DEAD_C    = Color::RGB(0xFF, 0x22, 0x44);
static const Color AXIS_X_C         = Color::RGB(0x00, 0xC8, 0xFF);
static const Color AXIS_Y_C         = Color::RGB(0xFF, 0x8C, 0x00);
static const Color AXIS_Z_C         = Color::RGB(0xFF, 0xFF, 0xFF);

// ── Pressure field characters ─────────────────────────────────────────────────
// Map magnitude 0→1 to visual density characters

static const wchar_t DENSITY_CHARS[] = {L' ', L'·', L'░', L'▒', L'▓', L'█'};
static constexpr int N_DENSITY = 6;

// Map t ∈ [0,1] to a grid color
static ftxui::Color colormap_mag(double t) {
    struct RGB { uint8_t r,g,b; };
    static const RGB stops[9] = {
        {0x00, 0x00, 0x00},
        {0x00, 0x15, 0x20},
        {0x00, 0x30, 0x60},
        {0x00, 0x58, 0x99},
        {0x00, 0x88, 0xCC},
        {0x00, 0xAA, 0xEE},
        {0x00, 0xCC, 0xFF},
        {0x80, 0xEE, 0xFF},
        {0xFF, 0xFF, 0xFF},
    };
    static const float pos[9] = {0.f,0.10f,0.25f,0.40f,0.55f,0.70f,0.85f,0.95f,1.f};
    t = std::clamp(t, 0.0, 1.0);
    float ft = (float)t;
    int i = 0;
    for(; i < 7; ++i) if(pos[i+1] >= ft) break;
    float sp = pos[i+1] - pos[i];
    float f  = (sp > 0) ? (ft - pos[i]) / sp : 0.f;
    auto& a = stops[i]; auto& b = stops[i+1];
    return Color::RGB(uint8_t(a.r + f*(b.r-a.r)),
                  uint8_t(a.g + f*(b.g-a.g)),
                  uint8_t(a.b + f*(b.b-a.b)));
}

// ── Constructor ───────────────────────────────────────────────────────────────

App::App() {
    auto& g = model_.world.grid;
    g.grid      = {64, 64, 32};
    g.cell_size = 0.005;
    g.default_medium.density            = 1.21;
    g.default_medium.stiffness          = 142000.0;
    g.default_medium.sound_speed        = 343.0;
    g.default_medium.acoustic_impedance = 415.0;
    g.default_medium.is_rigid           = false;

    slice_idx_ = g.grid.z / 2;
    inv_params_.gs_iterations = 100;
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

void App::dispatch(simulate::sim_base::actions a) {
    model_ = simulate::sim_base::update(model_, std::move(a));
    if(!std::holds_alternative<simulate::sim_state::actions>(a)) {
        if(model_.state.current == simulate::sim_state::VALID)
            model_.state.current = simulate::sim_state::OLD;
    }
}

void App::run_forward_solver() {
    auto t0 = std::chrono::steady_clock::now();
    dispatch_state(simulate::sim_state::action::run_solver{model_.world});
    auto t1 = std::chrono::steady_clock::now();
    last_solve_ms_ = std::chrono::duration<double,std::milli>(t1-t0).count();
}

void App::run_inverse_solver() {
    if(inv_targets_.empty()) return;
    inv_params_.solver_method = (inv_method_ == 0)
        ? simulate::inverse_solver::method::backprop
        : simulate::inverse_solver::method::gerchberg_saxton;
    auto res = simulate::inverse_solver::solve(model_.world, inv_targets_, inv_params_);
    for(size_t ai = 0; ai < res.phases.size(); ++ai)
        for(size_t ti = 0; ti < res.phases[ai].size(); ++ti)
            dispatch_world(world::action::mod_array{
                ai, transducer::tran_array::action::single_adjust{
                    (int)ti,
                    transducer::single::action::new_phase{res.phases[ai][ti]}}});
}

// ── Build pressure display ────────────────────────────────────────────────────
// Returns an ftxui::Element grid representing the pressure slice

static ftxui::Element build_field_display(
    const simulate::sim_base::simulation_model& model,
    simulate::field_query::slice_axis axis,
    int idx, int overlay_mode,
    int display_w, int display_h)
{
    if(model.state.current == simulate::sim_state::UNCOMPUTED) {
        // Placeholder — dim grid pattern
        std::vector<Element> rows;
        for(int r = 0; r < display_h; ++r) {
            std::wstring line(display_w, L'·');
            rows.push_back(text(std::string(line.begin(), line.end()))
                | color(TEXT_GHOST_C));
        }
        return vbox(std::move(rows));
    }

    const auto& g  = model.world.grid;
    auto sl = simulate::field_query::extract_slice(
        model.state, g.grid, axis, (uint16_t)idx);

    std::vector<double> values;
    if(overlay_mode == 0)      values = simulate::field_query::magnitude(sl);
    else if(overlay_mode == 1) values = simulate::field_query::phase(sl);
    else                       values = simulate::field_query::intensity(sl);

    double vmax = 0;
    for(auto v : values) vmax = std::max(vmax, std::abs(v));
    if(vmax < 1e-12) vmax = 1.0;

    // Sample the slice into display_w × display_h
    std::vector<Element> rows;
    for(int r = 0; r < display_h; ++r) {
        int sy = (int)(r * (double)sl.height / display_h);
        sy = std::clamp(sy, 0, (int)sl.height - 1);

        std::vector<Element> cells;
        for(int c = 0; c < display_w; ++c) {
            int sx = (int)(c * (double)sl.width / display_w);
            sx = std::clamp(sx, 0, (int)sl.width - 1);

            double v = values[(size_t)(sy * sl.width + sx)];
            double t = std::clamp(std::abs(v) / vmax, 0.0, 1.0);

            int ci = (int)(t * (N_DENSITY - 1) + 0.5);
            ci = std::clamp(ci, 0, N_DENSITY - 1);
            wchar_t ch = DENSITY_CHARS[ci];

            ftxui::Color col = colormap_mag(t);
            cells.push_back(text(std::string(1, (char)ch)) | color(col));
        }
        rows.push_back(hbox(std::move(cells)));
    }
    return vbox(std::move(rows));
}

// ── Orientation indicator ─────────────────────────────────────────────────────

static std::string orientation_str(float az, float el) {
    // Describe axis directions as arrows based on camera orientation
    char buf[64];
    // Simplified: just show current azimuth/elevation
    snprintf(buf, sizeof(buf), "AZ:%.0f° EL:%.0f°", az, el);
    return buf;
}

// ── Main run ──────────────────────────────────────────────────────────────────

void App::run() {
    auto screen = ScreenInteractive::Fullscreen();

    // ── Full renderer (no interactive children — all input handled by CatchEvent)
    auto renderer = Renderer([&]() -> Element {

        const auto& g = model_.world.grid;
        auto status   = model_.state.current;

        // ── Header ────────────────────────────────────────────────────────────
        Element status_el;
        if(status == simulate::sim_state::VALID)
            status_el = text("● VALID")   | color(STATUS_VALID_C);
        else if(status == simulate::sim_state::OLD)
            status_el = text("● STALE")   | color(STATUS_STALE_C);
        else
            status_el = text("○ UNCOMPUTED") | color(TEXT_GHOST_C);

        auto header = hbox({
            text("ACOUSTIC HOLOGRAPHY ENGINE") | color(TEXT_PRIMARY_C) | bold,
            text("  "),
            text(scene_name_.empty() ? "UNNAMED_PROGRAM" : scene_name_) | color(TEXT_SECONDARY_C),
            filler(),
            status_el,
            text("  "),
            text("[ SOLVE ]") | color(LINE_BRIGHT_C) | bold,
        }) | color(LINE_ACTIVE_C);

        // ── Control panel ─────────────────────────────────────────────────────
        std::vector<Element> ctrl_items;

        // ── SCENE section ─────────────────────────────────────────────────────
        if(sec_scene_open_) {
            ctrl_items.push_back(
                text("▼ SCENE") | color(TEXT_SECONDARY_C)
            );
            ctrl_items.push_back(
                hbox({text("  "), text(scene_name_)}) | color(TEXT_PRIMARY_C)
            );
            ctrl_items.push_back(separator() | color(LINE_DIM_C));
        } else {
            ctrl_items.push_back(text("▶ SCENE") | color(TEXT_SECONDARY_C));
        }

        // ── TRANSDUCERS section ───────────────────────────────────────────────
        if(sec_tran_open_) {
            ctrl_items.push_back(text("▼ TRANSDUCERS") | color(TEXT_SECONDARY_C));

            for(size_t ai = 0; ai < model_.world.transducers.size(); ++ai) {
                const auto& arr = model_.world.transducers[ai];
                std::string aname = arr.name.value_or("ARRAY_" + std::to_string(ai));
                bool asel = (sel_array_ == (int)ai);
                ctrl_items.push_back(
                    hbox({
                        text("  ● ") | color(asel ? LINE_ACTIVE_C : TEXT_GHOST_C),
                        text(aname)  | color(asel ? TEXT_BRIGHT_C : TEXT_PRIMARY_C),
                        text("  "),
                        text(std::to_string(arr.tran_array.size()) + " t") | color(TEXT_SECONDARY_C),
                    })
                );
                if(asel) {
                    for(size_t ti = 0; ti < arr.tran_array.size(); ++ti) {
                        const auto& t = arr.tran_array[ti];
                        bool tsel = (sel_tran_ == (int)ti);
                        char buf[64];
                        snprintf(buf, sizeof(buf), "  φ:%.3f A:%.2f %s",
                                 t.phase, t.amplitude, t.is_active ? "✓" : "✗");
                        ctrl_items.push_back(
                            hbox({
                                text("    ") | color(tsel ? LINE_ACTIVE_C : TEXT_GHOST_C),
                                text(t.name.value_or("T_" + std::to_string(ti)))
                                    | color(tsel ? TEXT_BRIGHT_C : TEXT_SECONDARY_C),
                                text(buf) | color(TEXT_SECONDARY_C),
                            })
                        );
                    }
                }
            }
            ctrl_items.push_back(separator() | color(LINE_DIM_C));
        } else {
            ctrl_items.push_back(text("▶ TRANSDUCERS") | color(TEXT_SECONDARY_C));
        }

        // ── SOLVER section ────────────────────────────────────────────────────
        if(sec_solver_open_) {
            ctrl_items.push_back(text("▼ SOLVER") | color(TEXT_SECONDARY_C));

            // Forward freq
            double freq = 40000.0;
            if(!model_.world.transducers.empty() && !model_.world.transducers[0].tran_array.empty())
                freq = model_.world.transducers[0].tran_array[0].frequency;
            char fbuf[32]; snprintf(fbuf, sizeof(fbuf), "FREQ: %.0f Hz", freq);
            ctrl_items.push_back(text(fbuf) | color(TEXT_PRIMARY_C));

            ctrl_items.push_back(
                text("[ SOLVE ]") | color(LINE_BRIGHT_C) | bold | center
            );
            ctrl_items.push_back(separator() | color(LINE_DIM_C));
        } else {
            ctrl_items.push_back(text("▶ SOLVER") | color(TEXT_SECONDARY_C));
        }

        // Solve time
        if(last_solve_ms_ > 0.0) {
            char tbuf[32]; snprintf(tbuf, sizeof(tbuf), "%.2fms", last_solve_ms_);
            ctrl_items.push_back(text(tbuf) | color(TEXT_GHOST_C));
        }

        auto ctrl_panel = vbox(std::move(ctrl_items))
            | color(TEXT_PRIMARY_C);

        // ── Pressure field display ─────────────────────────────────────────────
        // Use a fixed display size; terminal will constrain it
        int disp_w = 60, disp_h = 28;
        Element field_el = build_field_display(
            model_, slice_axis_, slice_idx_, overlay_mode_, disp_w, disp_h);

        // Axis indicator
        std::string orient = orientation_str(cam_azimuth_, cam_elevation_);
        const char* axis_labels[] = {"SLICE:XY", "SLICE:XZ", "SLICE:YZ"};
        char slice_info[32];
        snprintf(slice_info, sizeof(slice_info), "%s  Z=%d",
                 axis_labels[(int)slice_axis_], slice_idx_);

        auto viz_panel = vbox({
            field_el,
            hbox({
                text("X→ Y↑ Z↗") | color(TEXT_SECONDARY_C),
                text("  "),
                text(slice_info) | color(TEXT_SECONDARY_C),
                text("  "),
                text(orient) | color(TEXT_GHOST_C),
            }),
        }) | flex;

        // ── Status bar ────────────────────────────────────────────────────────
        size_t total_t = 0;
        for(const auto& a : model_.world.transducers) total_t += a.tran_array.size();

        char sbuf[128];
        snprintf(sbuf, sizeof(sbuf),
                 "GRID: %u×%u×%u │ CELL: %.4fm │ TRANS: %zu │ FIELD: %s",
                 g.grid.x, g.grid.y, g.grid.z, g.cell_size, total_t,
                 status==simulate::sim_state::VALID   ? "VALID" :
                 status==simulate::sim_state::OLD     ? "STALE" : "UNCOMPUTED");

        Color status_bar_col =
            (status==simulate::sim_state::VALID) ? STATUS_VALID_C :
            (status==simulate::sim_state::OLD)   ? STATUS_STALE_C : TEXT_GHOST_C;
        auto status_bar = color(status_bar_col, text(sbuf));

        // ── Full layout ───────────────────────────────────────────────────────
        return vbox({
            header | borderHeavy | color(LINE_ACTIVE_C),
            hbox({
                vbox({ctrl_panel}) | border | color(LINE_DIM_C) | size(WIDTH, EQUAL, 30),
                viz_panel | border | color(LINE_DIM_C),
            }) | flex,
            status_bar | border | color(LINE_DIM_C),
        });
    });

    // ── Event handling ────────────────────────────────────────────────────────
    auto event_handler = CatchEvent(renderer, [&](Event event) -> bool {
        // s — solve
        if(event == Event::Character('s') || event == Event::Character('S')) {
            run_forward_solver();
            return true;
        }
        // i — run inverse
        if(event == Event::Character('i') || event == Event::Character('I')) {
            run_inverse_solver();
            return true;
        }
        // f — reset camera
        if(event == Event::Character('f') || event == Event::Character('F')) {
            cam_azimuth_ = 45.f; cam_elevation_ = 30.f;
            return true;
        }
        // x/y/z — snap views
        if(event == Event::Character('x')) { cam_azimuth_=0.f;  cam_elevation_=0.f;  return true; }
        if(event == Event::Character('y')) { cam_azimuth_=90.f; cam_elevation_=0.f;  return true; }
        if(event == Event::Character('z')) { cam_azimuth_=45.f; cam_elevation_=89.f; return true; }
        // a/d — azimuth orbit,  w/e — elevation orbit
        if(event == Event::Character('a') || event == Event::Character('A'))
            { cam_azimuth_  -= 5.f; return true; }
        if(event == Event::Character('d') || event == Event::Character('D'))
            { cam_azimuth_  += 5.f; return true; }
        if(event == Event::Character('w') || event == Event::Character('W'))
            { cam_elevation_ = std::min(89.f, cam_elevation_+5.f); return true; }
        if(event == Event::Character('e') || event == Event::Character('E'))
            { cam_elevation_ = std::max(-89.f, cam_elevation_-5.f); return true; }
        // + / - — slice index
        if(event == Event::Character('+')) {
            const auto& g = model_.world.grid;
            int max_idx = (slice_axis_==simulate::field_query::slice_axis::XY) ? g.grid.z-1
                        : (slice_axis_==simulate::field_query::slice_axis::XZ) ? g.grid.y-1
                        : g.grid.x-1;
            slice_idx_ = std::min(slice_idx_+1, max_idx);
            return true;
        }
        if(event == Event::Character('-')) {
            slice_idx_ = std::max(slice_idx_-1, 0);
            return true;
        }
        // q — quit
        if(event == Event::Character('q') || event == Event::Character('Q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(event_handler);
}

} // namespace gui::ftxui_app
