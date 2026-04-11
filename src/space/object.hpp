#pragma once

#include <lager/effect.hpp>
#include <immer/vector.hpp>
#include <space/medium.hpp>
#include <space/utility/spacial_nav.hpp>

#include <string>
#include <cstdint>
#include <variant>
#include <vector>

namespace space::object{

    struct object_model{
        std::string name;

        double cell_s;
        descriptor::position world_space;
        descriptor::cell_quantity amount;

        medium::medium_model medium;

        immer::vector<double> distances;
        immer::vector<descriptor::vec_position> normals;
    };

    namespace actions{

    }
}
