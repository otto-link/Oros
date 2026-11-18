/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <cmath>
#include <random>
#include <vector>

#include "FastNoiseLite.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <oros/logger.hpp>
#include <oros/oros.hpp>
#include <oros/scene/scene_types.hpp>
#include <oros/ui/viewport_widget.hpp>

namespace
{

std::shared_ptr<oros::terrain::HeightmapData> generate_fractal_heightmap(
    size_t resolution,
    float  frequency,
    int    seed)
{
  std::vector<float> heights(resolution * resolution);
  const float        inv_res = (resolution > 1)
                                   ? (1.0f / static_cast<float>(resolution - 1))
                                   : 1.0f;

  FastNoiseLite noise(seed);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFractalType(FastNoiseLite::FractalType_FBm);
  noise.SetFractalOctaves(8);
  noise.SetFrequency(frequency);
  noise.SetFractalWeightedStrength(0.35f);

  for (size_t y = 0; y < resolution; ++y)
  {
    const float ny = static_cast<float>(y) * inv_res;
    for (size_t x = 0; x < resolution; ++x)
    {
      const float nx = static_cast<float>(x) * inv_res;
      float       val = (noise.GetNoise(nx, ny) + 1.0f) * 0.5f;

      // Falloff towards edges for island/mountain peak feel
      const float dx = (nx - 0.5f) * 2.0f;
      const float dy = (ny - 0.5f) * 2.0f;
      const float r2 = std::min(1.0f, dx * dx + dy * dy);
      val *= (1.0f - 0.35f * r2);

      heights[y * resolution + x] = std::clamp(val, 0.0f, 1.0f);
    }
  }

  return std::make_shared<oros::terrain::HeightmapData>(resolution,
                                                        resolution,
                                                        std::move(heights));
}

void populate_scatter_objects(oros::ui::ViewportWidget *viewport,
                              size_t                    tree_count,
                              size_t                    rock_count)
{
  std::mt19937                          rng(42);
  std::uniform_real_distribution<float> dist_uv(0.06f, 0.94f);
  std::uniform_real_distribution<float> dist_rot(0.0f, 360.0f);
  std::uniform_real_distribution<float> dist_tree_scale(0.65f, 1.65f);
  std::uniform_real_distribution<float> dist_rock_scale(0.50f, 2.40f);

  const std::vector<std::string> terrain_ids = {"terrain_main",
                                                "terrain_east_rotated",
                                                "terrain_north_plateau"};

  oros::scene::InstancedObjectGroup pines;
  pines.id = "scatter_pines";
  pines.mesh_type = oros::scene::ScatterMeshType::PINE_TREE;
  pines.base_color = {1.0f, 1.0f, 1.0f};
  pines.roughness = 0.85f;

  oros::scene::InstancedObjectGroup deciduous;
  deciduous.id = "scatter_deciduous";
  deciduous.mesh_type = oros::scene::ScatterMeshType::DECIDUOUS_TREE;
  deciduous.base_color = {1.0f, 1.0f, 1.0f};
  deciduous.roughness = 0.85f;

  oros::scene::InstancedObjectGroup rocks;
  rocks.id = "scatter_rocks";
  rocks.mesh_type = oros::scene::ScatterMeshType::ROCK;
  rocks.base_color = {0.82f, 0.80f, 0.78f};
  rocks.roughness = 0.90f;

  for (size_t i = 0; i < tree_count; ++i)
  {
    const std::string &tid = terrain_ids[i % terrain_ids.size()];
    const float        u = dist_uv(rng);
    const float        v = dist_uv(rng);
    const auto         pos = viewport->sample_terrain_world_pos(tid, u, v);

    // Place trees mostly at mid-to-low elevations
    if (pos.z < 125.0f)
    {
      oros::scene::ObjectInstance inst;
      inst.position = pos;
      inst.scale = dist_tree_scale(rng);
      inst.rotation_deg = dist_rot(rng);

      if (i % 3 == 0)
      {
        deciduous.instances.push_back(inst);
      }
      else
      {
        pines.instances.push_back(inst);
      }
    }
  }

  for (size_t i = 0; i < rock_count; ++i)
  {
    const std::string &tid = terrain_ids[i % terrain_ids.size()];
    const float        u = dist_uv(rng);
    const float        v = dist_uv(rng);
    const auto         pos = viewport->sample_terrain_world_pos(tid, u, v);

    oros::scene::ObjectInstance inst;
    inst.position = pos;
    inst.scale = dist_rock_scale(rng);
    inst.rotation_deg = dist_rot(rng);
    rocks.instances.push_back(inst);
  }

  viewport->add_or_update_instanced_group(pines);
  viewport->add_or_update_instanced_group(deciduous);
  viewport->add_or_update_instanced_group(rocks);

  // Add an interactive 3D path across the main terrain
  oros::scene::PathDescriptor demo_path;
  demo_path.id = "demo_path";
  demo_path.point_radius = 5.0f;
  demo_path.line_width = 2.0f;

  const std::vector<std::pair<float, float>> path_uvs = {
      {0.20f, 0.25f},
      {0.38f, 0.42f},
      {0.58f, 0.50f},
      {0.78f, 0.72f},
  };

  for (const auto &[u, v] : path_uvs)
  {
    oros::terrain::Vec3 pt = viewport->sample_terrain_world_pos("main_terrain",
                                                                u,
                                                                v);
    // pt.z += 25.0f;
    demo_path.points.push_back(pt);
  }

  viewport->add_or_update_path(demo_path);
}

} // namespace

