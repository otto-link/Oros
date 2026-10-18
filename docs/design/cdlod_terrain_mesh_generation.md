# CDLOD Terrain Mesh Generation Design Notes

## 1. Overview & Core Principles

Traditional terrain LOD algorithms (such as ROAM or chunked terrain with discrete meshes) typically rebuild geometry on the CPU or switch between discrete mesh resolutions at chunk boundaries. This leads to two major issues:

1. **CPU/GPU Bus Bottleneck**: The CPU spends excessive time stitching triangles or stitching skirts to avoid T-junction cracks.
1. **Visual Popping**: When moving the camera, vertices visibly "pop" as a chunk abruptly transitions from LOD $k$ to LOD $k+1$.

**CDLOD (Continuous Distance-Dependent Level of Detail)** addresses these limitations through three core principles:

### A. A Single, Static Mesh Grid (GPU Instancing)

- The GPU stores **only one immutable flat unit grid** (e.g. $33 \\times 33$ vertices spanning $[0, 1] \\times [0, 1]$), generated once at startup by `GridMeshFactory`.
- This mesh is never modified on the CPU or re-uploaded. It is reused for every active node across every LOD level.
- Scale, position, and elevation displacement are applied entirely on the GPU in the vertex shader by sampling the elevation raster texture.

### B. Hierarchical Quadtree Selection

- The heightmap patch is divided recursively into a quadtree.
- Starting from the coarsest root level down to the finest leaf level, each node is evaluated against distance thresholds relative to the camera:
  - If a node is **close** to the camera and can be subdivided, it branches into 4 smaller child nodes.
  - If a node is **far** from the camera (or reaches the maximum detail level), it stops subdividing and becomes an active render instance.
- Finer nodes cover smaller geographic areas with higher vertex density; distant nodes cover larger areas with lower vertex density.

### C. Continuous Vertex Morphing (Eliminating Cracks and Popping)

- When adjacent nodes have different LOD levels (e.g., a LOD 0 node next to a LOD 1 node), vertices along the shared boundary would normally produce cracks.
- CDLOD computes a continuous morph factor $k \\in [0.0, 1.0]$ based on camera distance:
  $$k = \\text{clamp}\\left(\\frac{\\text{dist} - \\text{morph_start}}{\\text{morph_range}}, 0.0, 1.0\\right)$$
- In the vertex shader, odd grid vertices smoothly interpolate toward their even neighbors as the camera moves away.
- When $k = 1.0$, the vertex spacing matches the coarser neighbor level, creating a **seamless transition with zero popping and zero T-junction cracks**, without requiring any CPU stitching.

## 2. Architecture & Data Flow

```
               [ User / Scene / Camera ]
                           |
             Camera Pos & LOD Thresholds
                           v
              +--------------------------+
              |  CDLODSelectionContext   |
              +------------+-------------+
                           |
                           v
              +--------------------------+
              |      CDLODQuadTree       |
              +------------+-------------+
                     |             |
           (Queries Bounds)    (Traverses Nodes)
                     |             |
                     v             v
       +-----------------------+   +----------------------+
       |     HeightmapData     |   |    HeightmapPatch    |
       | - Min/Max Mip Pyramid |   | - center, lx, ly     |
       | - Elevation values    |   | - rotation_deg       |
       |                       |   | - local_to_world()   |
       +-----------------------+   +----------------------+
                                               |
                                               v
                                   +-----------------------+
                                   |   AsyncLODSelector    |
                                   | (std::future / async) |
                                   +-----------+-----------+
                                               |
                                               v
                                   Produces vector of:
                                   +-----------------------+
                                   |     CDLODInstance     |
                                   | - offset_u, offset_v  |
                                   | - scale_u, scale_v    |
                                   | - morph_start, range  |
                                   | - lod_level           |
                                   +-----------+-----------+
                                               |
                                               v
                                 [ Renderer / Filament ]
                         DrawInstanced(shared_unit_grid, instances)
```

## 3. Class Responsibilities

### `HeightmapData` (`oros::terrain::HeightmapData`)

