#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "simulation_model.hpp"
#include "simulation/simulation_state.hpp"
#include "simulation/ray_traversal.hpp"
#include "simulation/field_query.hpp"
#include "simulation/inverse_solver.hpp"
#include "space/grid.hpp"
#include "space/utility/spacial_nav.hpp"
#include "transducer/transducer.hpp"
#include "transducer/transducer_array.hpp"
#include "world.hpp"

#include <cmath>
#include <numbers>

using Catch::Approx;

// ── helpers ──────────────────────────────────────────────────────────────────

static space::medium::medium_model air_medium(){
    space::medium::medium_model m;
    m.sound_speed = 343.0;
    m.absorption  = 0.0;
    m.density     = 1.2;
    m.is_rigid    = false;
    return m;
}

// Minimal world: one active transducer, no objects, air throughout
static world::world_model make_world(space::utility::point3 tran_pos,
                                     double freq = 40000.0,
                                     double amplitude = 1.0){
    world::world_model w;
    w.grid.grid      = {20, 20, 20};
    w.grid.cell_size = 0.003;
    w.grid.default_medium = air_medium();

    transducer::single::single_model t;
    t.position  = tran_pos;
    t.frequency = freq;
    t.amplitude = amplitude;
    t.phase     = 0.0;
    t.is_active = true;

    transducer::tran_array::tran_array_model arr;
    arr.tran_array = arr.tran_array.push_back(t);
    w.transducers = w.transducers.push_back(arr);

    return w;
}

// ── ray traversal ────────────────────────────────────────────────────────────

TEST_CASE("ray traversal - homogeneous space fast path matches analytical formula", "[ray_traversal]"){
    space::grid::grid_model grid;
    grid.grid      = {32, 32, 32};
    grid.cell_size = 0.003;
    grid.default_medium = air_medium();

    double freq = 40000.0;
    double r    = 0.03;
    auto result = simulate::ray_traversal::traverse({0,0,0}, {r,0,0}, freq, grid);

    double expected_phase = 2.0 * std::numbers::pi * freq / 343.0 * r;
    REQUIRE(result.phase       == Approx(expected_phase).epsilon(0.001));
    REQUIRE(result.attenuation == Approx(0.0).margin(1e-12));
}

TEST_CASE("ray traversal - attenuation accumulates over distance", "[ray_traversal]"){
    space::grid::grid_model grid;
    grid.grid      = {32, 32, 32};
    grid.cell_size = 0.003;
    grid.default_medium = air_medium();
    grid.default_medium.absorption = 0.5; // 0.5 Np/m

    double r = 0.02;
    auto result = simulate::ray_traversal::traverse({0,0,0}, {r,0,0}, 40000.0, grid);
    REQUIRE(result.attenuation == Approx(0.5 * r).epsilon(0.01));
}

TEST_CASE("ray traversal - zero length ray returns zeros", "[ray_traversal]"){
    space::grid::grid_model grid;
    grid.grid      = {16, 16, 16};
    grid.cell_size = 0.003;
    grid.default_medium = air_medium();

    space::utility::point3 p{0.01, 0.01, 0.01};
    auto result = simulate::ray_traversal::traverse(p, p, 40000.0, grid);
    REQUIRE(result.phase       == Approx(0.0).margin(1e-12));
    REQUIRE(result.attenuation == Approx(0.0).margin(1e-12));
}

// ── forward solver ───────────────────────────────────────────────────────────

TEST_CASE("forward solver - produces a field of the correct size", "[solver]"){
    auto world = make_world({-0.003, 0.015, 0.015});

    simulate::sim_state::state_model state;
    state = simulate::sim_state::update(state,
        simulate::sim_state::action::run_solver{world});

    REQUIRE(state.current == simulate::sim_state::VALID);
    REQUIRE(state.pressure.size() == 20u * 20u * 20u);
}

TEST_CASE("forward solver - inactive transducer produces zero field", "[solver]"){
    auto world = make_world({-0.003, 0.015, 0.015});
    // Deactivate the only transducer
    world.transducers = world.transducers.set(0,
        transducer::tran_array::update(world.transducers[0],
            transducer::tran_array::action::single_adjust{
                0, transducer::single::action::toggle_active{}}));

    simulate::sim_state::state_model state;
    state = simulate::sim_state::update(state,
        simulate::sim_state::action::run_solver{world});

    for(const auto& p : state.pressure)
        REQUIRE(std::abs(p) == Approx(0.0).margin(1e-12));
}

