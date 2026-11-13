/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <memory>

#include <QApplication>
#include <gtest/gtest.h>
#include <oros/oros.hpp>
#include <oros/ui/viewport_widget.hpp>

// --- ViewportWidget unit tests

TEST(OrosBasicTest, VersionCheck)
{
  EXPECT_FALSE(oros::get_version().empty());
  EXPECT_EQ(oros::get_version(), "0.1.0");
}

TEST(ViewportWidgetTest, WidgetCreationAndColor)
{
  // ensure offscreen platform for headless test execution
  qputenv("QT_QPA_PLATFORM", "offscreen");

  int           argc = 0;
  char         *argv[] = {nullptr};
  QApplication *app = qobject_cast<QApplication *>(
      QCoreApplication::instance());
  std::unique_ptr<QApplication> app_holder;

  if (!app)
  {
    app_holder = std::make_unique<QApplication>(argc, argv);
    app = app_holder.get();
  }

  oros::ui::ViewportWidget widget;
  EXPECT_EQ(widget.get_background_color(), QColor(30, 30, 35));

  QColor new_color(50, 70, 90);
  widget.set_background_color(new_color);
  EXPECT_EQ(widget.get_background_color(), new_color);
}

TEST(ViewportWidgetTest, WidgetResize)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");

  int           argc = 0;
  char         *argv[] = {nullptr};
  QApplication *app = qobject_cast<QApplication *>(
      QCoreApplication::instance());
  std::unique_ptr<QApplication> app_holder;

  if (!app)
  {
    app_holder = std::make_unique<QApplication>(argc, argv);
    app = app_holder.get();
  }

  oros::ui::ViewportWidget widget;
  widget.resize(640, 480);
  EXPECT_EQ(widget.width(), 640);
  EXPECT_EQ(widget.height(), 480);
}

TEST(ViewportWidgetTest, MultiTerrainAndInstancedSceneAPI)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");

  int           argc = 0;
  char         *argv[] = {nullptr};
  QApplication *app = qobject_cast<QApplication *>(
      QCoreApplication::instance());
  std::unique_ptr<QApplication> app_holder;

  if (!app)
  {
    app_holder = std::make_unique<QApplication>(argc, argv);
    app = app_holder.get();
  }

  oros::ui::ViewportWidget widget;

  auto hdata1 = std::make_shared<oros::terrain::HeightmapData>(16, 16, 0.5f);
  auto hdata2 = std::make_shared<oros::terrain::HeightmapData>(16, 16, 0.25f);

  oros::scene::TerrainLayerDescriptor t1;
  t1.id = "terrain_1";
  t1.heightmap = hdata1;
  t1.center = {-500.0f, 0.0f, 0.0f};
  t1.lx = 800.0f;
  t1.ly = 800.0f;
  t1.rotation_deg = 0.0f;
  t1.height_scale = 120.0f;

  oros::scene::TerrainLayerDescriptor t2;
  t2.id = "terrain_2";
  t2.heightmap = hdata2;
  t2.center = {500.0f, 0.0f, 20.0f};
  t2.lx = 600.0f;
  t2.ly = 600.0f;
  t2.rotation_deg = 35.0f;
  t2.height_scale = 80.0f;

  widget.add_or_update_terrain(t1);
  widget.add_or_update_terrain(t2);

  EXPECT_EQ(widget.get_terrain_count(), 2u);
  EXPECT_GT(widget.get_active_cdlod_node_count(), 0u);

  const auto pos = widget.sample_terrain_world_pos("terrain_1", 0.5f, 0.5f);
  EXPECT_NEAR(pos.x, -500.0f, 1e-3f);
  EXPECT_NEAR(pos.y, 0.0f, 1e-3f);
  EXPECT_NEAR(pos.z, 60.0f, 1e-3f);

  // Add instanced trees and rocks
  oros::scene::InstancedObjectGroup trees;
  trees.id = "pine_trees";
  trees.mesh_type = oros::scene::ScatterMeshType::PINE_TREE;
  trees.instances = {{{-500.0f, 0.0f, 60.0f}, 1.2f, 15.0f},
                     {{-450.0f, 50.0f, 60.0f}, 0.85f, 110.0f}};

  widget.add_or_update_instanced_group(trees);
  EXPECT_EQ(widget.get_instanced_group_count(), 1u);
  EXPECT_EQ(widget.get_total_object_instance_count(), 2u);

  // Update sun and PBR environment
  widget.set_sun_angles(210.0f, 18.0f);
  EXPECT_FLOAT_EQ(widget.get_sun_settings().azimuth_deg, 210.0f);
  EXPECT_FLOAT_EQ(widget.get_sun_settings().elevation_deg, 18.0f);

  widget.set_pbr_environment_enabled(false);
  EXPECT_FALSE(widget.get_environment_settings().pbr_enabled);
}
