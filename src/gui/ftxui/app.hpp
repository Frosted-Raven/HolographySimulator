#pragma once

#include "simulation_model.hpp"
#include "simulation/field_query.hpp"
#include "simulation/inverse_solver.hpp"
#include "world.hpp"
#include "space/grid.hpp"
#include "simulation/simulation_state.hpp"

#include <string>
#include <vector>
#include <complex>

namespace gui::ftxui_app {

class App {
public:
    App();
    void run();

private:
    // ── Model ─────────────────────────────────────────────────────────────────
    simulate::sim_base::simulation_model model_;
    double last_solve_ms_ = 0.0;

    void dispatch(simulate::sim_base::actions a);
    void dispatch_world(world::actions a)              { dispatch(std::move(a)); }
    void dispatch_grid(space::grid::actions a)         { dispatch(world::actions{std::move(a)}); }
    void dispatch_state(simulate::sim_state::actions a){ dispatch(std::move(a)); }
    void run_forward_solver();
    void run_inverse_solver();

    // ── UI state ──────────────────────────────────────────────────────────────
    std::string scene_name_ = "UNNAMED_PROGRAM";

    // Section expand state
    bool sec_scene_open_  = true;
    bool sec_tran_open_   = true;
    bool sec_solver_open_ = true;

    int  sel_array_ = -1;
    int  sel_tran_  = -1;
    int  sel_obj_   = -1;

    simulate::field_query::slice_axis slice_axis_ = simulate::field_query::slice_axis::XY;
    int  slice_idx_    = 0;
    int  overlay_mode_ = 0;    // 0=magnitude  1=phase  2=intensity
    int  inv_method_   = 0;    // 0=backprop  1=GS

    // Camera (for text orientation indicator)
    float cam_azimuth_   = 45.f;
    float cam_elevation_ = 30.f;

    // Inverse solver
    std::vector<simulate::inverse_solver::target_point> inv_targets_;
    simulate::inverse_solver::params inv_params_;
};

} // namespace gui::ftxui_app
