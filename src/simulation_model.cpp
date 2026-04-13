#include "simulation_model.hpp"

#include "world.hpp"
#include "simulation/simulation_state.hpp"
#include "simulation/inverse_solver.hpp"
#include "transducer/transducer.hpp"
#include "transducer/transducer_array.hpp"

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
            },
            [&](action::run_inverse a){
                auto inv = inverse_solver::solve(m.world, a.targets, a.p);

                for(size_t ai = 0; ai < inv.phases.size() && ai < m.world.transducers.size(); ++ai){
                    auto arr = m.world.transducers[ai];
                    for(size_t ti = 0; ti < inv.phases[ai].size() && ti < arr.tran_array.size(); ++ti){
                        arr.tran_array = arr.tran_array.set(ti,
                            transducer::single::update(arr.tran_array[ti],
                                transducer::single::action::new_phase{inv.phases[ai][ti]}));
                        arr.tran_array = arr.tran_array.set(ti,
                            transducer::single::update(arr.tran_array[ti],
                                transducer::single::action::new_amplitude{inv.amplitudes[ai][ti]}));
                    }
                    m.world.transducers = m.world.transducers.set(ai, arr);
                }

                m.state = simulate::sim_state::update(
                    std::move(m.state),
                    simulate::sim_state::action::update_status{simulate::sim_state::OLD});

                return m;
            });
    }
}
