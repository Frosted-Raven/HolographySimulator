# Acoustic Holography Engine — Implementation Status

## Actual File Structure

```
src/
├── main.cpp                               # Empty stub
├── transducer/
│   ├── transducer.hpp / transducer.cpp    # Complete
│   └── transducer_array.hpp / .cpp        # Complete
└── space/
    ├── medium.hpp / medium.cpp            # Complete
    ├── grid.hpp / grid.cpp                # Complete
    ├── object/
    │   ├── object.hpp / object.cpp        # Complete
    │   └── object_descriptor.hpp / .cpp   # Complete
    └── utility/
        └── spacial_nav.hpp / .cpp         # Complete
```

Everything else — `space.hpp`, `simulation_state.hpp`, `simulation_model.hpp`,
`pressure_field.hpp`, `physics/`, `gui/` — does not exist yet.

The project builds cleanly with clang-18 / libc++.

---

## Component Detail

### `medium` — Complete

Full action/reducer pattern implemented. `lager::match` dispatches over the `actions` variant
for all property changes. `sound_speed` and `acoustic_impedance` are recomputed inside the
relevant action handlers when their dependencies change.

**Divergences from design:**
- `stiffness: double` added as an explicit field; `sound_speed` is always derived via
  `sqrt(stiffness / density)` rather than being stored directly
- `acoustic_impedance` is stored as a field and kept in sync, rather than being a free
  function computed on demand

---

### `utility/spacial_nav.hpp/.cpp` — Complete

Defines shared coordinate types, the `sdf` struct, and grid utility functions under `space::utility`.

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

**Divergences from design:**
- Namespace is `space::utility`, not `space::descriptor`
- Types are named `point3`/`vector3`, not `position`/`vec_position`
- `sdf` lives here rather than in a dedicated `sdf.hpp`
- Grid utility functions live here rather than in a `coords/` layer

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

**`update(shape_model, actions)` reducer** — `lager::match` over 5 action types:
`new_sphere`, `new_cube`, `edit_position`, `edit_sphere`, `edit_cube`.

**Divergences from design:**
- Shape type is `cube_model`, not `box_model`
- `cube_model` includes `rotation: vector3` — design deferred rotation

---

### `object/object.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`object_model` fields:**
- `object_prio: int` — priority for voxel conflict resolution
- `name: std::string`
- `transform_mod: cell_point` — grid-space offset applied during volume generation
- `medium: medium_model` — acoustic properties
- `shape: shape_model` — variant holding sphere or cube authored data
- `sdf_data: utility::sdf` — computed signed distance field
- `volume: immer::map<cell_point, vector3>` — computed voxel map (position → normal)

**`generate_volume(cell_size)`** — walks `sdf_data`, stamps all cells where `distance < 0`
into `volume` as `cell_point → normal` entries, offset by `transform_mod`.

**`update()` reducer** — `lager::match` over 9 action types: `transform_mod`, `medium`,
`shape`, `priority`, `name`, `update_sdf`, `update_volume`, plus pass-through to
`space::medium::update` and `shapes::update` for sub-model actions.

**Divergences from design:**
- `object_prio` lives directly on `object_model`; design put priority on `medium`
- `generate_volume` is a mutating method rather than a pure free function
- Volume stores only normals per voxel — medium is on the object, not per-voxel

---

### `grid.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`grid_model` fields:**
- `grid: cell_quantity` — x/y/z cell counts
- `cell_size: double` — uniform cell size in metres
- `default_medium: medium_model` — baseline medium filling unmarked space
- `voxels: immer::map<cell_point, voxel_data>` — stamped voxel map
- `objects: immer::flex_vector<object_model>` — priority-sorted object list

**`voxel_data`** — `{ vector3 normal, medium_model medium }` — the resolved per-cell data
after stamping.

**`stamp_in()`** — iterates objects in reverse priority order, writes each object's volume
into a fresh `immer::map`, bounds-checking against the grid. Returns the completed voxel map.

**`sort_prio()`** — sorts the object list by `object_prio` ascending. Implemented via
`std::vector` round-trip since `immer::flex_vector` does not provide random-access iterators.

**`update()` reducer** — `lager::match` over 10 action types: `grid_dimensions`, `cell_size`,
`default_medium_action`, `object_action`, `update_object_volume`, `update_object_sdf`,
`new_sphere`, `new_cube`, `update_grid`, `sort_objects`.

**Divergences from design:**
- Grid does not have a world-space `origin` field on the model (origin is a file-scope
  constant used only when constructing new objects)
- `stamp_in` is a method on `grid_model` rather than a free function
- No `position_to_index` / `index_to_position` query interface on the grid itself —
  those live in `spacial_nav::funcs`

---

### `transducer/transducer.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`single_model` fields:**
- `name: std::optional<std::string>`
- `position: space::utility::point3`
- `frequency: double` — Hz
- `amplitude: double` — output pressure magnitude
- `phase: double` — key holography control parameter
- `is_active: bool`

**`update()` reducer** — `lager::match` over 11 action types. Two categories:
- **Absolute setters:** `new_transducer` (reset to defaults), `toggle_active`, `new_name`,
  `new_position`, `new_frequency`, `new_amplitude`, `new_phase`
- **Relative modifiers:** `mod_position`, `mod_frequency`, `mod_amplitude`, `mod_phase` —
  add a delta to the current value

`new_position` and `mod_position` use `std::optional` per-axis so individual axes can be
targeted without affecting others.

---

### `transducer/transducer_array.hpp/.cpp` — Complete

Full action/reducer pattern implemented.

**`tran_array_model` fields:**
- `name: std::optional<std::string>`
- `tran_array: immer::vector<single_model>` — persistent list of transducers

**`update()` reducer** — `lager::match` over 5 action types:
- `new_name` — set array name
- `add_tran` — appends a default-initialised transducer
- `remove_tran` — removes the last transducer (guarded against empty array)
- `group_adjust` — applies a `single::actions` to every transducer in the array
- `single_adjust` — applies a `single::actions` to one transducer by index

**Divergences from design:**
- No bulk `set_all_phases` / `set_all_amplitudes` convenience actions — these are covered
  by `group_adjust` with `new_phase` / `new_amplitude` actions
- No array geometry description (planar, circular etc.) — not yet added to the model

---

## Divergence Summary

| Area | Design Intent | Actual Implementation |
|---|---|---|
| `speed_of_sound` | Stored explicitly | Derived from `stiffness` |
| `acoustic_impedance` | Free function | Stored field, recomputed on change |
| Coordinate namespace | `space::descriptor` | `space::utility` |
| Coordinate type names | `position`/`vec_position` | `point3`/`vector3` |
| `sdf` struct home | Separate `sdf.hpp` | Lives in `spacial_nav.hpp` |
| Grid utility functions | `coords/` layer | `spacial_nav::funcs` |
| Shape type naming | `box_model` | `cube_model` |
| Rotation | Deferred | Present on `cube_model` now |
| Object priority | On `medium` | Direct field on `object_model` |
| Grid query interface | Methods on `Grid` | Free functions in `spacial_nav::funcs` |
| Bulk phase/amplitude | Dedicated actions | Covered by `group_adjust` |
| Array geometry | Described on model | Not yet added |

---

## Next Priorities

1. `space.hpp` — top-level Space struct combining `grid_model` + `tran_array_model`; actions + reducer
2. `simulation_model.hpp` + `simulation_state.hpp` — top-level Lager model
3. `pressure_field.hpp` — complex pressure output; magnitude/phase/intensity accessors
4. Physics solver — point source superposition over the grid
