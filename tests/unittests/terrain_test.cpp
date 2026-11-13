/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <cmath>
#include <numeric>
#include <vector>

#include "oros/scene/scene_types.hpp"
#include "oros/terrain/async_lod_selector.hpp"
#include "oros/terrain/cdlod_quadtree.hpp"
#include "oros/terrain/grid_mesh_factory.hpp"
#include "oros/terrain/heightmap_data.hpp"
#include "oros/terrain/heightmap_patch.hpp"

#include <gtest/gtest.h>

using namespace oros::terrain;
using namespace oros::scene;

// --- HeightmapData tests

TEST(TerrainHeightmapDataTest, InitializationAndAccess)
{
  HeightmapData hdata(4, 4, 10.0f);

  EXPECT_EQ(hdata.get_width(), 4);
  EXPECT_EQ(hdata.get_height(), 4);
  EXPECT_FLOAT_EQ(hdata.get_height_at(0, 0), 10.0f);
  EXPECT_FLOAT_EQ(hdata.get_height_at(3, 3), 10.0f);

  const auto [min_z, max_z] = hdata.get_elevation_range();
  EXPECT_FLOAT_EQ(min_z, 10.0f);
  EXPECT_FLOAT_EQ(max_z, 10.0f);
}

TEST(TerrainHeightmapDataTest, DynamicRegionalUpdate)
{
  HeightmapData hdata(8, 8, 0.0f);

  // Update a 2x2 sub-region
  std::vector<float> sub_data = {5.0f, 12.0f, 3.0f, 8.0f};
  hdata.update_region(2, 2, 2, 2, sub_data.data());

  EXPECT_FLOAT_EQ(hdata.get_height_at(2, 2), 5.0f);
  EXPECT_FLOAT_EQ(hdata.get_height_at(3, 2), 12.0f);
  EXPECT_FLOAT_EQ(hdata.get_height_at(2, 3), 3.0f);
  EXPECT_FLOAT_EQ(hdata.get_height_at(3, 3), 8.0f);
  EXPECT_FLOAT_EQ(hdata.get_height_at(0, 0), 0.0f);

  const auto [min_z, max_z] = hdata.get_elevation_range();
  EXPECT_FLOAT_EQ(min_z, 0.0f);
  EXPECT_FLOAT_EQ(max_z, 12.0f);
}

TEST(TerrainHeightmapDataTest, RegionalMinMaxQuery)
{
  HeightmapData hdata(8, 8, 0.0f);

  hdata.set_height_at(1, 1, 50.0f);
  hdata.set_height_at(7, 7, -20.0f);

  // Query top-left corner
  const auto [r1_min, r1_max] = hdata.get_elevation_range_in_rect(0.0f,
                                                                  0.0f,
                                                                  0.25f,
                                                                  0.25f);
  EXPECT_FLOAT_EQ(r1_min, 0.0f);
  EXPECT_FLOAT_EQ(r1_max, 50.0f);

  // Query bottom-right corner
  const auto [r2_min, r2_max] = hdata.get_elevation_range_in_rect(0.75f,
                                                                  0.75f,
                                                                  1.0f,
                                                                  1.0f);
  EXPECT_FLOAT_EQ(r2_min, -20.0f);
  EXPECT_FLOAT_EQ(r2_max, 0.0f);
}

// --- HeightmapPatch tests

TEST(TerrainHeightmapPatchTest, CoordinateTransformation)
{
  auto hdata = std::make_shared<HeightmapData>(4, 4, 0.0f);

  // Patch centered at (100, 200, 10), dimensions 200x100, 90 deg rotation
  HeightmapPatch patch("patch_1",
                       hdata,
                       {100.0f, 200.0f, 10.0f},
                       200.0f,
                       100.0f,
                       90.0f);

  EXPECT_EQ(patch.get_id(), "patch_1");
  EXPECT_FLOAT_EQ(patch.get_lx(), 200.0f);
  EXPECT_FLOAT_EQ(patch.get_ly(), 100.0f);

  // Center (u=0.5, v=0.5) should map to patch center
  const Vec3 center_world = patch.local_to_world(0.5f, 0.5f, 5.0f);
  EXPECT_NEAR(center_world.x, 100.0f, 1e-4f);
  EXPECT_NEAR(center_world.y, 200.0f, 1e-4f);
  EXPECT_NEAR(center_world.z, 15.0f, 1e-4f);

  // Inverse mapping
  const Vec3 local_coords = patch.world_to_local(center_world);
  EXPECT_NEAR(local_coords.x, 0.5f, 1e-4f);
  EXPECT_NEAR(local_coords.y, 0.5f, 1e-4f);
}

