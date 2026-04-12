#pragma once

#include <lager/effect.hpp>
#include <immer/map.hpp>
#include <space/utility/spacial_nav.hpp>
#include <space/object/object_descriptor.hpp>
#include <space/medium.hpp>

#include <variant>
#include <string>

namespace space::object{

    struct object_model{
        int object_prio;
        std::string name;

        utility::cell_point transform_mod = {0, 0, 0};
        medium::medium_model medium;

        shapes::shape_model shape;

        utility::sdf sdf_data;
        immer::map<utility::cell_point, utility::vector3> volume;
        void generate_volume(double cell_size);
    };

    namespace action{
        struct transform_mod{
            utility::cell_point new_transform;
        };

        struct medium{
            space::medium::medium_model new_medium;
        };

        struct shape{
            shapes::shape_model new_shape;
        };

        struct priority{
            int new_prio;
        };

        struct name{
            std::string new_name;
        };

        struct update_sdf{
            double cell_size;
        };

        struct update_volume{
            double cell_size;
        };
    }

    using actions = std::variant<action::transform_mod,
                                 action::medium,
                                 action::shape,
                                 action::priority,
                                 action::name,
                                 action::update_sdf,
                                 action::update_volume,
                                 shapes::actions,
                                 space::medium::actions>;

    object_model update(object_model m, actions a);
}
