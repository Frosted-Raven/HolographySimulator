# Acoustic Holography Engine — Implementation Status

## File Structure

```
src/
├── main.cpp
├── world.hpp / world.cpp
├── simulation_model.hpp / simulation_model.cpp
├── simulation/
│   ├── simulation_state.hpp / simulation_state.cpp
│   ├── ray_traversal.hpp / ray_traversal.cpp
│   ├── inverse_solver.hpp / inverse_solver.cpp
│   └── field_query.hpp / field_query.cpp
├── space/
│   ├── medium.hpp / medium.cpp
│   ├── grid.hpp / grid.cpp
│   ├── object/
│   │   ├── object.hpp / object.cpp
│   │   └── object_descriptor.hpp / object_descriptor.cpp
│   └── utility/
│       └── spacial_nav.hpp / spacial_nav.cpp
└── transducer/
    ├── transducer.hpp / transducer.cpp
    └── transducer_array.hpp / transducer_array.cpp
```

---

## Component Detail

### `utility/spacial_nav.hpp/.cpp` — Complete

Shared coordinate types, the `sdf` struct, and grid utility functions under `space::utility`.

```cpp
namespace space::utility {
    struct point3        { double x, y, z; };
    struct vector3       { double i, j, k; };
    struct cell_quantity { uint16_t x, y, z; };

    struct cell_point {
        uint16_t x, y, z;
        bool operator==(const cell_point&) const noexcept;
    };

    struct sdf {
        std::vector<double>  distance;
        std::vector<vector3> normal;
        point3 origin;
        point3 dimensions;
    };

    namespace funcs {
        point3     index_to_position(point3 origin, cell_point ind, double cell_size);
        cell_point position_to_index(point3 origin, point3 pos, double cell_size);
        bool       in_bound(cell_quantity bound, cell_point point);
    }
}

// std::hash<cell_point> specialisation — required for immer::map keying
```

---

### `medium.hpp/.cpp` — Complete

Full action/reducer pattern implemented. `sound_speed` and `acoustic_impedance` are
recomputed inside the relevant action handlers when their dependencies change.

**`medium_model` fields:**
- `name: std::string`, `priority: uint8_t`
- `sound_speed: double`, `acoustic_impedance: double` — derived, kept in sync
- `density: double`, `absorption: double`, `temperature: double`, `stiffness: double`
- `is_rigid: bool` — blocks pressure propagation in the solver

**`update()` reducer** — `lager::match` over 8 action types.

**Divergences:**
- `stiffness` is an explicit field; `sound_speed` is always derived via `sqrt(stiffness / density)`
- `acoustic_impedance` is stored and kept in sync rather than computed on demand

---

### `object/object_descriptor.hpp/.cpp` — Complete

Shape model layer — fully implemented.

**Types:**
- `space::object::shapes::sphere_model` — `world_position`, `scale` (= radius)
- `space::object::shapes::cube_model` — `world_position`, `scale` (vector3), `rotation` (vector3)
- `using shape_model = std::variant<sphere_model, cube_model>`

**`sphere_model::generate(cell_size)`** — analytical SDF over bounding cube; signed distance
and outward normal per cell.

**`cube_model::generate(cell_size)`** — analytical box SDF; axis-aligned face normals.

**`update()` reducer** — `lager::match` over 5 action types:
`new_sphere`, `new_cube`, `edit_position`, `edit_sphere`, `edit_cube`.

---

### `object/object.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`object_model` fields:**
- `object_prio: int` — priority for voxel conflict resolution
- `name: std::string`
- `transform_mod: cell_point` — grid-space offset applied during volume generation
- `medium: medium_model` — acoustic properties assigned to this object's voxels
- `shape: shape_model` — variant holding sphere or cube authored data
- `sdf_data: utility::sdf` — computed signed distance field
- `volume: immer::map<cell_point, vector3>` — computed voxel map (position → normal)

**`generate_volume(cell_size)`** — walks `sdf_data`, stamps all cells where `distance < 0`
into `volume` as `cell_point → normal` entries, offset by `transform_mod`.

**`update()` reducer** — `lager::match` over 9 action types.

