#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "space/medium.hpp"
#include "space/utility/spacial_nav.hpp"
#include "space/grid.hpp"

using Catch::Approx;

// ── medium ───────────────────────────────────────────────────────────────────

TEST_CASE("speed_calc returns sqrt(stiffness/density)", "[medium]"){
    REQUIRE(space::medium::speed_calc(1000.0, 1.0)          == Approx(std::sqrt(1000.0)));
    REQUIRE(space::medium::speed_calc(2250000000.0, 1000.0) == Approx(1500.0)); // water: c=1500, ρ=1000, K=ρc²=2.25e9
}

TEST_CASE("impedance_calc returns density * speed", "[medium]"){
    REQUIRE(space::medium::impedance_calc(1000.0, 1500.0) == Approx(1500000.0));
}

TEST_CASE("reflection_coefficient is zero for identical media", "[medium]"){
    REQUIRE(space::medium::reflection_coefficient(1000.0, 1000.0) == Approx(0.0));
}

TEST_CASE("reflection_coefficient approaches 1 for rigid boundary", "[medium]"){
    // Very high impedance object in air — almost total reflection
    REQUIRE(space::medium::reflection_coefficient(1e10, 400.0) == Approx(1.0).epsilon(0.001));
}

TEST_CASE("medium update - density recalculates sound_speed and impedance", "[medium]"){
    space::medium::medium_model m;
    m.density   = 1.0;
    m.stiffness = 1000.0;
    m.sound_speed = space::medium::speed_calc(m.stiffness, m.density);
    m.acoustic_impedance = space::medium::impedance_calc(m.density, m.sound_speed);

    auto m2 = space::medium::update(m, space::medium::action::density{4.0});
    REQUIRE(m2.density    == Approx(4.0));
    REQUIRE(m2.sound_speed == Approx(space::medium::speed_calc(1000.0, 4.0)));
    REQUIRE(m2.acoustic_impedance == Approx(space::medium::impedance_calc(4.0, m2.sound_speed)));
}

TEST_CASE("medium update - stiffness recalculates sound_speed", "[medium]"){
    space::medium::medium_model m;
    m.density   = 1.0;
    m.stiffness = 1000.0;
    m.sound_speed = space::medium::speed_calc(m.stiffness, m.density);

    auto m2 = space::medium::update(m, space::medium::action::stiffness{4000.0});
    REQUIRE(m2.stiffness  == Approx(4000.0));
    REQUIRE(m2.sound_speed == Approx(space::medium::speed_calc(4000.0, 1.0)));
}

TEST_CASE("medium update - rigid flag", "[medium]"){
    space::medium::medium_model m;
    m.is_rigid = false;
    REQUIRE(space::medium::update(m, space::medium::action::rigid{true}).is_rigid == true);
}

TEST_CASE("medium update - name", "[medium]"){
    space::medium::medium_model m;
    auto m2 = space::medium::update(m, space::medium::action::name{"air"});
    REQUIRE(m2.name == "air");
}

// ── spacial_nav ──────────────────────────────────────────────────────────────

TEST_CASE("index_to_position maps cell index to world coordinates", "[spacial_nav]"){
    space::utility::point3 origin{0.0, 0.0, 0.0};
    double cell_size = 0.003;

    auto p = space::utility::funcs::index_to_position(origin, {5, 3, 7}, cell_size);
    REQUIRE(p.x == Approx(0.015));
    REQUIRE(p.y == Approx(0.009));
    REQUIRE(p.z == Approx(0.021));
}

TEST_CASE("index_to_position respects non-zero origin", "[spacial_nav]"){
    space::utility::point3 origin{1.0, 2.0, 3.0};
    auto p = space::utility::funcs::index_to_position(origin, {1, 0, 0}, 0.01);
    REQUIRE(p.x == Approx(1.01));
    REQUIRE(p.y == Approx(2.0));
    REQUIRE(p.z == Approx(3.0));
}

TEST_CASE("position_to_index is inverse of index_to_position", "[spacial_nav]"){
    space::utility::point3 origin{0.0, 0.0, 0.0};
    double cell_size = 0.003;
    space::utility::cell_point cp{5, 3, 7};

    auto pos = space::utility::funcs::index_to_position(origin, cp, cell_size);
    auto cp2 = space::utility::funcs::position_to_index(origin, pos, cell_size);

    REQUIRE(cp2.x == cp.x);
    REQUIRE(cp2.y == cp.y);
    REQUIRE(cp2.z == cp.z);
}