TEST(TerrainHeightmapPatchTest, WorldAABBComputation)
{
  auto hdata = std::make_shared<HeightmapData>(4, 4, 0.0f);
  hdata->set_height_at(0, 0, 15.0f);

  HeightmapPatch patch("patch_aabb",
                       hdata,
                       {0.0f, 0.0f, 100.0f},
                       50.0f,
                       50.0f,
                       0.0f);
  const AABB     aabb = patch.compute_world_aabb();

  EXPECT_FLOAT_EQ(aabb.min.x, -25.0f);
  EXPECT_FLOAT_EQ(aabb.max.x, 25.0f);
  EXPECT_FLOAT_EQ(aabb.min.y, -25.0f);
  EXPECT_FLOAT_EQ(aabb.max.y, 25.0f);
  EXPECT_FLOAT_EQ(aabb.min.z, 100.0f);
  EXPECT_FLOAT_EQ(aabb.max.z, 115.0f);
}

// --- GridMeshFactory tests

TEST(TerrainGridMeshFactoryTest, GenerateUnitGrid)
{
  const GridMesh mesh = GridMeshFactory::create_unit_grid(33);

  EXPECT_EQ(mesh.grid_resolution, 33);
  EXPECT_EQ(mesh.vertices.size(), 33 * 33);

  // 32 x 32 cells * 2 triangles/cell * 3 indices/triangle = 6144 indices
  EXPECT_EQ(mesh.indices.size(), 32 * 32 * 6);

  // Verify first and last vertex UVs
  EXPECT_FLOAT_EQ(mesh.vertices.front().u, 0.0f);
  EXPECT_FLOAT_EQ(mesh.vertices.front().v, 0.0f);
  EXPECT_FLOAT_EQ(mesh.vertices.back().u, 1.0f);
  EXPECT_FLOAT_EQ(mesh.vertices.back().v, 1.0f);

  // Verify CCW winding in XY plane so geometric normal points in +Z (upward)
  const auto &v0 = mesh.vertices[mesh.indices[0]];
  const auto &v1 = mesh.vertices[mesh.indices[1]];
  const auto &v2 = mesh.vertices[mesh.indices[2]];
  const float cross_z = (v1.u - v0.u) * (v2.v - v0.v) -
                        (v1.v - v0.v) * (v2.u - v0.u);
  EXPECT_GT(cross_z, 0.0f);
}

// --- CDLODQuadTree & AsyncLODSelector tests

TEST(TerrainCDLODTest, QuadtreeSelectionLevels)
{
  auto           hdata = std::make_shared<HeightmapData>(64, 64, 0.0f);
  HeightmapPatch patch("patch_cdlod",
                       hdata,
                       {0.0f, 0.0f, 0.0f},
                       1000.0f,
                       1000.0f,
                       0.0f);

  CDLODQuadTree quadtree(4); // 4 LOD levels: 0 (finest), 1, 2, 3 (coarsest)

  CDLODSelectionContext ctx;
  ctx.camera_pos = {0.0f, 0.0f, 50.0f}; // Camera close to center
  ctx.lod_distances = {100.0f, 250.0f, 500.0f, 1200.0f};

  const auto instances = quadtree.select_lod(patch, ctx);

  // Should have subdivided because camera is close
  EXPECT_GT(instances.size(), 1);

  // Verify UV partition covers [0, 1] sum area
  float total_area = 0.0f;
  for (const auto &inst : instances)
  {
    total_area += inst.scale_u * inst.scale_v;
  }
  EXPECT_NEAR(total_area, 1.0f, 1e-4f);
}

