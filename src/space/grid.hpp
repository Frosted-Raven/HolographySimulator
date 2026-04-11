#pragma once

#include <space/medium.hpp>
#include <lager/effect.hpp>
#include <immer/flex_vector.hpp>

#include <variant>
#include <vector>
#include <optional>

namespace space::grid{

    struct voxel{
        int i, j, k;
        std_optional<medium::medium_model> medium;

    }

    struct grid_data{
        std::vector<int> origin = {0, 0, 0}

        double single_axis_d;
        int cell_size;

        double resolution; //Found by multiplying cell size and axis distance

        medium::medium_model default_medium;
        immer::flex_vector<voxel> voxels;
    };
}
