#include "simulation/inverse_solver.hpp"

#include "simulation/ray_traversal.hpp"
#include "space/grid.hpp"
#include "space/utility/spacial_nav.hpp"

#include <cmath>
#include <complex>
#include <vector>

namespace simulate::inverse_solver{

    // Flat view of a transducer — used to build and index the transfer matrix
    // without caring about the array/transducer nesting during computation.
    struct flat_tran{
        size_t array_idx;
        size_t tran_idx;
        space::utility::point3 position;
        double amplitude;
        double frequency;
    };

    static std::vector<flat_tran> flatten(const world::world_model& world){
        std::vector<flat_tran> out;
        for(size_t ai = 0; ai < world.transducers.size(); ++ai){
            const auto& arr = world.transducers[ai];
            for(size_t ti = 0; ti < arr.tran_array.size(); ++ti){
                const auto& t = arr.tran_array[ti];
                if(t.is_active)
                    out.push_back({ai, ti, t.position, t.amplitude, t.frequency});
            }
        }
        return out;
    }

    // H[m][n] — unit transfer from transducer n to target m.
    // Does not include the transducer's own phase or amplitude;
    // those are the unknowns the solver is finding.
    static std::vector<std::vector<std::complex<double>>> build_H(
        const std::vector<flat_tran>& trans,
        const std::vector<target_point>& targets,
        const space::grid::grid_model& grid)
    {
        size_t M = targets.size();
        size_t N = trans.size();
        std::vector<std::vector<std::complex<double>>> H(
            M, std::vector<std::complex<double>>(N, {0.0, 0.0}));

        for(size_t m = 0; m < M; ++m){
            for(size_t n = 0; n < N; ++n){
                double dx = targets[m].position.x - trans[n].position.x;
                double dy = targets[m].position.y - trans[n].position.y;
                double dz = targets[m].position.z - trans[n].position.z;
                double r  = std::sqrt(dx*dx + dy*dy + dz*dz);

                if(r < 1e-10){
                    H[m][n] = {1.0, 0.0};
                    continue;
                }

                auto ray = ray_traversal::traverse(
                    trans[n].position, targets[m].position, trans[n].frequency, grid);

                H[m][n] = (1.0 / r)
                         * std::exp(-ray.attenuation)
                         * std::exp(std::complex<double>{0.0, ray.phase});
            }
        }
        return H;
    }

    // ── Tier 1 — Backpropagation ─────────────────────────────────────────────
    // Applies the conjugate transpose of H to the desired pressure vector.
    // Fast and non-iterative. Works well for a small number of focal points.
    static std::vector<std::complex<double>> backprop(
        const std::vector<std::vector<std::complex<double>>>& H,
        const std::vector<target_point>& targets,
        const std::vector<flat_tran>& trans)
    {
        size_t M = targets.size();
        size_t N = trans.size();
        std::vector<std::complex<double>> q(N, {0.0, 0.0});

        for(size_t n = 0; n < N; ++n){
            for(size_t m = 0; m < M; ++m)
                q[n] += std::conj(H[m][n]) * targets[m].desired_pressure;
        }
        return q;
    }

    // ── Tier 2 — Gerchberg-Saxton ────────────────────────────────────────────
    // Iterates between transducer and target planes, enforcing amplitude
    // constraints at each. Converges to a phase-only solution that approximates
    // the desired pressure pattern at multiple simultaneous target points.
    static std::vector<std::complex<double>> gerchberg_saxton(
        const std::vector<std::vector<std::complex<double>>>& H,
        const std::vector<target_point>& targets,
        const std::vector<flat_tran>& trans,
        int iterations)
    {
        size_t M = targets.size();
        size_t N = trans.size();

        // Start with unit amplitude, zero phase on all transducers
        std::vector<std::complex<double>> q(N, {1.0, 0.0});
        std::vector<std::complex<double>> p(M, {0.0, 0.0});

        for(int iter = 0; iter < iterations; ++iter){
            // Forward pass — propagate to target points
            for(size_t m = 0; m < M; ++m){
                p[m] = {0.0, 0.0};
                for(size_t n = 0; n < N; ++n)
                    p[m] += H[m][n] * q[n];
            }

            // Target constraint — replace amplitude with desired, keep phase
            for(size_t m = 0; m < M; ++m){
                double mag = std::abs(p[m]);
                if(mag < 1e-10) continue;
                p[m] = std::abs(targets[m].desired_pressure) * (p[m] / mag);
            }

            // Backward pass — conjugate transpose of H applied to constrained targets
            for(size_t n = 0; n < N; ++n){
                q[n] = {0.0, 0.0};
                for(size_t m = 0; m < M; ++m)
                    q[n] += std::conj(H[m][n]) * p[m];
            }

            // Transducer constraint — clamp to hardware amplitude, keep phase
            for(size_t n = 0; n < N; ++n){
                double mag = std::abs(q[n]);
                if(mag < 1e-10) continue;
                q[n] = trans[n].amplitude * (q[n] / mag);
            }
        }
        return q;
    }

    // ── Result assembly ──────────────────────────────────────────────────────

    result solve(const world::world_model& world,
                 const std::vector<target_point>& targets,
                 const params& p)
    {
        // Initialise result from current world state so inactive transducers
        // and any arrays not touched by the solver retain their existing values.
        result res;
        res.phases.resize(world.transducers.size());
        res.amplitudes.resize(world.transducers.size());
        for(size_t ai = 0; ai < world.transducers.size(); ++ai){
            const auto& arr = world.transducers[ai];
            res.phases[ai].resize(arr.tran_array.size());
            res.amplitudes[ai].resize(arr.tran_array.size());
            for(size_t ti = 0; ti < arr.tran_array.size(); ++ti){
                res.phases[ai][ti]     = arr.tran_array[ti].phase;
                res.amplitudes[ai][ti] = arr.tran_array[ti].amplitude;
            }
        }

        if(targets.empty()) return res;

        auto trans = flatten(world);
        if(trans.empty()) return res;

        auto H = build_H(trans, targets, world.grid);

        std::vector<std::complex<double>> q;
        if(p.solver_method == method::backprop)
            q = backprop(H, targets, trans);
        else
            q = gerchberg_saxton(H, targets, trans, p.gs_iterations);

        // Write computed phases and amplitudes back into indexed result
        for(size_t ni = 0; ni < trans.size(); ++ni){
            size_t ai = trans[ni].array_idx;
            size_t ti = trans[ni].tran_idx;
            res.phases[ai][ti]     = std::arg(q[ni]);
            res.amplitudes[ai][ti] = std::abs(q[ni]);
        }

        return res;
    }
}