// --- Entry point

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  oros::Logger::log()->info("Starting Oros Hesiod Renderer Showcase (v{})...",
                            oros::get_version());

  QMainWindow main_window;
  main_window.setWindowTitle(
      "Oros - Hesiod Real-Time 3D Terrain & PBR Environment Renderer");
  main_window.resize(1360, 860);

  auto *central = new QWidget(&main_window);
  auto *root_layout = new QHBoxLayout(central);

  auto *viewport = new oros::ui::ViewportWidget(central);
  root_layout->addWidget(viewport, 1);

  // Build 3 distinct terrains with different positions, sizes, and rotations
  auto hmap_main = generate_fractal_heightmap(512, 2.2f, 1337);
  auto hmap_east = generate_fractal_heightmap(512, 3.0f, 4242);
  auto hmap_north = generate_fractal_heightmap(256, 1.8f, 9001);

  oros::scene::TerrainLayerDescriptor t_main;
  t_main.id = "terrain_main";
  t_main.heightmap = hmap_main;
  t_main.center = {-450.0f, -200.0f, 0.0f};
  t_main.lx = 900.0f;
  t_main.ly = 900.0f;
  t_main.rotation_deg = 0.0f;
  t_main.height_scale = 180.0f;

  oros::scene::TerrainLayerDescriptor t_east;
  t_east.id = "terrain_east_rotated";
  t_east.heightmap = hmap_east;
  t_east.center = {520.0f, -150.0f, 0.0f};
  t_east.lx = 850.0f;
  t_east.ly = 750.0f;
  t_east.rotation_deg = 32.0f;
  t_east.height_scale = 160.0f;

  oros::scene::TerrainLayerDescriptor t_north;
  t_north.id = "terrain_north_plateau";
  t_north.heightmap = hmap_north;
  t_north.center = {20.0f, 680.0f, 15.0f};
  t_north.lx = 1100.0f;
  t_north.ly = 650.0f;
  t_north.rotation_deg = -18.0f;
  t_north.height_scale = 140.0f;

  viewport->add_or_update_terrain(t_main);
  viewport->add_or_update_terrain(t_east);
  viewport->add_or_update_terrain(t_north);

  populate_scatter_objects(viewport, 1800, 900);

  viewport->set_camera_orbit(QVector3D(0.0f, 100.0f, 40.0f),
                             1450.0f,
                             -55.0f,
                             28.0f);

  // Side control panel
  auto *scroll = new QScrollArea(central);
  scroll->setFixedWidth(330);
  scroll->setWidgetResizable(true);

  auto *panel = new QWidget(scroll);
  auto *panel_layout = new QVBoxLayout(panel);

  auto *stats_lbl = new QLabel(panel);
  auto  update_stats = [viewport, stats_lbl]()
  {
    stats_lbl->setText(
        QString("<b>Terrains:</b> %1 | <b>CDLOD Nodes:</b> %2<br>"
                "<b>Scatter Instances:</b> %3 | <b>FPS:</b> %4")
            .arg(viewport->get_terrain_count())
            .arg(viewport->get_active_cdlod_node_count())
            .arg(viewport->get_total_object_instance_count())
            .arg(viewport->get_fps(), 0, 'f', 1));
  };
  update_stats();
  panel_layout->addWidget(stats_lbl);

  QObject::connect(viewport,
                   &oros::ui::ViewportWidget::lod_selection_updated,
                   [update_stats](size_t) { update_stats(); });
  QObject::connect(viewport,
                   &oros::ui::ViewportWidget::fps_updated,
                   [update_stats](float, float) { update_stats(); });

  // 1. Sun & Sky Group
  auto *sun_group = new QGroupBox("Sun & Sky Trajectory", panel);
  auto *sun_layout = new QVBoxLayout(sun_group);

  auto *az_lbl = new QLabel("Sun Azimuth: 125 deg", sun_group);
  auto *az_slider = new QSlider(Qt::Horizontal, sun_group);
  az_slider->setRange(0, 360);
  az_slider->setValue(125);
  sun_layout->addWidget(az_lbl);
  sun_layout->addWidget(az_slider);

  auto *el_lbl = new QLabel("Sun Elevation: 18 deg", sun_group);
  auto *el_slider = new QSlider(Qt::Horizontal, sun_group);
  el_slider->setRange(-5, 90);
  el_slider->setValue(18);
  sun_layout->addWidget(el_lbl);
  sun_layout->addWidget(el_slider);

  auto update_sun = [viewport, az_slider, el_slider, az_lbl, el_lbl]()
  {
    const float az = static_cast<float>(az_slider->value());
    const float el = static_cast<float>(el_slider->value());
    az_lbl->setText(QString("Sun Azimuth: %1 deg").arg(az));
    el_lbl->setText(QString("Sun Elevation: %1 deg").arg(el));
    viewport->set_sun_angles(az, el);
  };

  QObject::connect(az_slider, &QSlider::valueChanged, update_sun);
  QObject::connect(el_slider, &QSlider::valueChanged, update_sun);
  panel_layout->addWidget(sun_group);

  // 2. PBR Environment Group
  auto *env_group = new QGroupBox("PBR Environment", panel);
  auto *env_layout = new QVBoxLayout(env_group);

  auto *pbr_check = new QCheckBox("Enable Full PBR Environment", env_group);
  pbr_check->setChecked(true);
  env_layout->addWidget(pbr_check);
  QObject::connect(pbr_check,
                   &QCheckBox::toggled,
                   viewport,
                   &oros::ui::ViewportWidget::set_pbr_environment_enabled);

  auto *shadows_check = new QCheckBox("Soft Cascaded Shadows", env_group);
  shadows_check->setChecked(true);
  env_layout->addWidget(shadows_check);
  QObject::connect(shadows_check,
                   &QCheckBox::toggled,
                   viewport,
                   &oros::ui::ViewportWidget::set_soft_shadows_enabled);

  auto *atmo_check = new QCheckBox("Atmospheric Scattering Sky", env_group);
  atmo_check->setChecked(true);
  env_layout->addWidget(atmo_check);
  QObject::connect(
      atmo_check,
      &QCheckBox::toggled,
      viewport,
      &oros::ui::ViewportWidget::set_atmospheric_scattering_enabled);

  auto *fog_check = new QCheckBox("Volumetric / Height Fog", env_group);
  fog_check->setChecked(true);
  env_layout->addWidget(fog_check);
  QObject::connect(fog_check,
                   &QCheckBox::toggled,
                   viewport,
                   &oros::ui::ViewportWidget::set_fog_enabled);

  auto *ground_check = new QCheckBox("Infinite Ground Plane", env_group);
  ground_check->setChecked(false);
  env_layout->addWidget(ground_check);
  QObject::connect(ground_check,
                   &QCheckBox::toggled,
                   viewport,
                   &oros::ui::ViewportWidget::set_ground_plane_enabled);

  auto *ground_el_lbl = new QLabel("Ground Elevation: 0.0 m", env_group);
  auto *ground_el_slider = new QSlider(Qt::Horizontal, env_group);
  ground_el_slider->setRange(-50, 100);
  ground_el_slider->setValue(0);
  env_layout->addWidget(ground_el_lbl);
  env_layout->addWidget(ground_el_slider);
  QObject::connect(
      ground_el_slider,
      &QSlider::valueChanged,
      [viewport, ground_el_lbl](int val)
      {
        const float elev = static_cast<float>(val);
        ground_el_lbl->setText(
            QString("Ground Elevation: %1 m").arg(elev, 0, 'f', 1));
        viewport->set_ground_plane_elevation(elev);
      });

  panel_layout->addWidget(env_group);

  // 3. Multi-Terrain Transform Interactive Controls
  auto *ter_group = new QGroupBox("East Terrain Transform", panel);
  auto *ter_layout = new QVBoxLayout(ter_group);

  auto *rot_lbl = new QLabel("Rotation: 32 deg", ter_group);
  auto *rot_slider = new QSlider(Qt::Horizontal, ter_group);
  rot_slider->setRange(-180, 180);
  rot_slider->setValue(32);
  ter_layout->addWidget(rot_lbl);
  ter_layout->addWidget(rot_slider);

  QObject::connect(rot_slider,
                   &QSlider::valueChanged,
                   [viewport, rot_lbl](int val)
                   {
                     const float rot = static_cast<float>(val);
                     rot_lbl->setText(QString("Rotation: %1 deg").arg(val));
                     viewport->update_terrain_transform("terrain_east_rotated",
                                                        {520.0f, -150.0f, 0.0f},
                                                        850.0f,
                                                        750.0f,
                                                        rot);
                   });

  auto *lod_col_check = new QCheckBox("Show CDLOD Level Colors", ter_group);
  lod_col_check->setChecked(false);
  ter_layout->addWidget(lod_col_check);
  QObject::connect(lod_col_check,
                   &QCheckBox::toggled,
                   viewport,
                   &oros::ui::ViewportWidget::set_lod_color_debug_enabled);

  auto *wire_check = new QCheckBox("Wireframe Mode", ter_group);
  wire_check->setChecked(false);
  ter_layout->addWidget(wire_check);
  QObject::connect(wire_check,
                   &QCheckBox::toggled,
                   viewport,
                   &oros::ui::ViewportWidget::set_wireframe_enabled);

  panel_layout->addWidget(ter_group);
  panel_layout->addStretch();

  scroll->setWidget(panel);
  root_layout->addWidget(scroll);

  main_window.setCentralWidget(central);
  main_window.show();

  return app.exec();
}
