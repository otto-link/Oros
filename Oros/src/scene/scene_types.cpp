/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <algorithm>
#include <cmath>
#include <thread>

#include "oros/scene/scene_types.hpp"

#include <numbers>

namespace oros::scene
{

namespace
{

// --- Vector math helpers

terrain::Vec3 add_vec(const terrain::Vec3 &a, const terrain::Vec3 &b) noexcept
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

terrain::Vec3 sub_vec(const terrain::Vec3 &a, const terrain::Vec3 &b) noexcept
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

terrain::Vec3 cross_vec(const terrain::Vec3 &a, const terrain::Vec3 &b) noexcept
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

terrain::Vec3 normalize_vec(const terrain::Vec3 &v) noexcept
{
  const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len < 1e-6f)
  {
    return {0.0f, 0.0f, 1.0f};
  }

  return {v.x / len, v.y / len, v.z / len};
}

void compute_smooth_normals(MeshGeometryData &mesh)
{
  mesh.normals.assign(mesh.positions.size(), {0.0f, 0.0f, 0.0f});

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
  {
    const uint32_t i0 = mesh.indices[i + 0];
    const uint32_t i1 = mesh.indices[i + 1];
    const uint32_t i2 = mesh.indices[i + 2];

    const terrain::Vec3 e1 = sub_vec(mesh.positions[i1], mesh.positions[i0]);
    const terrain::Vec3 e2 = sub_vec(mesh.positions[i2], mesh.positions[i0]);
    const terrain::Vec3 fn = cross_vec(e1, e2);

    mesh.normals[i0] = add_vec(mesh.normals[i0], fn);
    mesh.normals[i1] = add_vec(mesh.normals[i1], fn);
    mesh.normals[i2] = add_vec(mesh.normals[i2], fn);
  }

  for (auto &n : mesh.normals)
  {
    n = normalize_vec(n);
  }
}

void append_cone_section(MeshGeometryData    &mesh,
                         float                z_bottom,
                         float                z_top,
                         float                r_bottom,
                         float                r_top,
                         uint32_t             segments,
                         const terrain::Vec3 &color)
{
  const uint32_t base_idx = static_cast<uint32_t>(mesh.positions.size());
  const float    two_pi = 2.0f * std::numbers::pi_v<float>;

  for (uint32_t s = 0; s <= segments; ++s)
  {
    const float angle = static_cast<float>(s) / static_cast<float>(segments) *
                        two_pi;
    const float c = std::cos(angle);
    const float si = std::sin(angle);

    // Bottom ring vertex
    mesh.positions.push_back({r_bottom * c, r_bottom * si, z_bottom});
    mesh.colors.push_back(color);

    // Top ring vertex
    mesh.positions.push_back({r_top * c, r_top * si, z_top});
    mesh.colors.push_back(color);
  }

  for (uint32_t s = 0; s < segments; ++s)
  {
    const uint32_t b0 = base_idx + s * 2;
    const uint32_t t0 = b0 + 1;
    const uint32_t b1 = base_idx + (s + 1) * 2;
    const uint32_t t1 = b1 + 1;

    mesh.indices.push_back(b0);
    mesh.indices.push_back(b1);
    mesh.indices.push_back(t0);

    mesh.indices.push_back(t0);
    mesh.indices.push_back(b1);
    mesh.indices.push_back(t1);
  }

  // Bottom cap if r_bottom > 0
  if (r_bottom > 0.001f)
  {
    const uint32_t center_idx = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.push_back({0.0f, 0.0f, z_bottom});
    mesh.colors.push_back(color);

    for (uint32_t s = 0; s < segments; ++s)
    {
      const uint32_t b0 = base_idx + s * 2;
      const uint32_t b1 = base_idx + (s + 1) * 2;
      mesh.indices.push_back(center_idx);
      mesh.indices.push_back(b1);
      mesh.indices.push_back(b0);
    }
  }
}

} // namespace

// --- Normal map computation utility