TEST_CASE("forward solver - pressure magnitude decreases with distance", "[solver]"){
    // Transducer outside the grid along -x; cells along x-axis get closer
    auto world = make_world({-0.003, 0.015, 0.015});

    simulate::sim_state::state_model state;
    state = simulate::sim_state::update(state,
        simulate::sim_state::action::run_solver{world});

    // Cell {2,5,5} is closer to the transducer than {10,5,5}
    auto near_idx = 5u * 20u * 20u + 5u * 20u + 2u;
    auto far_idx  = 5u * 20u * 20u + 5u * 20u + 10u;

    REQUIRE(std::abs(state.pressure[near_idx]) > std::abs(state.pressure[far_idx]));
}

TEST_CASE("forward solver - rigid voxel has zero pressure", "[solver]"){
    world::world_model world;
    world.grid.grid      = {20, 20, 20};
    world.grid.cell_size = 0.003;
    world.grid.default_medium = air_medium();

    // Stamp one rigid voxel manually
    space::medium::medium_model rigid = air_medium();
    rigid.is_rigid = true;
    space::utility::cell_point cp{5, 5, 5};
    world.grid.voxels = world.grid.voxels.set(cp, space::grid::voxel_data{{0,0,1}, rigid});

    transducer::single::single_model t;
    t.position  = {-0.003, 0.015, 0.015};
    t.frequency = 40000.0;
    t.amplitude = 1.0;
    t.phase     = 0.0;
    t.is_active = true;
    transducer::tran_array::tran_array_model arr;
    arr.tran_array = arr.tran_array.push_back(t);
    world.transducers = world.transducers.push_back(arr);

    simulate::sim_state::state_model state;
    state = simulate::sim_state::update(state,
        simulate::sim_state::action::run_solver{world});

    size_t rigid_idx = 5u * 20u * 20u + 5u * 20u + 5u;
    REQUIRE(std::abs(state.pressure[rigid_idx]) == Approx(0.0).margin(1e-12));
}

TEST_CASE("forward solver - status transitions correctly", "[solver]"){
    auto world = make_world({-0.003, 0.015, 0.015});
    simulate::sim_state::state_model state;

    REQUIRE(state.current == simulate::sim_state::UNCOMPUTED);

    state = simulate::sim_state::update(state,
        simulate::sim_state::action::run_solver{world});
    REQUIRE(state.current == simulate::sim_state::VALID);

    state = simulate::sim_state::update(state,
        simulate::sim_state::action::update_status{simulate::sim_state::OLD});
    REQUIRE(state.current == simulate::sim_state::OLD);
}

// ── field query ──────────────────────────────────────────────────────────────

TEST_CASE("field_query - uncomputed state returns zero for all accessors", "[field_query]"){
    simulate::sim_state::state_model state;
    space::utility::cell_quantity dims{16, 16, 16};

    REQUIRE(simulate::field_query::magnitude_at(state, {4,4,4}, dims) == Approx(0.0));
    REQUIRE(simulate::field_query::phase_at    (state, {4,4,4}, dims) == Approx(0.0));
    REQUIRE(simulate::field_query::intensity_at(state, {4,4,4}, dims) == Approx(0.0));
}

TEST_CASE("field_query - out-of-bounds cell returns zero", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    state.pressure.resize(16 * 16 * 16, {1.0, 0.0});
    space::utility::cell_quantity dims{16, 16, 16};

    REQUIRE(simulate::field_query::magnitude_at(state, {16, 0, 0}, dims) == Approx(0.0));
}

TEST_CASE("field_query - magnitude and intensity are consistent", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    state.pressure.resize(8 * 8 * 8, {3.0, 4.0}); // |p| = 5, |p|^2 = 25
    space::utility::cell_quantity dims{8, 8, 8};

    REQUIRE(simulate::field_query::magnitude_at (state, {2,2,2}, dims) == Approx(5.0));
    REQUIRE(simulate::field_query::intensity_at (state, {2,2,2}, dims) == Approx(25.0));
}

TEST_CASE("field_query - phase_at returns correct argument", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    state.pressure.resize(4 * 4 * 4, {0.0, 1.0}); // arg = π/2
    space::utility::cell_quantity dims{4, 4, 4};

    REQUIRE(simulate::field_query::phase_at(state, {1,1,1}, dims)
        == Approx(std::numbers::pi / 2.0).epsilon(0.001));
}