TEST_CASE("in_bound - origin cell is out of bound (strict > at lower end)", "[spacial_nav]"){
    // in_bound uses point.x > 0, so the origin cell {0,0,0} is excluded
    space::utility::cell_quantity dims{64, 64, 64};
    REQUIRE_FALSE(space::utility::funcs::in_bound(dims, {0, 0, 0}));
    REQUIRE_FALSE(space::utility::funcs::in_bound(dims, {1, 0, 0}));
    REQUIRE_FALSE(space::utility::funcs::in_bound(dims, {0, 1, 0}));
}

TEST_CASE("in_bound - interior cells are in bound", "[spacial_nav]"){
    space::utility::cell_quantity dims{64, 64, 64};
    REQUIRE(space::utility::funcs::in_bound(dims, {1, 1, 1}));
    REQUIRE(space::utility::funcs::in_bound(dims, {32, 32, 32}));
    REQUIRE(space::utility::funcs::in_bound(dims, {63, 63, 63}));
}

TEST_CASE("in_bound - cells at or beyond upper bound are out", "[spacial_nav]"){
    space::utility::cell_quantity dims{64, 64, 64};
    REQUIRE_FALSE(space::utility::funcs::in_bound(dims, {64, 1, 1}));
    REQUIRE_FALSE(space::utility::funcs::in_bound(dims, {1, 64, 1}));
    REQUIRE_FALSE(space::utility::funcs::in_bound(dims, {1, 1, 64}));
}

// ── grid ─────────────────────────────────────────────────────────────────────

TEST_CASE("stamp_in with no objects returns empty map", "[grid]"){
    space::grid::grid_model g;
    g.grid = {16, 16, 16};
    g.cell_size = 0.003;
    REQUIRE(g.stamp_in().empty());
}

TEST_CASE("grid update - grid_dimensions respects optional axes", "[grid]"){
    space::grid::grid_model g;
    g.grid = {16, 16, 16};

    auto g2 = space::grid::update(g, space::grid::action::grid_dimensions{32, {}, {}});
    REQUIRE(g2.grid.x == 32);
    REQUIRE(g2.grid.y == 16);
    REQUIRE(g2.grid.z == 16);

    auto g3 = space::grid::update(g, space::grid::action::grid_dimensions{{}, 8, {}});
    REQUIRE(g3.grid.x == 16);
    REQUIRE(g3.grid.y == 8);
    REQUIRE(g3.grid.z == 16);
}

TEST_CASE("grid update - cell_size", "[grid]"){
    space::grid::grid_model g;
    g.cell_size = 0.003;
    auto g2 = space::grid::update(g, space::grid::action::cell_size{0.005});
    REQUIRE(g2.cell_size == Approx(0.005));
}

TEST_CASE("grid new_sphere adds an object", "[grid]"){
    space::grid::grid_model g;
    g.grid = {32, 32, 32};
    g.cell_size = 0.003;
    g.default_medium.sound_speed = 343.0;
    g.default_medium.is_rigid    = false;

    REQUIRE(g.objects.empty());
    auto g2 = space::grid::update(g, space::grid::action::new_sphere{});
    REQUIRE(g2.objects.size() == 1);
}

TEST_CASE("grid new_cube adds an object", "[grid]"){
    space::grid::grid_model g;
    g.grid = {32, 32, 32};
    g.cell_size = 0.003;
    g.default_medium.sound_speed = 343.0;

    auto g2 = space::grid::update(g, space::grid::action::new_cube{});
    REQUIRE(g2.objects.size() == 1);
}

TEST_CASE("grid update_grid stamps objects into voxels", "[grid]"){
    space::grid::grid_model g;
    g.grid = {32, 32, 32};
    g.cell_size = 0.003;
    g.default_medium.sound_speed = 343.0;

    g = space::grid::update(g, space::grid::action::new_sphere{});
    g = space::grid::update(g, space::grid::action::update_grid{});

    REQUIRE_FALSE(g.voxels.empty());
}