TEST(TerrainAsyncLODTest, AsyncSelection)
{
  auto           hdata = std::make_shared<HeightmapData>(32, 32, 0.0f);
  HeightmapPatch patch("patch_async",
                       hdata,
                       {0.0f, 0.0f, 0.0f},
                       500.0f,
                       500.0f,
                       0.0f);

  CDLODQuadTree         quadtree(3);
  CDLODSelectionContext ctx;
  ctx.camera_pos = {0.0f, 0.0f, 50.0f};
  ctx.lod_distances = {80.0f, 200.0f, 600.0f};

  AsyncLODSelector selector;
  auto future_instances = selector.request_lod_async(patch, ctx, quadtree);

  ASSERT_TRUE(future_instances.valid());
  const auto instances = future_instances.get();

  EXPECT_FALSE(instances.empty());
}

TEST(TerrainHeightmapPatchTest, RotatedSubregionAABBAndSampleElevation)
{
  auto hdata = std::make_shared<HeightmapData>(4, 4, 0.0f);
  hdata->set_height_at(3, 3, 1.0f);

  // 45 degree rotated patch of size 100x100
  HeightmapPatch patch("patch_rot",
                       hdata,
                       {0.0f, 0.0f, 0.0f},
                       100.0f,
                       100.0f,
                       45.0f);

  const AABB full_aabb = patch.compute_world_aabb();
  // Diagonal half-extent of 100x100 square rotated by 45 deg is 50 * sqrt(2) ~=
  // 70.710678
  EXPECT_NEAR(full_aabb.min.x, -70.710678f, 1e-3f);
  EXPECT_NEAR(full_aabb.max.x, 70.710678f, 1e-3f);
  EXPECT_NEAR(full_aabb.min.y, -70.710678f, 1e-3f);
  EXPECT_NEAR(full_aabb.max.y, 70.710678f, 1e-3f);

  EXPECT_FLOAT_EQ(patch.sample_elevation(0.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(patch.sample_elevation(1.0f, 1.0f), 1.0f);
}

TEST(SceneTypesTest, ScatterMeshesAndSunPhysics)
{
  const auto rock = ScatterMeshFactory::create_rock_mesh(2.0f, 2, 42);
  EXPECT_FALSE(rock.positions.empty());
  EXPECT_EQ(rock.positions.size(), rock.normals.size());
  EXPECT_EQ(rock.indices.size() % 3, 0u);

  const auto pine = ScatterMeshFactory::create_pine_tree_mesh(3.0f, 12.0f);
  EXPECT_FALSE(pine.positions.empty());
  EXPECT_EQ(pine.positions.size(), pine.normals.size());
  EXPECT_EQ(pine.indices.size() % 3, 0u);

  const auto decid = ScatterMeshFactory::create_deciduous_tree_mesh(3.5f,
                                                                    10.0f);
  EXPECT_FALSE(decid.positions.empty());
  EXPECT_EQ(decid.positions.size(), decid.normals.size());
  EXPECT_EQ(decid.indices.size() % 3, 0u);

  // Sun at zenith (elevation = 90 deg) points straight down (0, 0, -1)
  const Vec3 zenith_dir = compute_sun_direction(0.0f, 90.0f);
  EXPECT_NEAR(zenith_dir.x, 0.0f, 1e-4f);
  EXPECT_NEAR(zenith_dir.y, 0.0f, 1e-4f);
  EXPECT_NEAR(zenith_dir.z, -1.0f, 1e-4f);

  const Vec3 sunset_col = compute_sun_color_from_elevation(2.0f);
  const Vec3 noon_col = compute_sun_color_from_elevation(75.0f);
  // Sunset has warmer (lower blue/green relative to red) color than noon
  EXPECT_LT(sunset_col.z, noon_col.z);
}
