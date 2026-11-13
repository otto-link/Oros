/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

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

  return compute_subregion_world_aabb(0.0f, 0.0f, 1.0f, 1.0f, min_z, max_z);
}

AABB HeightmapPatch::compute_subregion_world_aabb(float u0,
                                                  float v0,
                                                  float u1,
                                                  float v1,
                                                  float min_z,
                                                  float max_z) const
{
  // 4 corners of the sub-region in local coordinates
  const Vec3 c0 = local_to_world(u0, v0, min_z);
  const Vec3 c1 = local_to_world(u1, v0, min_z);
  const Vec3 c2 = local_to_world(u0, v1, min_z);
  const Vec3 c3 = local_to_world(u1, v1, min_z);

  const float aabb_min_x = std::min({c0.x, c1.x, c2.x, c3.x});
  const float aabb_max_x = std::max({c0.x, c1.x, c2.x, c3.x});

  const float aabb_min_y = std::min({c0.y, c1.y, c2.y, c3.y});
  const float aabb_max_y = std::max({c0.y, c1.y, c2.y, c3.y});

  return {{aabb_min_x, aabb_min_y, center_.z + min_z},
          {aabb_max_x, aabb_max_y, center_.z + max_z}};
}

float HeightmapPatch::sample_elevation(float u, float v) const
{
  if (!heightmap_data_ || heightmap_data_->get_width() == 0 ||
      heightmap_data_->get_height() == 0)
  {
    return 0.0f;
  }

  const float clamped_u = std::clamp(u, 0.0f, 1.0f);
  const float clamped_v = std::clamp(v, 0.0f, 1.0f);

  const size_t w = heightmap_data_->get_width();
  const size_t h = heightmap_data_->get_height();

  const float fx = clamped_u * static_cast<float>(w > 1 ? w - 1 : 0);
  const float fy = clamped_v * static_cast<float>(h > 1 ? h - 1 : 0);

  const size_t x0 = static_cast<size_t>(std::floor(fx));
  const size_t y0 = static_cast<size_t>(std::floor(fy));
  const size_t x1 = std::min(x0 + 1, w - 1);
  const size_t y1 = std::min(y0 + 1, h - 1);

  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);

  const float z00 = heightmap_data_->get_height_at(x0, y0);
  const float z10 = heightmap_data_->get_height_at(x1, y0);
  const float z01 = heightmap_data_->get_height_at(x0, y1);
  const float z11 = heightmap_data_->get_height_at(x1, y1);

  const float z0 = z00 + tx * (z10 - z00);
  const float z1 = z01 + tx * (z11 - z01);

  return z0 + ty * (z1 - z0);
}

} // namespace oros::terrain
