#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oros::terrain
{

struct Vertex2D
{
  float u{0.0f};
  float v{0.0f};
};

struct GridMesh
{
  std::vector<Vertex2D> vertices{};
  std::vector<uint32_t> indices{};
  size_t                grid_resolution{33};
};

// --- GridMeshFactory
// Generates shared, static unit grid meshes in [0, 1]^2 for CDLOD rendering.

class GridMeshFactory
{
public:
  // Generates a regular N x N grid of vertices with triangle list indices
  // N must be odd (e.g., 17, 33, 65) to support power-of-two quadtree steps.
  [[nodiscard]] static GridMesh create_unit_grid(size_t resolution = 33);
};

} // namespace oros::terrain
