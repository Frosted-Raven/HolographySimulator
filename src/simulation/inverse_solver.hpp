#pragma once

#include "world.hpp"
#include "space/utility/spacial_nav.hpp"

#include <complex>
#include <vector>

namespace simulate::inverse_solver{

    struct target_point{
        space::utility::point3  position;
        std::complex<double>    desired_pressure;
    };

    enum class method{ backprop, gerchberg_saxton };

    struct params{
        method solver_method = method::gerchberg_saxton;
        int    gs_iterations = 100;
    };

    struct result{
        // Indexed parallel to world.transducers[array_index][transducer_index].
        // Inactive transducers retain their current values.
        std::vector<std::vector<double>> phases;
        std::vector<std::vector<double>> amplitudes;
    };

    result solve(const world::world_model& world,
                 const std::vector<target_point>& targets,
                 const params& p = {});
}
