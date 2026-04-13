#pragma once

#include "space/grid.hpp"
#include "space/utility/spacial_nav.hpp"

namespace simulate::ray_traversal{

    struct ray_integral{
        double phase;       // accumulated integral of k(x) dx
        double attenuation; // accumulated integral of alpha(x) dx
    };

    // Returns the phase and attenuation integrals for a ray from `from` to `to`
    // through the grid. Voxels not in the stamped map use grid.default_medium.
    // Rays that pass entirely through homogeneous space skip full DDA traversal.
    ray_integral traverse(space::utility::point3 from,
                          space::utility::point3 to,
                          double frequency,
                          const space::grid::grid_model& grid);
}
