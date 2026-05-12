#include "simulation/ray_traversal.hpp"

#include "space/utility/spacial_nav.hpp"

#include <cmath>
#include <numbers>
#include <algorithm>

namespace simulate::ray_traversal{

    // Returns true if the ray bounding box overlaps the stamped voxel region.
    // The stamped map is sparse so we check against each voxel's cell bounds.
    // For grids with few objects this is a fast early-out — if no overlap,
    // the ray travels entirely through default_medium and DDA is not needed.
    static bool ray_touches_objects(space::utility::point3 from,
                                    space::utility::point3 to,
                                    const space::grid::grid_model& grid){
        if(grid.voxels.empty()) return false;

        // Ray AABB in cell space
        double cell = grid.cell_size;
        auto to_cell = [&](double w){ return w / cell; };

        double rx0 = std::min(to_cell(from.x), to_cell(to.x));
        double ry0 = std::min(to_cell(from.y), to_cell(to.y));
        double rz0 = std::min(to_cell(from.z), to_cell(to.z));
        double rx1 = std::max(to_cell(from.x), to_cell(to.x));
        double ry1 = std::max(to_cell(from.y), to_cell(to.y));
        double rz1 = std::max(to_cell(from.z), to_cell(to.z));

        for(const auto& [cp, _] : grid.voxels){
            if(cp.x >= rx0 && cp.x <= rx1 &&
               cp.y >= ry0 && cp.y <= ry1 &&
               cp.z >= rz0 && cp.z <= rz1)
                return true;
        }
        return false;
    }

    // 3D DDA traversal — steps through every voxel the ray crosses, accumulating
    // the phase integral (k * chord_length) and attenuation integral (alpha * chord_length)
    // for the medium in each voxel.
    static ray_integral dda(space::utility::point3 from,
                             space::utility::point3 to,
                             double frequency,
                             const space::grid::grid_model& grid){
        double cell = grid.cell_size;

        // Ray in voxel coordinates
        double ox = from.x / cell;
        double oy = from.y / cell;
        double oz = from.z / cell;
        double dx = (to.x - from.x) / cell;
        double dy = (to.y - from.y) / cell;
        double dz = (to.z - from.z) / cell;
        double len = std::sqrt(dx*dx + dy*dy + dz*dz);

        if(len < 1e-10){
            auto med = grid.default_medium;
            double k = 2.0 * std::numbers::pi * frequency / med.sound_speed;
            return {k * 0.0, med.absorption * 0.0};
        }

        // Normalised ray direction
        double ux = dx / len;
        double uy = dy / len;
        double uz = dz / len;

        // Current voxel
        int cx = static_cast<int>(std::floor(ox));
        int cy = static_cast<int>(std::floor(oy));
        int cz = static_cast<int>(std::floor(oz));

        // Step direction per axis
        int sx = ux > 0.0 ? 1 : (ux < 0.0 ? -1 : 0);
        int sy = uy > 0.0 ? 1 : (uy < 0.0 ? -1 : 0);
        int sz = uz > 0.0 ? 1 : (uz < 0.0 ? -1 : 0);

        // t at which ray crosses next voxel boundary in each axis
        auto next_boundary = [](double o, double u, int s) -> double {
            if(s == 0) return std::numeric_limits<double>::infinity();
            double boundary = s > 0 ? std::floor(o) + 1.0 : std::ceil(o) - 1.0;
            return (boundary - o) / u;
        };

        double t_max_x = next_boundary(ox, ux, sx);
        double t_max_y = next_boundary(oy, uy, sy);
        double t_max_z = next_boundary(oz, uz, sz);

        // How far along the ray (in voxel units) to cross one full voxel per axis
        double t_delta_x = sx != 0 ? std::abs(1.0 / ux) : std::numeric_limits<double>::infinity();
        double t_delta_y = sy != 0 ? std::abs(1.0 / uy) : std::numeric_limits<double>::infinity();
        double t_delta_z = sz != 0 ? std::abs(1.0 / uz) : std::numeric_limits<double>::infinity();

        double t = 0.0;
        double phase_acc = 0.0;
        double atten_acc = 0.0;

        int gx = static_cast<int>(grid.grid.x);
        int gy = static_cast<int>(grid.grid.y);
        int gz = static_cast<int>(grid.grid.z);

        while(t < len){
            // Chord length through this voxel (in voxel units, convert to metres)
            double t_next = std::min({t_max_x, t_max_y, t_max_z, len});
            double chord = (t_next - t) * cell;

            // Resolve medium — stamped voxel or default
            const space::medium::medium_model* med = &grid.default_medium;
            if(cx >= 0 && cy >= 0 && cz >= 0 && cx < gx && cy < gy && cz < gz){
                space::utility::cell_point cp{
                    static_cast<uint16_t>(cx),
                    static_cast<uint16_t>(cy),
                    static_cast<uint16_t>(cz)
                };
                auto found = grid.voxels.find(cp);
                if(found) med = &found->medium;
            }

            double k = 2.0 * std::numbers::pi * frequency / med->sound_speed;
            phase_acc += k     * chord;
            atten_acc += med->absorption * chord;

            // Step to next voxel
            if(t_max_x < t_max_y && t_max_x < t_max_z){
                cx     += sx;
                t       = t_max_x;
                t_max_x += t_delta_x;
            } else if(t_max_y < t_max_z){
                cy     += sy;
                t       = t_max_y;
                t_max_y += t_delta_y;
            } else {
                cz     += sz;
                t       = t_max_z;
                t_max_z += t_delta_z;
            }
        }

        return {phase_acc, atten_acc};
    }

    ray_integral traverse(space::utility::point3 from,
                          space::utility::point3 to,
                          double frequency,
                          const space::grid::grid_model& grid){
        // Fast path — homogeneous space, skip DDA
        if(!ray_touches_objects(from, to, grid)){
            double dx = to.x - from.x;
            double dy = to.y - from.y;
            double dz = to.z - from.z;
            double r = std::sqrt(dx*dx + dy*dy + dz*dz);
            auto& med = grid.default_medium;
            double k = 2.0 * std::numbers::pi * frequency / med.sound_speed;
            return {k * r, med.absorption * r};
        }

        return dda(from, to, frequency, grid);
    }
}
