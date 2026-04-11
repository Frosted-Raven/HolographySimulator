#pragma once

#include <space/medium.hpp>
#include <space/utility/spacial_nav.hpp>
#include <lager/effect.hpp>

#include <functional>
#include <variant>
#include <vector>
//seperate shapes that objects can have
namespace space::object::shapes{

    struct shape_model{
        utility::point3 world_position;
        virtual utility::sdf generate(double cell_size);
    };

    struct sphere_model : public shape_model{
        double scale;
        utility::sdf generate(double cell_size) override;
    };

    struct cube_model : public shape_model{
        utility::vector3 scale;
        utility::vector3 rotation;

        utility::sdf generate(double cell_size) override;
    };

    namespace action{
        struct new_sphere{
            utility::point3 new_pos;
            double scale;
        };

        struct new_cube{
            utility::point3 new_pos;
            utility::vector3 new_scale;
            utility::vector3 rotation;
        };

        struct edit_position{
            utility::point3 new_pos;
        };

        struct edit_cube{
            std::optional<utility::vector3> new_scale;
            std::optional<utility::vector3> new_rotation;
        };

        struct edit_sphere{
            double new_scale;
        };
    }

    using actions = std::variant<action::new_sphere,
                                 action::new_cube,
                                 action::edit_sphere,
                                 action::edit_cube,
                                 action::edit_position>;

    shape_model update(shape_model m, actions a);
}
