#include "simulation/simulation_state.hpp"

#include "space/grid.hpp"
#include "space/utility/spacial_nav.hpp"

#include <lager/util.hpp>
#include <lager/context.hpp>

#include <cmath>
#include <numbers>

namespace simulate::sim_state{
    std::vector<std::complex<double>> solve(world::world_model world){
        auto& grid = world.grid;
        auto& dims = grid.grid;
        size_t total = static_cast<size_t>(dims.x) * dims.y * dims.z;

        std::vector<std::complex<double>> field(total, {0.0, 0.0});

        for(uint16_t z = 0; z < dims.z; ++z){
            for(uint16_t y = 0; y < dims.y; ++y){
                for(uint16_t x = 0; x < dims.x; ++x){
                    space::utility::cell_point cp{x, y, z};
                    space::utility::point3 pos = space::utility::funcs::index_to_position(
                        space::grid::origin, cp, grid.cell_size);

                    auto voxel = grid.voxels.find(cp);
                    auto med = voxel ? voxel->medium : grid.default_medium;

                    if(med.is_rigid) continue;

                    std::complex<double> pressure{0.0, 0.0};

                    for(const auto& array : world.transducers){
                        for(const auto& tran : array.tran_array){
                            if(!tran.is_active) continue;

                            double dx = pos.x - tran.position.x;
                            double dy = pos.y - tran.position.y;
                            double dz = pos.z - tran.position.z;
                            double r = std::sqrt(dx*dx + dy*dy + dz*dz);

                            if(r < 1e-10) continue;

                            double k = 2.0 * std::numbers::pi * tran.frequency / med.sound_speed;

                            pressure += (tran.amplitude / r)
                                      * std::exp(-med.absorption * r)
                                      * std::exp(std::complex<double>{0.0, k * r + tran.phase});
                        }
                    }

                    size_t idx = static_cast<size_t>(z) * dims.x * dims.y
                               + static_cast<size_t>(y) * dims.x
                               + x;
                    field[idx] = pressure;
                }
            }
        }

        return field;
    }

    state_model update(state_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::run_solver a){
                m.pressure = solve(a.world);
                return m;
            },
            [&](action::update_status a){
                m.current = a.new_status;
                return m;
            });
    }
}
