#include "simulation_model.hpp"

#include "world.hpp"
#include "simulation/simulation_state.hpp"

#include <lager/util.hpp>
#include <lager/context.hpp>

namespace simulate::sim_base{
    simulation_model update(simulation_model m, actions a){
        return lager::match(std::move(a))(
            [&](world::actions a){
                m.world = world::update(std::move(m.world), std::move(a));
                m.state = simulate::sim_state::update(
                    std::move(m.state),
                    simulate::sim_state::action::update_status{simulate::sim_state::OLD});

                return m;
            },
            [&](sim_state::actions a){
                m.state = simulate::sim_state::update(std::move(m.state), std::move(a));
                return m;
            });
    }
}
