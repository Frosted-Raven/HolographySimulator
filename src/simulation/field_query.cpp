#include "simulation/field_query.hpp"

#include <cmath>
#include <complex>

namespace simulate::field_query{

    static size_t to_index(space::utility::cell_point cp, space::utility::cell_quantity dims){
        return static_cast<size_t>(cp.z) * dims.x * dims.y
             + static_cast<size_t>(cp.y) * dims.x
             + cp.x;
    }

    static bool in_bound(space::utility::cell_point cp, space::utility::cell_quantity dims){
        return cp.x < dims.x && cp.y < dims.y && cp.z < dims.z;
    }

    static bool field_valid(const sim_state::state_model& state,
                            space::utility::cell_point cp,
                            space::utility::cell_quantity dims){
        return state.current != sim_state::UNCOMPUTED
            && in_bound(cp, dims)
            && to_index(cp, dims) < state.pressure.size();
    }

    // ── Point accessors ──────────────────────────────────────────────────────

    double magnitude_at(const sim_state::state_model& state,
                        space::utility::cell_point cp,
                        space::utility::cell_quantity dims){
        if(!field_valid(state, cp, dims)) return 0.0;
        return std::abs(state.pressure[to_index(cp, dims)]);
    }

    double phase_at(const sim_state::state_model& state,
                    space::utility::cell_point cp,
                    space::utility::cell_quantity dims){
        if(!field_valid(state, cp, dims)) return 0.0;
        return std::arg(state.pressure[to_index(cp, dims)]);
    }

    double intensity_at(const sim_state::state_model& state,
                        space::utility::cell_point cp,
                        space::utility::cell_quantity dims){
        if(!field_valid(state, cp, dims)) return 0.0;
        return std::norm(state.pressure[to_index(cp, dims)]);
    }

    // ── Slice extraction ─────────────────────────────────────────────────────

    slice extract_slice(const sim_state::state_model& state,
                        space::utility::cell_quantity dims,
                        slice_axis axis,
                        uint16_t index){
        slice s;

        if(state.current == sim_state::UNCOMPUTED || state.pressure.empty())
            return s;

        switch(axis){
            case slice_axis::XY:{
                if(index >= dims.z) return s;
                s.width  = dims.x;
                s.height = dims.y;
                s.data.resize(s.width * s.height);
                for(uint16_t y = 0; y < dims.y; ++y){
                    for(uint16_t x = 0; x < dims.x; ++x){
                        size_t src = to_index({x, y, index}, dims);
                        s.data[y * s.width + x] = state.pressure[src];
                    }
                }
                break;
            }
            case slice_axis::XZ:{
                if(index >= dims.y) return s;
                s.width  = dims.x;
                s.height = dims.z;
                s.data.resize(s.width * s.height);
                for(uint16_t z = 0; z < dims.z; ++z){
                    for(uint16_t x = 0; x < dims.x; ++x){
                        size_t src = to_index({x, index, z}, dims);
                        s.data[z * s.width + x] = state.pressure[src];
                    }
                }
                break;
            }
            case slice_axis::YZ:{
                if(index >= dims.x) return s;
                s.width  = dims.y;
                s.height = dims.z;
                s.data.resize(s.width * s.height);
                for(uint16_t z = 0; z < dims.z; ++z){
                    for(uint16_t y = 0; y < dims.y; ++y){
                        size_t src = to_index({index, y, z}, dims);
                        s.data[z * s.width + y] = state.pressure[src];
                    }
                }
                break;
            }
        }

        return s;
    }

    // ── Derived slice views ──────────────────────────────────────────────────

    std::vector<double> magnitude(const slice& s){
        std::vector<double> out;
        out.reserve(s.data.size());
        for(const auto& c : s.data)
            out.push_back(std::abs(c));
        return out;
    }

    std::vector<double> phase(const slice& s){
        std::vector<double> out;
        out.reserve(s.data.size());
        for(const auto& c : s.data)
            out.push_back(std::arg(c));
        return out;
    }

    std::vector<double> intensity(const slice& s){
        std::vector<double> out;
        out.reserve(s.data.size());
        for(const auto& c : s.data)
            out.push_back(std::norm(c));
        return out;
    }
}
