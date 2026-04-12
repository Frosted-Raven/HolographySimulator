# Acoustic Holography Engine — Project Context

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

**Key Lager action flow for objects:**
A `ChangeObject` action triggers a two stage recomputation effect. If authored data changes, both SDF and voxels are recomputed. If only dx changes, only voxels are recomputed. The rest of the scene is untouched.

---

## File Structure
```
acoustic_engine/
│
├── src/
│   ├── space/
│   │   ├── medium.hpp/cpp            # Acoustic medium — used for both space and barriers
│   │   ├── grid.hpp                  # 3D spatial grid
│   │   ├── space.hpp                 # Top-level spatial definition
│   │   ├── objects/
│   │   │   ├── sdf_model.hpp         # Abstract base — world_position, scale, virtual generate()
│   │   │   ├── sphere_model.hpp/cpp  # Sphere SDF generation
│   │   │   ├── box_model.hpp/cpp     # Box SDF generation
│   │   │   └── object.hpp            # Object — authored data + computed SDF + voxels
│   │   └── utility/
│   │       └── spacial_nav.hpp       # Shared coordinate vocabulary
│   │
│   ├── transducer.hpp
│   ├── transducer_array.hpp
│   ├── pressure_field.hpp
│   ├── simulation_state.hpp
│   ├── simulation_model.hpp          # Top-level Lager model
│   ├── actions.hpp                   # All action types + std::variant
│   │
│   ├── physics/
│   │   ├── wave_solver.hpp/cpp       # Core solver interface
│   │   └── helmholtz_solver.hpp/cpp  # Frequency domain implementation
│   │
│   └── reducer.hpp/cpp               # Pure update() function
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
├── Space                                  # Static environment definition
│   ├── Grid                               # Spatial definition + query interface
│   │   └── default_medium                 # Baseline Medium (air etc.)
│   ├── Objects []                         # immer::vector of Objects
│   │   └── Object
│   │       ├── medium                     # Acoustic properties + priority
│   │       ├── sdf_model*                 # Pointer to shape (sphere, box etc.)
│   │       ├── generator_type             # Enum for serialization (sphere, box)
│   │       ├── sdf                        # Computed — distances + normals
│   │       ├── immer::flex_vector<Voxel>  # Computed — sampled from SDF at dx
│   │       └── dx                         # Resolution voxels were computed at
│   └── TransducerArray
│       └── Transducers []                 # immer::vector
│
└── SimulationState                        # Dynamic solver output
    ├── PressureField                      # plain std::vector internally
    └── Solver settings
```

**Key principles:**
- Space is static geometry. SimulationState is the computed result of running the solver against a Space. They are explicitly separate.
- An Object has two kinds of data — authored (medium, sdf_model, generator_type) and computed (sdf, voxels, dx). Computed data is always derived from authored data and should never be set directly.
- The two stage computation: authored data changes → recompute SDF → recompute voxels. dx change only → skip SDF, recompute voxels only.
- Grid does not own voxels — it is a spatial definition and query interface. Voxels live on Objects.

---

## Component Definitions

### `medium.hpp`
Single struct describing acoustic properties of any matter — used for both the propagation medium (air, water etc.) and barriers (plastic, felt etc.). There is no separate Material type.

```cpp
struct medium_model {
    std::string name;                   // optional, useful for presets
    double      temperature;            // °C
    double      density;                // ρ — kg/m³
    double      stiffness;              // bulk modulus — speed of sound derived from this
    double      acoustic_impedance;     // stored for performance, kept in sync with ρ and c
    double      absorption_coefficient; // α — energy loss over distance
    bool        is_rigid = false;       // true for hard boundaries, simplifies solver math
    int         priority = 0;           // higher priority wins when two objects occupy the same voxel
};
```

**Speed of sound** is derived from stiffness and density: `c = sqrt(stiffness / density)`. It is not stored directly. This is physically sounder than storing speed explicitly — stiffness is a genuine material property that works universally across all media.

**Derived quantities computed as free functions** (require both sides of a boundary, calculated at solve time):
- `speed_of_sound(medium)` → `sqrt(stiffness / density)`
- `reflection_coefficient(medium_a, medium_b)` → `(Z_a - Z_b) / (Z_a + Z_b)`
- `transmission_coefficient(medium_a, medium_b)` → `1 - R`

**Named constructors** for convenience:
```cpp
static medium_model air(double temperature_celsius);
// others (water, tissue etc.) added as needed
```

---

### `objects/sdf_model.hpp`
Abstract base for all shape types. Defines the shared authored data and the pure virtual generation interface:

```cpp
struct sdf_model {
    space::descriptor::position world_position;
    double scale;

    virtual SDF generate(double dx) = 0;
};
```

Rotation is deferred — to be added later without restructuring.

---

### SDF struct
The computed output of a generator. Stored on Object, produced by `sdf_model::generate()`:

