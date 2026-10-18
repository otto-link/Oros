#include <cmath>
#include <stdexcept>

#include "oros/terrain/heightmap_patch.hpp"

#include <numbers>

namespace oros::terrain
{

HeightmapPatch::HeightmapPatch(std::string                    id,
                               std::shared_ptr<HeightmapData> heightmap_data,
                               Vec3                           center,
                               float                          lx,
                               float                          ly,
                               float                          rotation_deg)
    : id_(std::move(id)),
      heightmap_data_(std::move(heightmap_data)),
      center_(center),
      lx_(lx),
      ly_(ly),
      rotation_deg_(rotation_deg)
{
  if (lx_ <= 0.0f || ly_ <= 0.0f)
  {
    throw std::invalid_argument("Patch dimensions lx and ly must be positive");
  }
}

const std::string &HeightmapPatch::get_id() const noexcept
{
  return id_;
}

const std::shared_ptr<HeightmapData> &HeightmapPatch::get_heightmap_data()
    const noexcept
{
  return heightmap_data_;
}

void HeightmapPatch::set_heightmap_data(
    std::shared_ptr<HeightmapData> heightmap_data) noexcept
{
  heightmap_data_ = std::move(heightmap_data);
}

const Vec3 &HeightmapPatch::get_center() const noexcept
{
  return center_;
}

void HeightmapPatch::set_center(const Vec3 &center) noexcept
{
  center_ = center;
}

float HeightmapPatch::get_lx() const noexcept
{
  return lx_;
}

float HeightmapPatch::get_ly() const noexcept
{
  return ly_;
}

void HeightmapPatch::set_dimensions(float lx, float ly)
{
  if (lx <= 0.0f || ly <= 0.0f)
  {
    throw std::invalid_argument("Patch dimensions lx and ly must be positive");
  }

  lx_ = lx;
  ly_ = ly;
}

float HeightmapPatch::get_rotation_deg() const noexcept
{
  return rotation_deg_;
}

void HeightmapPatch::set_rotation_deg(float rotation_deg) noexcept
{
  rotation_deg_ = rotation_deg;
}

Vec3 HeightmapPatch::local_to_world(float u, float v, float z_elevation) const
{
  // Local patch coordinates relative to patch center in [-lx/2, lx/2] x [-ly/2,
  // ly/2]
  const float local_x = (u - 0.5f) * lx_;
  const float local_y = (v - 0.5f) * ly_;

  const float rad = rotation_deg_ * (std::numbers::pi_v<float> / 180.0f);
  const float cos_theta = std::cos(rad);
  const float sin_theta = std::sin(rad);

  // Rotate around Z axis and translate by center
  const float world_x = cos_theta * local_x - sin_theta * local_y + center_.x;
  const float world_y = sin_theta * local_x + cos_theta * local_y + center_.y;
  const float world_z = center_.z + z_elevation;

  return {world_x, world_y, world_z};
}

Vec3 HeightmapPatch::world_to_local(const Vec3 &world_pos) const
{
  // Translate back relative to center
  const float dx = world_pos.x - center_.x;
  const float dy = world_pos.y - center_.y;

  const float rad = -rotation_deg_ * (std::numbers::pi_v<float> / 180.0f);
  const float cos_theta = std::cos(rad);
  const float sin_theta = std::sin(rad);

  // Inverse rotation
  const float local_x = cos_theta * dx - sin_theta * dy;
  const float local_y = sin_theta * dx + cos_theta * dy;

  // Normalize to [0, 1] range
  const float u = (local_x / lx_) + 0.5f;
  const float v = (local_y / ly_) + 0.5f;
  const float z = world_pos.z - center_.z;

  return {u, v, z};
}

AABB HeightmapPatch::compute_world_aabb() const
{
  float min_z = 0.0f;
  float max_z = 0.0f;

  if (heightmap_data_)
  {
    const auto range = heightmap_data_->get_elevation_range();
    min_z = range.first;
    max_z = range.second;
  }

  // 4 corners of the patch in local coordinates
  const Vec3 c0 = local_to_world(0.0f, 0.0f, min_z);
  const Vec3 c1 = local_to_world(1.0f, 0.0f, min_z);
  const Vec3 c2 = local_to_world(0.0f, 1.0f, min_z);
  const Vec3 c3 = local_to_world(1.0f, 1.0f, min_z);

  const float aabb_min_x = std::min({c0.x, c1.x, c2.x, c3.x});
  const float aabb_max_x = std::max({c0.x, c1.x, c2.x, c3.x});

  const float aabb_min_y = std::min({c0.y, c1.y, c2.y, c3.y});
  const float aabb_max_y = std::max({c0.y, c1.y, c2.y, c3.y});

  return {{aabb_min_x, aabb_min_y, center_.z + min_z},
          {aabb_max_x, aabb_max_y, center_.z + max_z}};
}

} // namespace oros::terrain
