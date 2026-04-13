#pragma once

#include "simulation/simulation_state.hpp"
#include "space/utility/spacial_nav.hpp"

#include <complex>
#include <vector>

namespace simulate::field_query{

    enum class slice_axis{ XY, XZ, YZ };

    struct slice{
        std::vector<std::complex<double>> data;
        uint16_t width;
        uint16_t height;
    };

    // ── Point accessors ──────────────────────────────────────────────────────
    double magnitude_at(const sim_state::state_model& state,
                        space::utility::cell_point cp,
                        space::utility::cell_quantity dims);

    double phase_at(const sim_state::state_model& state,
                    space::utility::cell_point cp,
                    space::utility::cell_quantity dims);

    double intensity_at(const sim_state::state_model& state,
                        space::utility::cell_point cp,
                        space::utility::cell_quantity dims);

    // ── Slice extraction ─────────────────────────────────────────────────────
    slice extract_slice(const sim_state::state_model& state,
                        space::utility::cell_quantity dims,
                        slice_axis axis,
                        uint16_t index);

    std::vector<double> magnitude(const slice& s);
    std::vector<double> phase(const slice& s);
    std::vector<double> intensity(const slice& s);
}