TEST_CASE("field_query - extract_slice XY dimensions", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    space::utility::cell_quantity dims{8, 12, 6};
    state.pressure.resize(8 * 12 * 6, {1.0, 0.0});

    auto s = simulate::field_query::extract_slice(
        state, dims, simulate::field_query::slice_axis::XY, 0);
    REQUIRE(s.width  == 8);
    REQUIRE(s.height == 12);
    REQUIRE(s.data.size() == 8u * 12u);
}

TEST_CASE("field_query - extract_slice XZ dimensions", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    space::utility::cell_quantity dims{8, 12, 6};
    state.pressure.resize(8 * 12 * 6, {1.0, 0.0});

    auto s = simulate::field_query::extract_slice(
        state, dims, simulate::field_query::slice_axis::XZ, 0);
    REQUIRE(s.width  == 8);
    REQUIRE(s.height == 6);
}

TEST_CASE("field_query - extract_slice YZ dimensions", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    space::utility::cell_quantity dims{8, 12, 6};
    state.pressure.resize(8 * 12 * 6, {1.0, 0.0});

    auto s = simulate::field_query::extract_slice(
        state, dims, simulate::field_query::slice_axis::YZ, 0);
    REQUIRE(s.width  == 12);
    REQUIRE(s.height == 6);
}

TEST_CASE("field_query - out-of-range slice index returns empty slice", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    space::utility::cell_quantity dims{8, 8, 8};
    state.pressure.resize(8 * 8 * 8, {1.0, 0.0});

    auto s = simulate::field_query::extract_slice(
        state, dims, simulate::field_query::slice_axis::XY, 99);
    REQUIRE(s.data.empty());
}

TEST_CASE("field_query - magnitude slice has correct size and values", "[field_query]"){
    simulate::sim_state::state_model state;
    state.current = simulate::sim_state::VALID;
    space::utility::cell_quantity dims{4, 4, 4};
    state.pressure.resize(4 * 4 * 4, {3.0, 4.0}); // |p| = 5 everywhere

    auto s    = simulate::field_query::extract_slice(state, dims, simulate::field_query::slice_axis::XY, 0);
    auto mags = simulate::field_query::magnitude(s);

    REQUIRE(mags.size() == 4u * 4u);
    for(auto v : mags)
        REQUIRE(v == Approx(5.0));
}

// ── inverse solver ───────────────────────────────────────────────────────────

TEST_CASE("inverse solver - empty targets returns current values unchanged", "[inverse_solver]"){
    auto world = make_world({-0.003, 0.015, 0.015});
    world.transducers[0].tran_array[0]; // just access it

    auto result = simulate::inverse_solver::solve(world, {});
    REQUIRE(result.phases.size()     == 1);
    REQUIRE(result.phases[0].size()  == 1);
    // Phase should be the transducer's current phase (0.0)
    REQUIRE(result.phases[0][0] == Approx(0.0));
}

TEST_CASE("inverse solver - result is indexed parallel to transducer hierarchy", "[inverse_solver]"){
    world::world_model world;
    world.grid.grid      = {20, 20, 20};
    world.grid.cell_size = 0.003;
    world.grid.default_medium = air_medium();

    // Two arrays, 3 and 2 transducers
    for(int n : {3, 2}){
        transducer::tran_array::tran_array_model arr;
        for(int i = 0; i < n; ++i){
            transducer::single::single_model t;
            t.position  = {static_cast<double>(i) * 0.01, 0.0, 0.0};
            t.frequency = 40000.0;
            t.amplitude = 1.0;
            t.phase     = 0.0;
            t.is_active = true;
            arr.tran_array = arr.tran_array.push_back(t);
        }
        world.transducers = world.transducers.push_back(arr);
    }

    std::vector<simulate::inverse_solver::target_point> targets{
        {{0.03, 0.03, 0.03}, {1.0, 0.0}}
    };

    auto result = simulate::inverse_solver::solve(world, targets);
    REQUIRE(result.phases.size()     == 2);
    REQUIRE(result.phases[0].size()  == 3);
    REQUIRE(result.phases[1].size()  == 2);
    REQUIRE(result.amplitudes[0].size() == 3);
    REQUIRE(result.amplitudes[1].size() == 2);
}

TEST_CASE("inverse solver - backprop produces phases in valid range", "[inverse_solver]"){
    auto world = make_world({-0.003, 0.015, 0.015});

    std::vector<simulate::inverse_solver::target_point> targets{
        {{0.015, 0.015, 0.015}, {1.0, 0.0}}
    };
    simulate::inverse_solver::params p;
    p.solver_method = simulate::inverse_solver::method::backprop;

    auto result = simulate::inverse_solver::solve(world, targets, p);

    for(auto ph : result.phases[0])
        REQUIRE((ph >= -std::numbers::pi && ph <= std::numbers::pi));
}

