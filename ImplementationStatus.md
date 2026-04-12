# Acoustic Holography Engine — Implementation Status

## Actual File Structure

```
src/
├── main.cpp                               # Empty stub
└── space/
    ├── medium.hpp / medium.cpp            # Complete
    ├── grid.hpp                           # Skeleton only
    ├── object.hpp                         # Stale — superseded, should be removed
    ├── object/
    │   ├── object.hpp                     # Object model struct (no actions/reducer yet)
    │   └── object_descriptor.hpp / .cpp   # Shape SDF generation + shape reducer — Complete
    └── utility/
        └── spacial_nav.hpp               # Coordinate types + sdf struct — Complete
```

Everything else — `space.hpp`, `transducer.hpp`, `transducer_array.hpp`, `pressure_field.hpp`,
`simulation_state.hpp`, `simulation_model.hpp`, `coords/`, `physics/`, `gui/` — does not exist yet.

---

## Component Detail

### `medium` — Complete

Full action/reducer pattern implemented. `lager::match` dispatches over the `actions` variant
for all property changes. `sound_speed` and `acoustic_impedance` are recomputed inside the
relevant action handlers when their dependencies change.

**Structural divergences from design:**
- `stiffness: double` added as an explicit field; `sound_speed` is always derived via
  `sqrt(stiffness / density)` rather than being stored directly
- `acoustic_impedance` is stored as a field and kept in sync, rather than being a free
  function computed on demand

---

### `utility/spacial_nav.hpp` — Complete

Defines shared coordinate types and the `sdf` struct under `space::utility`.

```cpp
namespace space::utility {
    struct point3      { double x, y, z; };      // world-space position
    struct vector3     { double i, j, k; };       // vectors and normals
    struct cell_quantity { uint16_t x, y, z; };  // grid cell counts

    struct sdf {
        std::vector<double>  distance;
        std::vector<vector3> normal;
        point3 origin;
        point3 dimensions;
    };
}
```

**Divergences from design:**
- Namespace is `space::utility`, not `space::descriptor` as the context doc specifies
- Types are named `point3`/`vector3`, not `position`/`vec_position`
- `sdf` lives here rather than in a dedicated `sdf.hpp`
- `sdf::dimensions` is `point3` rather than `vec_position`

---

### `object/object_descriptor.hpp/.cpp` — Complete

Defines and implements the shape model layer. Fully implemented.

**Types:**
- `space::object::shapes::sphere_model` — `world_position`, `scale` (= radius)
- `space::object::shapes::cube_model` — `world_position`, `scale` (vector3), `rotation` (vector3)
- `using shape_model = std::variant<sphere_model, cube_model>`

**`sphere_model::generate(cell_size)`** — builds bounding cube, iterates cells, computes
signed distance `|cell_center - center| - radius` and normalised outward normal analytically.

**`cube_model::generate(cell_size)`** — builds bounding box, iterates cells, uses standard
box SDF formula (`q = |local| - dims/2`, exterior/interior distance combined), computes
axis-aligned face normals.

**`update(shape_model, actions)` reducer** — `lager::match` over 5 action types:
`new_sphere`, `new_cube`, `edit_position`, `edit_sphere`, `edit_cube`.

**Divergences from design:**
- Shape type is `cube_model`, not `box_model`
- `cube_model` includes `rotation: vector3` — design deferred rotation
- `sphere_model::scale` is the radius (same as design intent)

---

### `object/object.hpp` — Struct only, no actions or reducer

The current `object_model`:

```cpp
struct object_model {
    int                    object_prio;
    shapes::shape_model    shape;
    medium::medium_model   medium;
    utility::sdf           sdf_model;
};
```

**What's missing:**
- Actions (no `ChangeObject` equivalent yet)
- Reducer / `update()` function
- `compute_sdf(dx)` — call `shape`'s `generate()`, store result in `sdf_model`
- `compute_voxels(dx)` — walk `sdf_model`, write voxels where distance < 0
- The computed voxel cache (`immer::flex_vector<Voxel>`) is not yet on the struct
- `dx` tracking field not yet on the struct

**Divergences from design:**
- `object_prio` lives directly on `object_model`; design put priority on `medium`
- No distinction between authored and computed data yet

---

### `object.hpp` (root-level, `src/space/object.hpp`) — Stale, should be removed

An older flat `object_model` struct still exists at `src/space/object.hpp` in the same
`space::object` namespace as the new `object/object.hpp`. It uses `immer::vector` for
`distances` and `normals` (design specified plain `std::vector`), has an empty `actions`
namespace, and predates the shape descriptor system. It conflicts with the newer version
and should be deleted.

---

### `grid.hpp` — Skeleton, same issues as before

The `grid_data` struct and `voxel` exist. No actions, no reducer, no methods.

**Structural divergences still to fix:**

| Design | Implementation |
|---|---|
| `origin` as typed world-space coordinate | `std::vector<int> origin` |
| Rectangular `physical_dimensions` (w × h × d) | `single_axis_d: double` — cubic only |
| `dx` — cell size in meters (`double`) | `cell_size: int` |
| `nx, ny, nz` derived cell counts | Not present |
| `resolution = physical_size / dx` | `resolution = cell_size * single_axis_d` — inverted |

**Methods not yet implemented:** `index_to_position`, `position_to_index`, `is_in_bounds`, `query`.

---

## Divergence Summary

| Area | Design Intent | Actual Implementation |
|---|---|---|
| `speed_of_sound` | Stored explicitly | Derived from `stiffness`; cannot be set directly |
| `acoustic_impedance` | Free function | Stored field, recomputed on change |
| Coordinate namespace | `space::descriptor` | `space::utility` |
| Coordinate type names | `position`/`vec_position` | `point3`/`vector3` |
| `sdf` struct home | Separate `sdf.hpp` | Lives in `spacial_nav.hpp` |
| Shape type naming | `box_model` | `cube_model` |
| Rotation | Deferred | Present on `cube_model` now |
| Object priority | On `medium` | Direct field on `object_model` |
| Grid dimensions | Rectangular | Implicitly cubic |
| Grid `dx` | `double` | `int cell_size` |
| Grid methods | Four highest-priority methods | None implemented |
| Voxel cache on object | `immer::flex_vector<Voxel>` | Not yet on struct |

---

## Current Development Focus

Shape SDF generation is done. The immediate next work is completing `object_model`:

### Immediate Priorities
1. **Remove stale `src/space/object.hpp`** — the old flat struct conflicts with the new layout
2. **Add voxel cache + `dx` to `object_model`** — `immer::flex_vector<voxel>` and `double dx`
3. **Implement `compute_sdf(dx)`** — call `shape`'s `generate()`, store into `sdf_model`
4. **Implement `compute_voxels(dx)`** — walk sdf, populate voxel cache (use immer transient for batch writes)
5. **Add object actions + `update()` reducer** — `ChangeObject` triggering the two-stage recompute
6. **Fix `grid.hpp`** — origin type, rectangular dimensions, `dx` as double, resolution semantics
7. **Implement grid methods** — `index_to_position`, `position_to_index`, `is_in_bounds`, `query`