---

### `grid.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`grid_model` fields:**
- `grid: cell_quantity` — x/y/z cell counts
- `cell_size: double` — uniform cell size in metres
- `default_medium: medium_model` — baseline medium filling unmarked space
- `voxels: immer::map<cell_point, voxel_data>` — stamped voxel map
- `objects: immer::flex_vector<object_model>` — priority-sorted object list

**`voxel_data`** — `{ vector3 normal, medium_model medium }` — resolved per-cell data
after stamping.

**`stamp_in()`** — iterates objects in reverse priority order, writes each object's volume
into a fresh `immer::map`, bounds-checking against the grid.

**`sort_prio()`** — sorts the object list by `object_prio` ascending.

**`update()` reducer** — `lager::match` over 10 action types: `grid_dimensions`, `cell_size`,
`default_medium_action`, `object_action`, `update_object_volume`, `update_object_sdf`,
`new_sphere`, `new_cube`, `update_grid`, `sort_objects`.

---

### `transducer/transducer.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`single_model` fields:**
- `name: std::optional<std::string>`
- `position: point3`
- `frequency: double` — Hz
- `amplitude: double` — output pressure magnitude
- `phase: double` — primary holography control parameter
- `is_active: bool`

**`update()` reducer** — `lager::match` over 11 action types. Absolute setters
(`new_position`, `new_frequency`, `new_amplitude`, `new_phase`) and relative modifiers
(`mod_position`, `mod_frequency`, `mod_amplitude`, `mod_phase`). Position and mod_position
use `std::optional` per-axis for targeted single-axis edits.

---

### `transducer/transducer_array.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`tran_array_model` fields:**
- `name: std::optional<std::string>`
- `tran_array: immer::vector<single_model>`

**`update()` reducer** — `lager::match` over 5 action types: `new_name`, `add_tran`,
`remove_tran`, `group_adjust`, `single_adjust`. `group_adjust` applies a `single::actions`
to every transducer in the array — covers bulk phase/amplitude changes.

---

### `world.hpp/.cpp` — Complete

Top-level domain model combining grid and transducer arrays.

**`world_model` fields:**
- `grid: grid_model`
- `transducers: immer::flex_vector<tran_array_model>`

**`update()` reducer** — `lager::match` over 4 action types: `add_array`, `remove_array`,
`mod_array`, and a pass-through for `space::grid::actions`.

---

### `simulation/ray_traversal.hpp/.cpp` — Complete

3D DDA ray traversal through the voxel grid for physically accurate phase and attenuation
accumulation across multiple media.

**`ray_integral`** — `{ double phase, double attenuation }` — the accumulated integrals
of `k(x) dx` and `alpha(x) dx` along the ray path, in radians and nepers respectively.

**`traverse(from, to, frequency, grid)`** — public entry point.

**Fast path** — if the ray's bounding box does not overlap any stamped voxel, the ray
travels entirely through `default_medium` and the result is computed analytically without
DDA. Keeps homogeneous-space solves as cheap as the original single-medium formula.

**DDA traversal** — operates in voxel-space coordinates. At each step, advances to the
nearest axis-boundary crossing, computes the chord length through that voxel in metres,
looks up the medium (`voxels.find` or `default_medium`), and accumulates
`k * chord` and `alpha * chord`. Voxels outside the grid bounds fall back to
`default_medium`.

---

### `simulation/simulation_state.hpp/.cpp` — Complete

Pressure field state and forward solver.

**`state_model` fields:**
- `current: status` — `UNCOMPUTED`, `VALID`, or `OLD`; defaults to `UNCOMPUTED`
- `pressure: std::vector<std::complex<double>>` — flattened 3D pressure field,
  indexed as `z * (x*y) + y * x + x`

**`solve(world_model)`** — point-source pressure superposition forward solver.
For each non-rigid voxel, sums complex contributions from all active transducers:

```
p += (A / r) * exp(-atten_integral) * exp(i * (phase_integral + phi))
```

Phase and attenuation integrals are computed per-ray by `ray_traversal::traverse`,
correctly accounting for multiple media along the path.