TEST_CASE("inverse solver - backprop single transducer produces constructive phase at target", "[inverse_solver]"){
    // Transducer at {-0.003, 0.015, 0.015}, target voxel at {5,5,5} = {0.015, 0.015, 0.015}
    // r = 0.018m. After applying backprop phase, pressure at target should be real and positive.
    auto world = make_world({-0.003, 0.015, 0.015});

    space::utility::cell_point target_cell{5, 5, 5};
    auto target_pos = space::utility::funcs::index_to_position(
        space::grid::origin, target_cell, world.grid.cell_size);

    std::vector<simulate::inverse_solver::target_point> targets{{target_pos, {1.0, 0.0}}};
    simulate::inverse_solver::params p;
    p.solver_method = simulate::inverse_solver::method::backprop;

    // Run inverse, apply to world, run forward
    simulate::sim_base::simulation_model sim;
    sim.world = world;
    sim = simulate::sim_base::update(std::move(sim),
        simulate::sim_base::action::run_inverse{targets, p});

    // Capture world before moving sim to avoid undefined argument evaluation order
    auto updated_world = sim.world;
    sim = simulate::sim_base::update(std::move(sim),
        simulate::sim_state::action::run_solver{updated_world});

    REQUIRE(sim.state.current == simulate::sim_state::VALID);

    // Pressure at target voxel should be real (arg ≈ 0) — backprop cancels travel phase
    auto pressure_at_target = sim.state.pressure[
        5u * 20u * 20u + 5u * 20u + 5u];

    REQUIRE(std::arg(pressure_at_target) == Approx(0.0).margin(0.01));
    REQUIRE(std::abs(pressure_at_target) > 0.0);
}

TEST_CASE("inverse solver - GS improves coherence at focal point vs no optimisation", "[inverse_solver]"){
    // Linear transducer array along x, focal point off-axis so each transducer
    // has a different path length and the phase=0 baseline is incoherent.
    // Distances: ~0.0436, ~0.0410, ~0.0392, ~0.0383 m → baseline |p| ≈ 30,
    // coherent maximum ≈ 99. GS should clearly outperform the baseline.
    world::world_model world;
    world.grid.grid      = {16, 16, 16};
    world.grid.cell_size = 0.003;
    world.grid.default_medium = air_medium();

    space::utility::point3 focal{0.021, 0.021, 0.021}; // cell {7,7,7}
    transducer::tran_array::tran_array_model arr;
    for(double x : {0.0, 0.006, 0.012, 0.018}){
        transducer::single::single_model t;
        t.position  = {x, -0.006, -0.006}; // outside grid
        t.frequency = 40000.0;
        t.amplitude = 1.0;
        t.phase     = 0.0;
        t.is_active = true;
        arr.tran_array = arr.tran_array.push_back(t);
    }
    world.transducers = world.transducers.push_back(arr);

    std::vector<simulate::inverse_solver::target_point> targets{{focal, {1.0, 0.0}}};
    simulate::inverse_solver::params p;
    p.solver_method = simulate::inverse_solver::method::gerchberg_saxton;
    p.gs_iterations = 50;

    // Baseline: forward solve with phase = 0
    simulate::sim_state::state_model baseline;
    baseline = simulate::sim_state::update(baseline,
        simulate::sim_state::action::run_solver{world});

    // Optimised: inverse then forward
    simulate::sim_base::simulation_model sim;
    sim.world = world;
    sim = simulate::sim_base::update(std::move(sim),
        simulate::sim_base::action::run_inverse{targets, p});

    // Capture world before moving sim to avoid undefined argument evaluation order
    auto updated_world = sim.world;
    sim = simulate::sim_base::update(std::move(sim),
        simulate::sim_state::action::run_solver{updated_world});

    auto focal_cell = space::utility::funcs::position_to_index(
        space::grid::origin, focal, world.grid.cell_size);
    size_t focal_idx = static_cast<size_t>(focal_cell.z) * 16u * 16u
                     + static_cast<size_t>(focal_cell.y) * 16u
                     + focal_cell.x;

    double baseline_mag  = std::abs(baseline.pressure[focal_idx]);
    double optimised_mag = std::abs(sim.state.pressure[focal_idx]);

    REQUIRE(optimised_mag > baseline_mag);
}