- **Elevation Raster & Pyramid**: Owns the elevation float buffer and maintains a hierarchical **min/max mip pyramid** (each pyramid node stores ${z\_{\\min}, z\_{\\max}}$ for a $2 \\times 2$ block of children).
- **Fast Regional Queries**: `get_elevation_range_in_rect(u0, v0, u1, v1)` returns conservative elevation bounds for any sub-region in $O(1)$ to logarithmic time.
- **Dynamic Updates**: `update_region(start_x, start_y, w, h, data)` writes a sub-region and bubbles min/max adjustments up ancestor pyramid nodes without recomputing the entire heightmap.

### `HeightmapPatch` (`oros::terrain::HeightmapPatch`)

- **Oriented Spatial Entity**: Encapsulates patch placement:
  - `id`: Unique identifier.
  - `center`: World position $(x, y, z)$.
  - `lx, ly`: Horizontal dimensions in world units.
  - `rotation_deg`: Yaw rotation around the vertical Z-axis.
- **Coordinate Mapping**: Provides bidirectional transformations:
  - `local_to_world(u, v, z)`: Maps normalized $[0, 1] \\times [0, 1]$ coordinates to world space.
  - `world_to_local(pos)`: Inverse transform from world coordinates to normalized patch space.
- **AABB Calculation**: Computes conservative world-space Axis-Aligned Bounding Box (AABB) accounting for patch rotation and elevation bounds.

### `GridMeshFactory` (`oros::terrain::GridMeshFactory`)

- **Shared Static Geometry**: Generates a standard $N \\times N$ unit grid mesh (e.g. $33 \\times 33$ vertices, with $32 \\times 32 \\times 2$ triangles).
- **Zero Runtime Allocation**: Created once at initialization and shared across all heightmap patches and LOD levels.

### `CDLODQuadTree` (`oros::terrain::CDLODQuadTree`)

- **Selection Traversal**: Performs hierarchical traversal for a given patch and camera state:
  - Evaluates distance between camera position and node center in world coordinates.
  - Subdivides closer nodes into four quadrants down to maximum LOD depth.
  - Emits `CDLODInstance` descriptors for selected nodes containing UV offsets, scales, and morph transition intervals.

### `AsyncLODSelector` (`oros::terrain::AsyncLODSelector`)

- **Asynchronous Execution**: Dispatches quadtree selection as non-blocking tasks (`std::future<std::vector<CDLODInstance>>`).
- **Decoupled Render Loop**: The main rendering thread consumes the latest available instance buffer without waiting on CPU scene graph traversal, ensuring smooth 60+ FPS frame rates.

## 4. GPU Shader Integration (Concept)

When rendering, the graphics engine (e.g., Filament) issues an instanced draw call using the single shared unit grid:

```glsl
// Vertex Shader (e.g. Filament / GLSL)
in vec2 in_unit_pos; // [0, 1] from GridMeshFactory

// Per-instance uniform or instance buffer:
uniform vec4 u_offset_scale; // (offset_u, offset_v, scale_u, scale_v)
uniform vec2 u_morph_params; // (morph_start, morph_range)

void main() {
    // 1. Calculate heightmap UV for this vertex
    vec2 uv = u_offset_scale.xy + in_unit_pos * u_offset_scale.zw;

    // 2. Continuous CDLOD morphing to prevent seams with coarser neighbor
    vec3 world_pos = calculate_world_pos(uv);
    float dist = distance(camera_pos, world_pos);
    float k = clamp((dist - u_morph_params.x) / u_morph_params.y, 0.0, 1.0);

    // Morph odd vertices towards even neighbors using frac/step:
    vec2 morphed_grid_pos = morph_vertex(in_unit_pos, k);
    vec2 final_uv = u_offset_scale.xy + morphed_grid_pos * u_offset_scale.zw;

    // 3. Sample elevation texture
    float height = texture(elevation_map, final_uv).r;
    gl_Position = projection_view * vec4(apply_patch_transform(final_uv, height), 1.0);
}
```
