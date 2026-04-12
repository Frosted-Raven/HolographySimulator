#pragma once

#include "space/medium.hpp"
#include "space/object/object.hpp"
#include "space/utility/spacial_nav.hpp"

#include <lager/effect.hpp>
#include <immer/map.hpp>
#include <immer/flex_vector.hpp>

#include <variant>
#include <vector>
#include <optional>
#include <cstdint>

namespace space::grid{
    const utility::point3 origin = {0.0, 0.0, 0.0};
    immer::flex_vector<object::object_model> sort_prio(immer::flex_vector<object::object_model> objects);

    struct voxel_data{
        utility::vector3 normal;
        medium::medium_model medium;
    };

    struct grid_model{
        utility::cell_quantity grid;
        double cell_size;

        medium::medium_model default_medium;
        immer::map<utility::cell_point, voxel_data> voxels;
        immer::flex_vector<object::object_model> objects;

        immer::map<utility::cell_point, voxel_data> stamp_in();
    };

    namespace action{
        struct cell_size{
            double new_size;
        };

        struct grid_dimensions{
            std::optional<uint16_t> new_x;
            std::optional<uint16_t> new_y;
            std::optional<uint16_t> new_z;
        };

        struct default_medium_action{
            medium::actions action;
        };

        struct object_action{
            int index;
            object::actions action;
        };

        struct update_object_volume{
            int index;
        };

        struct update_object_sdf{
            int index;
        };

        struct new_sphere{};
        struct new_cube{};

        struct update_grid{};
        struct sort_objects{};
    }

    using actions = std::variant<action::cell_size,
                                 action::grid_dimensions,
                                 action::default_medium_action,
                                 action::object_action,
                                 action::update_object_volume,
                                 action::update_object_sdf,
                                 action::new_sphere,
                                 action::new_cube,
                                 action::update_grid,
                                 action::sort_objects>;

    grid_model update(grid_model m, actions a);
}