```cpp
struct SDF {
    std::vector<double>                          distances;  // flat 1D, negative inside positive outside
    std::vector<space::descriptor::vec_position> normals;    // flat 1D, one per cell
    space::descriptor::position                  origin;     // world space corner of coverage
    space::descriptor::vec_position              dimensions; // physical size of coverage in meters
};
```

nx, ny, nz are derived at query time from dimensions and dx — not stored. The SDF has no resolution of its own, it uses whatever dx the grid provides.

---

### `objects/sphere_model.hpp`
Extends `sdf_model`. No additional authored data beyond `world_position` and `scale` (scale = radius).

`generate(dx)` — analytical SDF generation:
```
for each cell in bounding box (2×radius on each side):
    d = length(cell_center - world_position) - radius
    normal = normalize(cell_center - world_position)
    store d and normal
```

---

### `objects/box_model.hpp`
Extends `sdf_model`. Adds `dimensions` (width, height, depth). Rotation deferred to later.

`generate(dx)` — analytical SDF generation using standard box SDF formula:
```
for each cell in bounding box:
    transform cell into box local space
    q = abs(local) - (dimensions / 2)
    d = length(max(q, 0)) + min(max(q.x, q.y, q.z), 0)
    normal = outward face normal of nearest face
    store d and normal
```

---

### `objects/object.hpp`
Owns both authored and computed data. Voxel computation lives here as a method.

**Authored data:**
- `medium` — acoustic properties and priority
- `sdf_model*` — pointer to shape (sphere_model or box_model)
- `generator_type` — enum (sphere, box) for serialization

**Computed data:**
- `sdf` — generated by calling `sdf_model->generate(dx)`
- `immer::flex_vector<Voxel>` — sampled from SDF at grid's dx
- `dx` — resolution voxels were computed at

**Methods:**
- `compute_sdf(dx)` — calls `sdf_model->generate(dx)`, stores result
- `compute_voxels(dx)` — walks SDF, writes Voxels where distance < 0, respects priority
- `update(dx)` — orchestrates both stages, checks what needs recomputing

---

### `grid.hpp`
The spatial definition of the simulation space. Purely Cartesian. Does **not** own voxels — voxels live on Objects. Grid is a spatial definition and query interface only.

- `origin` — world space corner using `space::descriptor::position`
- `physical_dimensions` — width, height, depth in meters (rectangular, not cubic)
- `dx` — uniform cell size in meters (constrained by wavelength: `dx ≤ λ/10`)
- `nx, ny, nz` — derived cell counts (`physical_size / dx`)
- `default_medium` — the baseline Medium filling the space (e.g. air)

**Methods:**
- `index_to_position(i,j,k)` → physical (x,y,z) of cell center
- `position_to_index(x,y,z)` → nearest (i,j,k)
- `is_in_bounds(i,j,k)` → bool
- `query(i,j,k)` → Voxel — checks all Objects for a voxel at this index respecting priority, falls back to default medium if none found

**Current implementation divergences to fix:**
- `origin` is currently `std::vector<int>` — should use `space::descriptor::position`
- Grid is currently implicitly cubic via `single_axis_d` — should support independent width/height/depth
- `cell_size` is currently `int` — should be `double dx`
- `resolution` semantics are currently inverted
- Grid methods not yet implemented

---

### `utility/spacial_nav.hpp`
Defines shared coordinate descriptor types used across grid and objects:

```cpp
namespace space::descriptor {
    struct position      { double x, y, z; };    // world-space coordinates
    struct vec_position  { double i, j, k; };    // vectors, normals, dimensions
    struct cell_quantity { uint16_t x, y, z; };  // grid cell counts
}
```

---

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
- 3D array of **complex values** (need magnitude and phase per voxel) — plain `std::vector`
- Dimensions matching the grid
- Metadata: solver settings and Space configuration used to produce it
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
- **`immer::vector<Object>`** — object list in Space. Structural sharing means modifying one object doesn't copy the others
- **`immer::flex_vector<Voxel>`** — cached voxels inside each Object. flex_vector chosen because voxels are added and removed during recalculation, requiring efficient insertion and removal
- **`immer::vector<Transducer>`** — transducer list in TransducerArray

### Where NOT To Use Immer
These are recomputed wholesale — structural sharing provides no benefit:
- SDF distances and normals — use plain `std::vector`
- Complex pressure values in PressureField — use plain `std::vector`

### Transients
Immer transients allow efficient batch mutations before converting back to an immutable structure. Useful during `compute_voxels()` when many voxels are being written at once — convert to transient, write all voxels, convert back.

---

## Coordinate System
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
8. **Rotation** — deferred, to be added to sdf_model without restructuring

---

## Current Development Focus
`medium` and `sphere_model`/`box_model` are implemented. `object.hpp` is next — specifically `compute_voxels()` and the two stage update trigger. Then grid fixes before moving to the solver.

### Immediate Priorities
1. Implement `object.hpp` — compute_sdf, compute_voxels, update
2. Fix grid semantic issues (origin type, cubic assumption, dx as double, resolution semantics)
3. Implement grid methods — `index_to_position`, `position_to_index`, `is_in_bounds`, `query`
4. Move to transducer and solver layer
