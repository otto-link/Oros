#include <stdexcept>

#include "oros/terrain/grid_mesh_factory.hpp"

namespace oros::terrain
{

GridMesh GridMeshFactory::create_unit_grid(size_t resolution)
{
  if (resolution < 3 || (resolution % 2 == 0))
  {
    throw std::invalid_argument(
        "Grid resolution must be an odd integer >= 3 (e.g. 17, 33, 65)");
  }

  GridMesh mesh;
  mesh.grid_resolution = resolution;

  const size_t num_vertices = resolution * resolution;
  mesh.vertices.reserve(num_vertices);

  const float step = 1.0f / static_cast<float>(resolution - 1);

  // Generate vertices in [0, 1] range
  for (size_t y = 0; y < resolution; ++y)
  {
    const float v = static_cast<float>(y) * step;
    for (size_t x = 0; x < resolution; ++x)
    {
      const float u = static_cast<float>(x) * step;
      mesh.vertices.push_back({u, v});
    }
  }

  // Generate triangle indices (2 triangles per quad cell)
  const size_t quads_per_side = resolution - 1;
  const size_t num_indices = quads_per_side * quads_per_side * 6;
  mesh.indices.reserve(num_indices);

  for (size_t y = 0; y < quads_per_side; ++y)
  {
    for (size_t x = 0; x < quads_per_side; ++x)
    {
      const uint32_t i0 = static_cast<uint32_t>(y * resolution + x);
      const uint32_t i1 = static_cast<uint32_t>(y * resolution + (x + 1));
      const uint32_t i2 = static_cast<uint32_t>((y + 1) * resolution + x);
      const uint32_t i3 = static_cast<uint32_t>((y + 1) * resolution + (x + 1));

      // Triangle 1
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i1);

      // Triangle 2
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i3);
    }
  }

  return mesh;
}

} // namespace oros::terrain
