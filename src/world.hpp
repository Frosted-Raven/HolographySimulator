#pragma once

#include "transducer/transducer_array.hpp"
#include "space/grid.hpp"

#include <lager/effect.hpp>
#include <immer/flex_vector.hpp>

#include <variant>

namespace world{
    struct world_model{
        space::grid::grid_model grid;
        immer::flex_vector<transducer::tran_array::tran_array_model> transducers;
    };

    namespace action{
        struct add_array{};
        struct remove_array{
            size_t index;
        };
        struct mod_array{
            size_t index;
            transducer::tran_array::actions a;
        };
    }

    using actions = std::variant<action::add_array,
                                 action::remove_array,
                                 action::mod_array,
                                 space::grid::actions>;

    world_model update(world_model m, actions a);
}