std::vector<uint8_t> compute_normal_map_rgb8(
    const terrain::HeightmapData &heightmap,
    float                         lx,
    float                         ly,
    float                         height_scale)
{
  const size_t w = heightmap.get_width();
  const size_t h = heightmap.get_height();

  if (w == 0 || h == 0)
  {
    return {};
  }

  std::vector<uint8_t> normal_map(w * h * 3);
  const auto          &elevations = heightmap.get_data();

  const float step_x = 2.0f * (lx / static_cast<float>(std::max<size_t>(1, w)));
  const float step_y = 2.0f * (ly / static_cast<float>(std::max<size_t>(1, h)));

  const unsigned int hw_threads = std::thread::hardware_concurrency();
  const size_t num_threads = std::max(1u, hw_threads > 0 ? hw_threads : 8u);
  const size_t rows_per_thread = (h + num_threads - 1) / num_threads;

  {
    std::vector<std::jthread> workers;
    workers.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t)
    {
      const size_t y_start = t * rows_per_thread;
      const size_t y_end = std::min(h, y_start + rows_per_thread);

      if (y_start >= h)
      {
        break;
      }

      workers.emplace_back(
          [&elevations,
           &normal_map,
           w,
           h,
           step_x,
           step_y,
           height_scale,
           y_start,
           y_end]()
          {
            for (size_t y = y_start; y < y_end; ++y)
            {
              const size_t y_prev = (y > 0) ? (y - 1) : 0;
              const size_t y_next = (y + 1 < h) ? (y + 1) : y;

              for (size_t x = 0; x < w; ++x)
              {
                const size_t x_prev = (x > 0) ? (x - 1) : 0;
                const size_t x_next = (x + 1 < w) ? (x + 1) : x;

                const float hL = elevations[y * w + x_prev] * height_scale;
                const float hR = elevations[y * w + x_next] * height_scale;
                const float hD = elevations[y_prev * w + x] * height_scale;
                const float hU = elevations[y_next * w + x] * height_scale;

                const float nx = -(hR - hL) / step_x;
                const float ny = -(hU - hD) / step_y;
                const float nz = 1.0f;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);

                const size_t idx = (y * w + x) * 3;
                normal_map[idx + 0] = static_cast<uint8_t>(
                    std::clamp((nx / len * 0.5f + 0.5f) * 255.0f,
                               0.0f,
                               255.0f));
                normal_map[idx + 1] = static_cast<uint8_t>(
                    std::clamp((ny / len * 0.5f + 0.5f) * 255.0f,
                               0.0f,
                               255.0f));
                normal_map[idx + 2] = static_cast<uint8_t>(
                    std::clamp((nz / len * 0.5f + 0.5f) * 255.0f,
                               0.0f,
                               255.0f));
              }
            }
          });
    }
  }

  return normal_map;
}

// --- ScatterMeshFactory

MeshGeometryData ScatterMeshFactory::create_deciduous_tree_mesh(float radius,
                                                                float height)
{
  MeshGeometryData mesh;
  mesh.roughness = 0.85f;
  mesh.metallic = 0.0f;

  const terrain::Vec3 trunk_color{0.35f, 0.22f, 0.12f};
  const terrain::Vec3 canopy_color{0.18f, 0.48f, 0.16f};

  // Trunk
  const float trunk_h = height * 0.4f;
  const float trunk_r = radius * 0.16f;
  append_cone_section(mesh,
                      0.0f,
                      trunk_h,
                      trunk_r,
                      trunk_r * 0.7f,
                      8,
                      trunk_color);

  // Spherical / ovoid canopy built from stacked bands
  const uint32_t rings = 6;
  const uint32_t segments = 10;
  const float    canopy_center_z = height * 0.65f;
  const float    canopy_rz = height * 0.38f;

  for (uint32_t r = 0; r < rings; ++r)
  {
    const float t0 = static_cast<float>(r) / static_cast<float>(rings);
    const float t1 = static_cast<float>(r + 1) / static_cast<float>(rings);

    const float phi0 = (t0 - 0.5f) * std::numbers::pi_v<float>;
    const float phi1 = (t1 - 0.5f) * std::numbers::pi_v<float>;

    const float z0 = canopy_center_z + canopy_rz * std::sin(phi0);
    const float z1 = canopy_center_z + canopy_rz * std::sin(phi1);
    const float r0 = radius * std::cos(phi0);
    const float r1 = radius * std::cos(phi1);

    append_cone_section(mesh, z0, z1, r0, r1, segments, canopy_color);
  }

  compute_smooth_normals(mesh);
  return mesh;
}

MeshGeometryData ScatterMeshFactory::create_pine_tree_mesh(float radius,
                                                           float height)
{
  MeshGeometryData mesh;
  mesh.roughness = 0.85f;
  mesh.metallic = 0.0f;

  const terrain::Vec3 trunk_color{0.32f, 0.20f, 0.10f};
  const terrain::Vec3 needle_dark{0.08f, 0.28f, 0.12f};
  const terrain::Vec3 needle_mid{0.11f, 0.35f, 0.15f};
  const terrain::Vec3 needle_top{0.14f, 0.42f, 0.18f};

  // Trunk cylinder
  const float trunk_h = height * 0.28f;
  const float trunk_r = radius * 0.15f;
  append_cone_section(mesh,
                      0.0f,
                      trunk_h,
                      trunk_r,
                      trunk_r * 0.8f,
                      8,
                      trunk_color);

  // 3 stacked conifer tiers
  append_cone_section(mesh,
                      height * 0.20f,
                      height * 0.55f,
                      radius,
                      radius * 0.2f,
                      10,
                      needle_dark);

  append_cone_section(mesh,
                      height * 0.45f,
                      height * 0.80f,
                      radius * 0.8f,
                      radius * 0.15f,
                      10,
                      needle_mid);

  append_cone_section(mesh,
                      height * 0.68f,
                      height,
                      radius * 0.55f,
                      0.0f,
                      10,
                      needle_top);

  compute_smooth_normals(mesh);
  return mesh;
}