**`update()` reducer** — 2 action types:
- `run_solver{world}` — runs `solve()`, writes result into `pressure`
- `update_status{new_status}` — updates `current` independently (used to mark field `OLD`
  when the world model changes)

---

### `simulation/field_query.hpp/.cpp` — Complete

Read-only accessors over a computed pressure field. No action/reducer — purely functional.

**Point accessors** — all guard against uncomputed state, out-of-bounds coordinates, and
vector size mismatch before accessing data:
- `magnitude_at` — `std::abs(pressure[idx])` — acoustic pressure amplitude
- `phase_at` — `std::arg(pressure[idx])` — wave phase in radians [-π, π]
- `intensity_at` — `std::norm(pressure[idx])` — proportional to |p|²

**Slice extraction** — `extract_slice(state, dims, axis, index)` pulls a 2D plane from
the 3D field into a `slice` struct (`data`, `width`, `height`):
- `XY` at fixed `z` — width=x, height=y
- `XZ` at fixed `y` — width=x, height=z
- `YZ` at fixed `x` — width=y, height=z

**Derived slice views** — `magnitude(slice)`, `phase(slice)`, `intensity(slice)` each
return `std::vector<double>` for direct use in colour mapping.

---

### `simulation/inverse_solver.hpp/.cpp` — Complete

Computes transducer phases and amplitudes that produce a desired pressure pattern at a
set of target points.

**`target_point`** — `{ point3 position, std::complex<double> desired_pressure }`

**`params`** — `{ method solver_method, int gs_iterations }`. `method` is either
`backprop` or `gerchberg_saxton`; defaults to GS with 100 iterations.

**`result`** — phases and amplitudes indexed parallel to
`world.transducers[array_idx][tran_idx]`. Inactive transducers and untouched arrays
retain their current values.

**Transfer matrix H** — built once per solve. `H[m][n]` is the unit transfer from
transducer `n` to target `m`, computed via `ray_traversal::traverse` so multi-medium
paths are handled correctly.

**Tier 1 — Backpropagation** — applies `H^*` (conjugate transpose) to the desired
pressure vector. Single pass, non-iterative. Good for simple focal point patterns.

**Tier 2 — Gerchberg-Saxton** — iterates between transducer and target planes:
forward-propagate via `H`, clamp target amplitudes to desired values, back-propagate
via `H^*`, clamp transducer amplitudes to hardware limits. Converges to a phase
distribution that approximates the desired pattern across multiple simultaneous targets.

---

### `simulation_model.hpp/.cpp` — Complete

Root model for the engine, combining world state and simulation state.

**`simulation_model` fields:**
- `world: world_model`
- `state: state_model`

**`actions`** — `std::variant<world::actions, sim_state::actions, action::run_inverse>`

**`update()` reducer** — 3 branches:
- `world::actions` — updates `world`, marks `state` as `OLD`
- `sim_state::actions` — updates `state` directly
- `action::run_inverse{targets, params}` — runs the inverse solver, writes resulting
  phases and amplitudes back into `world.transducers`, marks `state` as `OLD`

Any world edit automatically invalidates the pressure field.

---

## Divergence Summary

| Area | Design Intent | Actual |
|---|---|---|
| `sound_speed` | Stored explicitly | Derived from `stiffness` |
| `acoustic_impedance` | Free function | Stored field, recomputed on change |
| `sdf` struct home | Separate `sdf.hpp` | Lives in `spacial_nav.hpp` |
| Grid utility functions | `coords/` layer | `spacial_nav::funcs` |
| Shape type naming | `box_model` | `cube_model` |
| Rotation | Deferred | Present on `cube_model` |
| Object priority | On `medium` | Direct field on `object_model` |
| Bulk phase/amplitude | Dedicated actions | Covered by `group_adjust` |
| Array geometry | Described on model | Not yet added |

---

## Not Yet Implemented

- **Array geometry** — planar, circular, and arbitrary array layout descriptions on
  `tran_array_model`; auto-population of transducer positions from a layout descriptor
- **Field statistics** — aggregate queries over the pressure field: min/max magnitude,
  RMS pressure, focal point detection; useful for evaluating inverse solver convergence
