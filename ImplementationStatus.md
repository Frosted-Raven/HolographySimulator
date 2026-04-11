# Acoustic Holography Engine — Implementation Status

## Files Implemented

The actual source lives under `src/space/`, not the planned `src/model/`. Only 5 files exist:

| File | Status |
|---|---|
| `src/space/medium.hpp` + `medium.cpp` | Implemented |
| `src/space/grid.hpp` | Partial skeleton |
| `src/space/object.hpp` | Early skeleton |
| `src/space/utility/spacial_nav.hpp` | Implemented (not in original design) |

Everything else — `space.hpp`, `transducer.hpp`, `transducer_array.hpp`, `pressure_field.hpp`, `simulation_state.hpp`, `simulation_model.hpp`, `sdf.hpp`, `coords/`, `physics/`, `gui/`, `io/` — does not exist yet.

---

## Component Detail

### `medium` — Most Complete

The `medium_model` struct and its full action/reducer pattern are implemented. `lager::match` dispatches over the `actions` variant for all property changes. Derived quantities (`sound_speed`, `acoustic_impedance`) are recomputed inside the relevant action handlers whenever their dependencies change.

**Additions vs. design:**
- `name: std::string` — not in the design doc; useful for future presets
- `stiffness: double` — new physical property; used to derive `sound_speed` via `sqrt(stiffness / density)` (bulk modulus formula)
- `acoustic_impedance` is **stored** as a field rather than being a derived free function

**Structural divergence — how speed is handled:**
The design says `speed_of_sound` is *"stored explicitly, not derived, to support non-air media."* The implementation does the opposite: speed is always derived from `stiffness` and `density`. These are conflicting philosophies. Setting air's speed directly (e.g. 343 m/s) requires back-calculating a stiffness value under the current approach.

**`acoustic_impedance` storage:**
The design intended `acoustic_impedance(medium)` as a free function computed on demand (`ρ × c`). The implementation stores it as a field and keeps it in sync by recomputing it inside the `density` and `stiffness` action handlers.

---

### `grid` — Skeleton

The `grid_data` struct exists with the core fields and the `immer::flex_vector<voxel>`. The `voxel` struct is defined inline here rather than in a separate `voxel.hpp`.

**Structural divergences vs. design:**

| Design | Implementation |
|---|---|
| `origin` as typed world-space coordinate | `std::vector<int> origin` |
| `physical_dimensions` (width, height, depth) | `single_axis_d: double` — implies cubic grid only |
| `dx` — cell size in meters (`double`) | `cell_size: int` |
| `nx, ny, nz` derived cell counts | Not present |
| `resolution = physical_size / dx` | `resolution = cell_size * single_axis_d` — inverted semantics |

The design treats `dx` as a small cell size from which the total cell count is derived. The implementation's `resolution` field is `cell_size × single_axis_d`, making it a large number with different semantics.

The assumption of `single_axis_d` for all axes means the grid is implicitly cubic. The design specified a rectangular grid with independent width, height, and depth.

**Methods not yet implemented:** `index_to_position`, `position_to_index`, `is_in_bounds`, `query`.

---

### `object` — Major Structural Divergence

The design specified a **virtual base class hierarchy** with `Object` as an interface and `Sphere`, `Box`, and `MeshObject` as concrete subtypes. `contains()` and `stamp_onto()` were to be the core virtual interface.

The implementation uses a **single flat struct** `object_model` with no virtual dispatch. More significantly, the SDF data (`distances`, `normals`) is baked directly into `object_model` rather than living in a separate `SDF` type. This collapses the intended three-layer design (`sdf.hpp` + `mesh_object.hpp` + `object.hpp`) into one struct.

The `distances` and `normals` fields use `immer::vector` — the design specified plain `std::vector` for SDF data since it is recomputed wholesale and does not benefit from structural sharing.

No `contains()` or `stamp_onto()` logic is implemented yet. The `actions` namespace is present but empty.

---

### `spacial_nav.hpp` — New Addition

Not in the original design. Defines three shared descriptor types under `space::descriptor`:

- `position` — `{x, y, z}` doubles for world-space coordinates
- `vec_position` — `{i, j, k}` doubles for vectors and normals
- `cell_quantity` — `{x, y, z}` as `uint16_t` for grid cell counts

This provides the shared coordinate vocabulary used by `grid.hpp` and `object.hpp`. It is a sensible addition that consolidates types the design left scattered or implicit.

---

## Divergence Summary

| Area | Design Intent | Actual Implementation |
|---|---|---|
| `acoustic_impedance` | Derived free function | Stored field, recomputed on change |
| `speed_of_sound` | Stored explicitly | Derived from `stiffness`; cannot be set directly |
| `stiffness` | Not in design | Added as new field; drives speed derivation |
| Grid dimensions | Rectangular (`width × height × depth`) | Implicitly cubic (`single_axis_d`) |
| Grid `dx` | `double`, fine cell size | `int cell_size`, semantics differ |
| Grid methods | Four methods marked highest priority | None implemented |
| Object hierarchy | Virtual base + Sphere/Box/Mesh subtypes | Single flat `object_model` struct |
| SDF | Separate `SDF` type in `sdf.hpp` | Absorbed into `object_model` directly |
| Coordinate utilities | Implicit / part of `coords/` layer | Centralised in new `spacial_nav.hpp` |
