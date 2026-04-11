#pragma once

#include <lager/effect.hpp>
#include <immer/flex_vector.hpp>

#include <space/utility/spacial_nav.hpp>
#include <space/object/object_descriptor.hpp>
#include <space/medium.hpp>
#include <space/gridd.hpp>

#include <variant>

namespace space::object{

    struct object_model{
        int object_prio;

        shapes::shape_model shape;
        medium::medium_model medium;
        utility::sdf sdf_model;

        immer::flex_vector<grid::voxel> voxels;
    };
}