MeshGeometryData ScatterMeshFactory::create_rock_mesh(float    radius,
                                                      uint32_t subdivisions,
                                                      uint32_t seed)
{
  MeshGeometryData mesh;
  mesh.roughness = 0.9f;
  mesh.metallic = 0.05f;

  const uint32_t lat_steps = std::max(4u, 4u * (subdivisions + 1));
  const uint32_t lon_steps = std::max(6u, 6u * (subdivisions + 1));

  const float         pi = std::numbers::pi_v<float>;
  const terrain::Vec3 rock_color{0.48f, 0.46f, 0.44f};

  // Deterministic pseudo-random displacement for faceted rock appearance
  auto hash_disp = [seed](float x, float y, float z) -> float
  {
    const float d = std::sin(x * 12.9898f + y * 78.233f + z * 37.719f +
                             static_cast<float>(seed)) *
                    43758.5453f;
    return d - std::floor(d);
  };

  for (uint32_t lat = 0; lat <= lat_steps; ++lat)
  {
    const float theta = static_cast<float>(lat) /
                        static_cast<float>(lat_steps) * pi;
    const float sin_t = std::sin(theta);
    const float cos_t = std::cos(theta);

    for (uint32_t lon = 0; lon <= lon_steps; ++lon)
    {
      // Wrap seam so u=0 and u=lon_steps match
      const uint32_t eff_lon = (lon == lon_steps) ? 0 : lon;
      const float    phi = static_cast<float>(eff_lon) /
                        static_cast<float>(lon_steps) * (2.0f * pi);

      const float ux = sin_t * std::cos(phi);
      const float uy = sin_t * std::sin(phi);
      const float uz = cos_t;

      const float noise = 0.78f + 0.35f * hash_disp(ux, uy, uz);
      const float rx = radius * noise * 1.15f;
      const float ry = radius * noise * 0.95f;
      const float rz = radius * noise * 0.75f;

      // Embed base slightly around z = 0 so rocks sit naturally on terrain
      mesh.positions.push_back({ux * rx, uy * ry, (uz * 0.5f + 0.35f) * rz});

      const float shade = 0.85f + 0.25f * uz;
      mesh.colors.push_back(
          {rock_color.x * shade, rock_color.y * shade, rock_color.z * shade});
    }
  }

  const uint32_t stride = lon_steps + 1;
  for (uint32_t lat = 0; lat < lat_steps; ++lat)
  {
    for (uint32_t lon = 0; lon < lon_steps; ++lon)
    {
      const uint32_t i0 = lat * stride + lon;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = (lat + 1) * stride + lon;
      const uint32_t i3 = i2 + 1;

      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);

      mesh.indices.push_back(i1);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i2);
    }
  }

  compute_smooth_normals(mesh);
  return mesh;
}

// --- Sun physical helper functions

terrain::Vec3 compute_sun_direction(float azimuth_deg,
                                    float elevation_deg) noexcept
{
  const float deg_to_rad = std::numbers::pi_v<float> / 180.0f;
  const float az = azimuth_deg * deg_to_rad;
  const float el = elevation_deg * deg_to_rad;

  // Direction FROM the sun TO the scene (light ray direction)
  const float cos_el = std::cos(el);
  const float sun_x = cos_el * std::cos(az);
  const float sun_y = cos_el * std::sin(az);
  const float sun_z = std::sin(el);

  return normalize_vec({-sun_x, -sun_y, -sun_z});
}

terrain::Vec3 compute_sun_color_from_elevation(float elevation_deg) noexcept
{
  // Smooth atmospheric reddening from horizon (0 deg) to zenith (90 deg)
  const float t = std::clamp(elevation_deg / 65.0f, 0.0f, 1.0f);
  const float smooth_t = t * t * (3.0f - 2.0f * t);

  // Horizon warm sunset orange/gold -> Zenith neutral warm daylight
  const float r = 1.0f;
  const float g = 0.48f + 0.50f * smooth_t;
  const float b = 0.20f + 0.74f * std::pow(smooth_t, 1.3f);

  return {r, g, b};
}

float compute_sun_intensity_multiplier(float elevation_deg) noexcept
{
  if (elevation_deg <= -3.0f)
  {
    return 0.02f;
  }

  // Atmospheric optical mass attenuation approximation
  const float clamped_el = std::clamp(elevation_deg + 3.0f, 0.0f, 90.0f);
  const float t = clamped_el / 90.0f;

  return std::clamp(0.12f + 0.88f * std::pow(t, 0.45f), 0.02f, 1.0f);
}

} // namespace oros::scene
