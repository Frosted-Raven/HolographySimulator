# Acoustic Holography Physics Engine — Project Context

## Project Goal
Build a 3D acoustic physics engine to simulate and test acoustic holography concepts. The engine is a **testing tool for theoretical ideas**, not a real-time application. The primary output is a computed 3D pressure field that can be visualized and analyzed.

---

## Language & Libraries
- **Language:** C++
- **State Management:** [Lager](https://sinusoid.es/lager/) — a unidirectional data-flow library inspired by Elm/Redux (by Sinusoidal Engineering / arximboldi). Not a data structure library — it manages application state via a Model/Action/Reducer pattern.
- **Immutable Data Structures:** [Immer](https://sinusoid.es/immer/) — a library of persistent and immutable data structures by the same author as Lager. The two are designed to work together. Immer containers use structural sharing so "copying" state on every reducer update is efficient — only changed parts are actually copied, unchanged parts are shared with the previous version.
- **Visualization:** TBD, lives in gui/ layer

---

## Architecture Pattern
Lager's unidirectional data flow:
```
Actions → Reducer → Model → View (GUI)
```
- All state lives in a single **SimulationModel** held by a Lager store
- State is never mutated directly — actions are dispatched, reducer returns new state
- GUI only reads state and dispatches actions, never touches physics directly
- Physics logic lives in pure functions (the reducer and solver)

---

## File Structure
```
acoustic_engine/
│
├── src/
│   ├── model/
│   │   ├── grid.hpp                  # 3D spatial grid
│   │   ├── medium.hpp                # Acoustic medium — used for both space and barriers
│   │   ├── voxel.hpp                 # Voxel struct
│   │   ├── space.hpp                 # Top-level spatial definition
│   │   ├── objects/
│   │   │   ├── object.hpp            # Base object interface
│   │   │   ├── sphere.hpp
│   │   │   ├── box.hpp
│   │   │   └── mesh_object.hpp       # Arbitrary mesh via SDF
│   │   ├── sdf.hpp                   # Signed Distance Field type
│   │   ├── transducer.hpp
│   │   ├── transducer_array.hpp
│   │   ├── pressure_field.hpp
│   │   ├── simulation_state.hpp
│   │   └── simulation_model.hpp      # Top-level Lager model
│   │
│   ├── actions.hpp                   # All action types + std::variant
│   │
│   ├── physics/
│   │   ├── wave_solver.hpp/cpp       # Core solver interface
│   │   └── helmholtz_solver.hpp/cpp  # Frequency domain implementation
│   │
│   ├── coords/
│   │   └── coord_transform.hpp       # Cartesian <-> Spherical utilities
│   │
│   └── reducer.hpp/cpp               # Pure update() function
│
├── io/                               # File loading layer
│   └── mesh_loader.hpp/cpp           # Loads 3D files via assimp, generates SDF
│
├── gui/
│   ├── views/
│   │   ├── slice_view.hpp/cpp        # 2D pressure slice visualization
│   │   └── controls.hpp/cpp          # UI controls, dispatches actions
│   └── app.cpp
│
├── tests/
│   ├── test_grid.cpp
│   ├── test_wave_solver.cpp
│   └── test_reducer.cpp
│
└── main.cpp
```

---

## Core Model Hierarchy
```
SimulationModel
├── Space                          # Static environment definition
│   ├── Grid                       # Voxel space
│   │   ├── default_medium         # Baseline Medium (air etc.)
│   │   └── immer::flex_vector<Voxel>  # Only non-default voxels
│   ├── Objects []                 # immer::vector — barriers and obstacles
│   │   ├── Sphere + Medium
│   │   └── Box + Medium
│   └── TransducerArray
│       └── Transducers []         # immer::vector
│
└── SimulationState                # Dynamic solver output
    ├── PressureField              # plain std::vector internally
    └── Solver settings
```

**Key principle:** Space is static geometry. SimulationState is the computed result of running the solver against a Space. They are explicitly separate so the space can be modified without running the solver, and the solver can be rerun with different settings without redefining the space.

---

## Component Definitions

### `medium.hpp`
Single struct describing acoustic properties of any matter — used for both the propagation medium (air, water etc.) and barriers (plastic, felt etc.). Material is no longer a separate type.

```cpp
struct Medium {
    double temperature;              // °C
    double density;                  // ρ — kg/m³
    double speed_of_sound;           // c — m/s (stored explicitly, not derived, to support non-air media)
    double absorption_coefficient;   // α — energy loss over distance
    bool   is_rigid = false;         // true for hard boundaries, simplifies solver math
    int    priority = 0;             // higher priority wins when two objects stamp the same voxel
};
```

**Derived quantities** — computed as free functions, not stored:
- `acoustic_impedance(medium)` → `ρ × c`
- `reflection_coefficient(medium_a, medium_b)` → `(Z_a - Z_b) / (Z_a + Z_b)`
- `transmission_coefficient(medium_a, medium_b)` → `1 - R`

Reflection and transmission require both sides of a boundary so they can't be precomputed — they are calculated at solve time as free functions in the physics layer.

**Named constructors** for convenience:
```cpp
static Medium air(double temperature_celsius);
// others (water, tissue etc.) added as needed
```

### `voxel.hpp`
A cell in the grid that differs from the default. Only non-default voxels are stored — the vast majority of open space is represented implicitly by the grid's default Medium.

```cpp
struct Voxel {
    int i, j, k;                    // grid index
    std::optional<Medium> medium;   // present if this voxel overrides the default medium
                                    // is_rigid = true inside Medium indicates a barrier
};
```

A Voxel exists in the grid's flex_vector only if its Medium differs from the default. The presence of a Medium with `is_rigid = true` is what identifies a barrier voxel.

### `grid.hpp`
The 3D voxel space. Purely Cartesian. Knows nothing about waves:
- `origin` — (x,y,z) of grid corner in world space
- `physical_dimensions` — width, height, depth in meters
- `dx` — uniform cell size in meters (constrained by wavelength: dx ≤ λ/10)
- `nx, ny, nz` — derived cell counts (physical_size / dx)
- `default_medium` — the baseline Medium filling the space (e.g. air)
- `immer::flex_vector<Voxel>` — only non-default voxels stored here

**Methods:**
- `index_to_position(i,j,k)` → physical (x,y,z) of cell center
- `position_to_index(x,y,z)` → nearest (i,j,k)
- `is_in_bounds(i,j,k)` → bool
- `query(i,j,k)` → Voxel — returns stored voxel if present, otherwise a default voxel constructed from default_medium

**Implementation note:** `flex_vector` is used over `vector` because voxels are added and removed as objects are stamped onto and removed from the grid — `flex_vector` supports efficient insertion and removal where `vector` does not.

### `objects/object.hpp`
Base interface for all physical objects in the space:
- `medium` — instance of Medium describing the object's acoustic properties. `is_rigid = true` for hard barriers
- `position` — Cartesian (x,y,z) world space reference point
- `contains(x,y,z)` → bool — pure virtual, core geometric query
- `stamp_onto(grid)` — walks grid voxels, calls contains(), writes a Voxel with this object's Medium into the grid's flex_vector. Should use bounding box optimization to avoid testing every voxel.

### `objects/sphere.hpp`
- `radius` — meters
- `contains(x,y,z)`: `sqrt((x-cx)² + (y-cy)² + (z-cz)²) <= radius`

### `objects/box.hpp`
- `dimensions` — width, height, depth in meters
- `orientation` — axis-aligned to start, rotation optional later
- `contains(x,y,z)` — bounds check on all three axes

### `transducer.hpp`
A single wave emitter:
- `position` — Cartesian (x,y,z)
- `frequency` (f) — Hz
- `amplitude` (A) — output pressure magnitude
- `phase` (φ) — key holography control parameter
- `is_active` (bool) — toggle without removing

### `transducer_array.hpp`
- `immer::vector<Transducer>` — persistent list, efficient structural sharing on updates
- Array geometry description (planar, circular, hemispherical etc.)
- `set_all_phases(vector<double>)` — bulk phase assignment for hologram solver
- `set_all_amplitudes(vector<double>)`

### `pressure_field.hpp`
Computed solver output — separate from grid because it changes every solve:
- 3D array of **complex values** (need magnitude and phase per voxel)
- Dimensions matching the grid
- Metadata: solver settings and Space reference used to produce it
- `magnitude_at(i,j,k)` → |p|
- `phase_at(i,j,k)` → phase angle
- `intensity_at(i,j,k)` → |p|² / (2ρc)
- `slice_xy(z_index)` → 2D slice for visualization

### `simulation_model.hpp`
Top-level Lager model — what the store holds:
```cpp
struct SimulationModel {
    Space           space;
    SimulationState state;
};
```

---

## Immer Usage Guide

### Where To Use Immer
- **`immer::flex_vector<Voxel>`** — non-default voxels in Grid. flex_vector chosen over vector because voxels are added and removed as objects are stamped/unstamped, requiring efficient insertion and removal
- **`immer::vector<Transducer>`** — transducer list in TransducerArray. Adding, removing, or modifying a single transducer shares all unchanged transducers with the previous state
- **`immer::vector<Object>`** — object/barrier list in Space, same benefit
- **`immer::map`** — good candidate for a medium presets library when that gets added

### Where NOT To Use Immer
The pressure field is recomputed wholesale every solver run — structural sharing provides no benefit when everything changes at once. Use plain `std::vector` for:
- The complex pressure values in PressureField

### Transients
Immer provides transients — a way to perform batch mutations efficiently before converting back to an immutable structure. Useful during `stamp_onto()` when an object is writing many voxels into the grid's flex_vector in sequence. Convert to transient, stamp all voxels, then convert back.

### `sdf.hpp`
A 3D grid of distance values precomputed from a mesh. Negative inside the mesh, positive outside:
```cpp
struct SDF {
    std::vector<double> distances;   // negative inside, positive outside
    std::vector<Vec3>   normals;     // surface normal at each cell
    Vec3    origin;
    double  dx;                      // should match simulation grid dx
    int     nx, ny, nz;

    double  distance_at(Vec3 point) const;
    Vec3    normal_at(Vec3 point) const;
    bool    contains(Vec3 point) const;  // distance < 0
};
```
Surface normals are stored because they are needed for accurate reflection calculations — a capability ray casting and voxelization don't provide as cleanly.

### `objects/mesh_object.hpp`
Extends Object for arbitrary mesh geometry. Internally uses an SDF for efficient spatial queries:
```cpp
struct MeshObject : public Object {
    SDF sdf;

    bool contains(double x, double y, double z) const override {
        return sdf.contains({x, y, z});
    }
};
```

### `io/mesh_loader.hpp`
Loads 3D mesh files via assimp and produces a MeshObject ready to place in the scene:
```cpp
namespace io {
    MeshObject load_mesh(
        const std::string& filepath,   // any format assimp supports (.obj, .stl, .ply etc.)
        const Medium&      medium,     // acoustic properties to stamp
        double             dx          // SDF resolution — match simulation grid dx
    );
}
```
The loader parses the file, generates the SDF from the resulting mesh, and returns a fully formed MeshObject. The rest of the engine sees it as just another Object.

---


**Dual coordinate approach:**
- **Cartesian** — the grid, pressure field, object positions, and visualization all live here permanently
- **Spherical** — used per-transducer inside the wave solver to compute each source's contribution naturally

The conversion is not a full system switch — it's computing r per voxel relative to each transducer:
`r = sqrt((x-x₀)² + (y-y₀)² + (z-z₀)²)`

### `coords/coord_transform.hpp`
Central utility — all coordinate conversions go here:
```cpp
namespace coords {
    double to_r(Vec3 source, Vec3 point);
    double to_theta(Vec3 source, Vec3 point);
    double to_phi(Vec3 source, Vec3 point);
    Vec3 to_cartesian(Vec3 source, double r, double theta, double phi);
}
```

---

## Physics Model

### Wave Equation
The acoustic wave equation:
`∇²p - (1/c²) ∂²p/∂t² = 0`

### Solver Approach: Frequency Domain (Helmholtz)
Assume single frequency: `p(x,t) = P(x)e^(iωt)`

Simplifies to Helmholtz equation:
`∇²P + k²P = 0`  where `k = ω/c = 2πf/c`

**Rationale:** Chosen over time-domain (FDTD) because:
- Directly maps to how holography works (phase/amplitude problem)
- Far less computationally expensive in 3D
- Computes full field in one pass

### Point Source Model
Each transducer emits a spherical wave:
`p(r) = (A/r) × e^(i(kr - φ))`

Total field is superposition of all sources:
`p_total(x) = Σ (Aₙ/rₙ) × e^(i(krₙ - φₙ))`

### Key Output Quantities
- **Pressure amplitude** |p(x)| — where sound is loud/quiet
- **Intensity** I = |p|² / (2ρc) — energy flow
- **Phase field** ∠p(x) — wavefront visualization
- **Acoustic radiation force** F ∝ -∇(|p|²) — for levitation modeling later

---

## Known Oversights To Address Later
1. **Solver interface** — should be abstract so implementations can be swapped
2. **Frequency as simulation property** — currently baked into transducers, should move to SimulationState/solver settings
3. **Medium presets library** — no home for reusable named media (air at various temps, water, tissue etc.) yet
4. **PressureField metadata** — needs to store what produced it
5. **Export/measurement layer** — for extracting data outside the GUI
6. **Scene/Scenario concept** — for comparing multiple Space configurations
7. **Composite objects** — layered media, complex shapes

---

## Current Development Focus
**Space layer only** — specifically `grid.hpp` and its supporting types. SimulationState and the solver are not being implemented yet.

### Grid Implementation Priorities
1. Get the flat array data layout right first
2. Ensure `index_to_position` and `position_to_index` are airtight — everything depends on these
3. Bounding box optimization in `stamp_onto` before tackling large grids
4. dx constraint documentation: dx must satisfy `dx ≤ λ/10` for target frequency
