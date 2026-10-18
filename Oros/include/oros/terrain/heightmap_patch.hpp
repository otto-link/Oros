/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <memory>
#include <string>

#include "oros/terrain/heightmap_data.hpp"

namespace oros::terrain
{

// --- 3D Vector & Matrix helper structures
struct Vec3
{
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct AABB
{
  Vec3 min{};
  Vec3 max{};
};

// --- HeightmapPatch class
// Represents an oriented, positioned heightmap patch in world space.

class HeightmapPatch
{
public:
  HeightmapPatch(std::string                    id,
                 std::shared_ptr<HeightmapData> heightmap_data,
                 Vec3                           center = {0.0f, 0.0f, 0.0f},
                 float                          lx = 100.0f,
                 float                          ly = 100.0f,
                 float                          rotation_deg = 0.0f);

  [[nodiscard]] const std::string &get_id() const noexcept;

  [[nodiscard]] const std::shared_ptr<HeightmapData> &get_heightmap_data()
      const noexcept;

  void set_heightmap_data(
      std::shared_ptr<HeightmapData> heightmap_data) noexcept;

  [[nodiscard]] const Vec3 &get_center() const noexcept;

  void set_center(const Vec3 &center) noexcept;

  [[nodiscard]] float get_lx() const noexcept;

  [[nodiscard]] float get_ly() const noexcept;

  void set_dimensions(float lx, float ly);

  [[nodiscard]] float get_rotation_deg() const noexcept;

  void set_rotation_deg(float rotation_deg) noexcept;

  // Transforms a normalized local coordinate [u, v] in [0, 1] into world space
  [[nodiscard]] Vec3 local_to_world(float u, float v, float z_elevation) const;

  // Transforms a world space coordinate (x, y) into normalized local coordinate
  // [u, v]
  [[nodiscard]] Vec3 world_to_local(const Vec3 &world_pos) const;

  // Computes conservative world-space Axis-Aligned Bounding Box (AABB)
  [[nodiscard]] AABB compute_world_aabb() const;

private:
  std::string                    id_{};
  std::shared_ptr<HeightmapData> heightmap_data_{nullptr};
  Vec3                           center_{0.0f, 0.0f, 0.0f};
  float                          lx_{100.0f};
  float                          ly_{100.0f};
  float                          rotation_deg_{0.0f};
};

} // namespace oros::terrain
