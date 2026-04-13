#pragma once

#include "world.hpp"

#include <lager/effect.hpp>

#include <complex>
#include <vector>
#include <variant>

namespace simulate::sim_state{
    enum status{
        UNCOMPUTED,
        VALID,
        OLD
    };

    struct state_model{
        enum status current = UNCOMPUTED;
        std::vector<std::complex<double>> pressure;
    };

    namespace action{
        struct run_solver{
            world::world_model world;
        };

        struct update_status{
            enum status new_status;
        };
    }

    using actions = std::variant<action::run_solver,
                                 action::update_status>;

    state_model update(state_model m, actions a);
}
