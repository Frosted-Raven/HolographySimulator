# Acoustic Holography Engine — Implementation Status

## File Structure

```
src/
├── main.cpp
├── world.hpp / world.cpp
├── simulation_model.hpp / simulation_model.cpp
├── simulation/
│   └── simulation_state.hpp / simulation_state.cpp
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

### `simulation/simulation_state.hpp/.cpp` — Complete

Pressure field state and forward solver.

**`state_model` fields:**
- `current: status` — `UNCOMPUTED`, `VALID`, or `OLD`; defaults to `UNCOMPUTED`
- `pressure: std::vector<std::complex<double>>` — flattened 3D pressure field,
  indexed as `z * (x*y) + y * x + x`

**`solve(world_model)`** — point-source pressure superposition forward solver.
For each voxel, resolves the medium (stamped object or grid default), then sums
complex contributions from all active transducers:

```
p += (A / r) * exp(-alpha * r) * exp(i * (k*r + phi))
```

where `k = 2π * f / c` uses the voxel's local sound speed. Rigid voxels are skipped.
Near-zero `r` is guarded against division by zero.

**`update()` reducer** — 2 action types:
- `run_solver{world}` — runs `solve()`, writes result into `pressure`
- `update_status{new_status}` — updates `current` independently (used to mark field `OLD`
  when the world model changes)

---

### `simulation_model.hpp/.cpp` — Complete

Root model for the engine, combining world state and simulation state.

**`simulation_model` fields:**
- `world: world_model`
- `state: state_model`

**`actions`** — `std::variant<world::actions, sim_state::actions>`

**`update()` reducer** — 2 branches:
- `world::actions` — updates `world`, then marks `state` as `OLD` via `update_status`
- `sim_state::actions` — updates `state` directly (used to trigger or inject solver results)

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

- **Pressure field query interface** — magnitude, phase, and intensity accessors over
  the computed field; slice extraction at arbitrary planes
- **Inverse solver** — given a target pressure pattern, compute the transducer phases
  that produce it (the holography problem proper)
- **Array geometry** — planar, circular, and arbitrary array layout descriptions on
  `tran_array_model`
- **Multi-medium ray traversal** — solver currently uses the voxel's local medium;
  phase accumulation along a ray through multiple media is not yet integrated
