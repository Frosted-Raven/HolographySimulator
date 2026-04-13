#pragma once

#include "world.hpp"
#include "simulation/simulation_state.hpp"

#include<lager/effect.hpp>
#include<variant>

namespace simulate::sim_base{
    struct simulation_model{
        world::world_model world;
        sim_state::state_model state;
    };

    using actions = std::variant<world::actions,
                                 sim_state::actions>;
    simulation_model update(simulation_model m, actions a);
}
