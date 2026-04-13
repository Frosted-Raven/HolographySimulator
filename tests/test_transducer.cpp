#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "transducer/transducer.hpp"
#include "transducer/transducer_array.hpp"
#include "world.hpp"

using Catch::Approx;

// ── single transducer ────────────────────────────────────────────────────────

TEST_CASE("new_transducer sets safe defaults", "[transducer]"){
    transducer::single::single_model m;
    auto m2 = transducer::single::update(m, transducer::single::action::new_transducer{});
    REQUIRE(m2.is_active  == false);
    REQUIRE(m2.frequency  == Approx(0.0));
    REQUIRE(m2.amplitude  == Approx(0.0));
    REQUIRE(m2.phase      == Approx(0.0));
}

TEST_CASE("toggle_active flips is_active", "[transducer]"){
    transducer::single::single_model m;
    m.is_active = false;
    auto m2 = transducer::single::update(m, transducer::single::action::toggle_active{});
    REQUIRE(m2.is_active == true);
    auto m3 = transducer::single::update(m2, transducer::single::action::toggle_active{});
    REQUIRE(m3.is_active == false);
}

TEST_CASE("new_phase sets absolute phase", "[transducer]"){
    transducer::single::single_model m;
    m.phase = 0.0;
    auto m2 = transducer::single::update(m, transducer::single::action::new_phase{1.5});
    REQUIRE(m2.phase == Approx(1.5));
}

TEST_CASE("mod_phase adds delta to phase", "[transducer]"){
    transducer::single::single_model m;
    m.phase = 1.0;
    auto m2 = transducer::single::update(m, transducer::single::action::mod_phase{0.5});
    REQUIRE(m2.phase == Approx(1.5));
}

TEST_CASE("new_position sets all axes", "[transducer]"){
    transducer::single::single_model m;
    m.position = {0.0, 0.0, 0.0};
    auto m2 = transducer::single::update(m, transducer::single::action::new_position{1.0, 2.0, 3.0});
    REQUIRE(m2.position.x == Approx(1.0));
    REQUIRE(m2.position.y == Approx(2.0));
    REQUIRE(m2.position.z == Approx(3.0));
}

TEST_CASE("mod_position applies per-axis delta with optional axes", "[transducer]"){
    transducer::single::single_model m;
    m.position = {1.0, 2.0, 3.0};

    // Only move x
    auto m2 = transducer::single::update(m, transducer::single::action::mod_position{{0.5}, {}, {}});
    REQUIRE(m2.position.x == Approx(1.5));
    REQUIRE(m2.position.y == Approx(2.0));
    REQUIRE(m2.position.z == Approx(3.0));
}

TEST_CASE("mod_frequency adds delta", "[transducer]"){
    transducer::single::single_model m;
    m.frequency = 40000.0;
    auto m2 = transducer::single::update(m, transducer::single::action::mod_frequency{1000.0});
    REQUIRE(m2.frequency == Approx(41000.0));
}

TEST_CASE("mod_amplitude adds delta", "[transducer]"){
    transducer::single::single_model m;
    m.amplitude = 1.0;
    auto m2 = transducer::single::update(m, transducer::single::action::mod_amplitude{0.5});
    REQUIRE(m2.amplitude == Approx(1.5));
}

// ── transducer array ─────────────────────────────────────────────────────────

TEST_CASE("add_tran appends a transducer", "[transducer_array]"){
    transducer::tran_array::tran_array_model m;
    REQUIRE(m.tran_array.empty());

    auto m2 = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});
    REQUIRE(m2.tran_array.size() == 1);

    auto m3 = transducer::tran_array::update(m2, transducer::tran_array::action::add_tran{});
    REQUIRE(m3.tran_array.size() == 2);
}

TEST_CASE("remove_tran removes the last transducer", "[transducer_array]"){
    transducer::tran_array::tran_array_model m;
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});

    auto m2 = transducer::tran_array::update(m, transducer::tran_array::action::remove_tran{});
    REQUIRE(m2.tran_array.size() == 1);
}

TEST_CASE("remove_tran on empty array is safe", "[transducer_array]"){
    transducer::tran_array::tran_array_model m;
    auto m2 = transducer::tran_array::update(m, transducer::tran_array::action::remove_tran{});
    REQUIRE(m2.tran_array.empty());
}

TEST_CASE("group_adjust applies action to all transducers", "[transducer_array]"){
    transducer::tran_array::tran_array_model m;
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});

    m = transducer::tran_array::update(m, transducer::tran_array::action::group_adjust{
        transducer::single::action::new_phase{1.5}});

    for(const auto& t : m.tran_array)
        REQUIRE(t.phase == Approx(1.5));
}

TEST_CASE("single_adjust applies action to one transducer only", "[transducer_array]"){
    transducer::tran_array::tran_array_model m;
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});
    m = transducer::tran_array::update(m, transducer::tran_array::action::add_tran{});

    m = transducer::tran_array::update(m, transducer::tran_array::action::single_adjust{
        0, transducer::single::action::new_frequency{40000.0}});

    REQUIRE(m.tran_array[0].frequency == Approx(40000.0));
    REQUIRE(m.tran_array[1].frequency == Approx(0.0));
}

// ── world ────────────────────────────────────────────────────────────────────

TEST_CASE("world add_array and remove_array", "[world]"){
    world::world_model m;
    REQUIRE(m.transducers.empty());

    auto m2 = world::update(m, world::action::add_array{});
    REQUIRE(m2.transducers.size() == 1);

    auto m3 = world::update(m2, world::action::add_array{});
    REQUIRE(m3.transducers.size() == 2);

    auto m4 = world::update(m3, world::action::remove_array{0});
    REQUIRE(m4.transducers.size() == 1);
}

TEST_CASE("world mod_array dispatches to the correct array", "[world]"){
    world::world_model m;
    m = world::update(m, world::action::add_array{});
    m = world::update(m, world::action::add_array{});

    m = world::update(m, world::action::mod_array{
        0, transducer::tran_array::action::add_tran{}});

    REQUIRE(m.transducers[0].tran_array.size() == 1);
    REQUIRE(m.transducers[1].tran_array.empty());
}
