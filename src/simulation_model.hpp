#pragma once

#include "world.hpp"
#include "simulation/simulation_state.hpp"
#include "simulation/inverse_solver.hpp"

#include <lager/effect.hpp>
#include <variant>
#include <vector>

namespace simulate::sim_base{
    struct simulation_model{
        world::world_model world;
        sim_state::state_model state;
    };

    namespace action{
        struct run_inverse{
            std::vector<inverse_solver::target_point> targets;
            inverse_solver::params p;
        };
    }

    using actions = std::variant<world::actions,
                                 sim_state::actions,
                                 action::run_inverse>;

    simulation_model update(simulation_model m, actions a);
}
