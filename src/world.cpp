#include "world.hpp"
#include "transducer/transducer_array.hpp"
#include "space/grid.hpp"

#include <lager/util.hpp>
#include <lager/context.hpp>
#include <immer/flex_vector.hpp>

namespace world{
    world_model update(world_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::add_array a){
                m.transducers = m.transducers.push_back(transducer::tran_array::tran_array_model {});
                return m;
            },
            [&](action::remove_array a){
                m.transducers =  m.transducers.erase(a.index);
                return m;
            },
            [&](action::mod_array a){
                m.transducers = m.transducers.set(
                    a.index,
                    transducer::tran_array::update(m.transducers[a.index], a.a));

                return m;
            },
            [&](space::grid::actions a){
                m.grid = space::grid::update(std::move(m.grid), std::move(a));
                return m;
            }
        );
    }
}
