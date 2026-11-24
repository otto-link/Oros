/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "oros/terrain/heightmap_data.hpp"
#include "oros/terrain/heightmap_patch.hpp"

namespace oros::scene
{

// --- TerrainLayerDescriptor
// Describes a terrain patch along with its transform, elevation scaling,
// and optional PBR material maps (albedo, normal).

struct TerrainLayerDescriptor
{
  std::string                             id{};
  std::shared_ptr<terrain::HeightmapData> heightmap{};
  terrain::Vec3                           center{0.0f, 0.0f, 0.0f};
  float                                   lx{1000.0f};
  float                                   ly{1000.0f};
  float                                   rotation_deg{0.0f};
  float                                   height_scale{150.0f};

  // Optional custom normal map (RGB8) and albedo map (RGBA8 or RGB8)
  std::vector<uint8_t> normal_map_rgb8{};
  std::vector<uint8_t> albedo_map_rgb8{};
  size_t               texture_width{0};
  size_t               texture_height{0};
};

// --- Normal map computation utility

[[nodiscard]] std::vector<uint8_t> compute_normal_map_rgb8(
    const terrain::HeightmapData &heightmap,
    float                         lx,
    float                         ly,
    float                         height_scale);

// --- Instanced object types (trees, rocks, scatter objects)

enum class ScatterMeshType : uint8_t
{
  ROCK = 0,
  PINE_TREE,
  DECIDUOUS_TREE,
  CUSTOM
};

struct MeshGeometryData
{
  std::vector<terrain::Vec3> positions{};
  std::vector<terrain::Vec3> normals{};
  std::vector<terrain::Vec3> colors{};
  std::vector<uint32_t>      indices{};
  float                      roughness{0.8f};
  float                      metallic{0.0f};
};

struct ObjectInstance
{
  terrain::Vec3 position{0.0f, 0.0f, 0.0f};
  float         scale{1.0f};
  float         rotation_deg{0.0f};
};

struct InstancedObjectGroup
{
  std::string                 id{};
  ScatterMeshType             mesh_type{ScatterMeshType::ROCK};
  MeshGeometryData            custom_mesh{};
  terrain::Vec3               base_color{0.5f, 0.5f, 0.5f};
  float                       roughness{0.8f};
  float                       metallic{0.0f};
  std::vector<ObjectInstance> instances{};
};

// --- PathDescriptor
// Describes an interactive 3D path made of ordered control points connected
// by line segments.

struct PathDescriptor
{
  std::string                id{};
  std::vector<terrain::Vec3> points{};
  terrain::Vec3              line_color{1.0f, 0.75f, 0.2f};
  terrain::Vec3              point_color{0.2f, 0.7f, 1.0f};
  terrain::Vec3              selected_color{1.0f, 0.35f, 0.2f};
  float                      point_radius{4.0f};
  float                      line_width{1.2f};
};

// --- Procedural mesh generators for trees and rocks

class ScatterMeshFactory
{
public:
  [[nodiscard]] static MeshGeometryData create_deciduous_tree_mesh(
      float radius = 3.5f,
      float height = 10.0f);

  [[nodiscard]] static MeshGeometryData create_pine_tree_mesh(
      float radius = 3.0f,
      float height = 12.0f);

  [[nodiscard]] static MeshGeometryData create_rock_mesh(
      float    radius = 2.0f,
      uint32_t subdivisions = 2,
      uint32_t seed = 1337);
};

// --- Sun & Sky settings

struct SunSettings
{
  float         azimuth_deg{125.0f};
  float         elevation_deg{18.0f};
  float         intensity{95000.0f};
  terrain::Vec3 base_color{1.0f, 0.98f, 0.92f};
  bool          auto_color_temperature{true};
  bool          render_sun_disk{true};
  float         sun_angular_radius_deg{0.65f};
};

// --- Sun physical helper functions

[[nodiscard]] terrain::Vec3 compute_sun_direction(float azimuth_deg,
                                                  float elevation_deg) noexcept;

[[nodiscard]] terrain::Vec3 compute_sun_color_from_elevation(
    float elevation_deg) noexcept;

[[nodiscard]] float compute_sun_intensity_multiplier(
    float elevation_deg) noexcept;

// --- PBR Environment settings

struct EnvironmentSettings
{
  bool  pbr_enabled{true};
  bool  soft_shadows_enabled{true};
  bool  ambient_occlusion_enabled{true};
  bool  fog_enabled{true};
  float fog_density{0.0006f};
  float fog_start{60.0f};
  float fog_height{90.0f};
  float fog_height_falloff{0.015f};
  bool  atmospheric_scattering_enabled{true};
  float atmosphere_turbidity{2.5f};
  float rayleigh_strength{1.0f};
  float mie_strength{1.0f};

  // Optional infinite ground plane
  bool          ground_plane_enabled{false};
  float         ground_plane_elevation{0.0f};
  terrain::Vec3 ground_plane_color{0.20f, 0.24f, 0.16f};
  float         ground_plane_roughness{0.88f};
};

// --- Water layer & settings

struct WaterLayerDescriptor
{
  std::string                             id{};
  std::shared_ptr<terrain::HeightmapData> terrain_heightmap{};
  std::shared_ptr<terrain::HeightmapData> water_depth_map{};
  terrain::Vec3                           center{0.0f, 0.0f, 0.0f};
  float                                   lx{1000.0f};
  float                                   ly{1000.0f};
  float                                   rotation_deg{0.0f};
  float                                   height_scale{150.0f};
};

struct WaterSettings
{
  terrain::Vec3 shallow_color{0.18f, 0.58f, 0.62f};
  terrain::Vec3 deep_color{0.03f, 0.16f, 0.28f};
  float         depth_falloff{6.0f};
  float         opacity{0.82f};
  float         roughness{0.12f};
};

} // namespace oros::scene
