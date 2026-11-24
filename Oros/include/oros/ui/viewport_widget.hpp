/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "oros/scene/scene_types.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QTimer>
#include <QVector3D>
#include <QWidget>

namespace oros::ui
{

// --- ViewportWidget class
// Real-time 3D Google Filament viewport widget for Hesiod terrain rendering:
// - Multi-terrain CDLOD rendering (position, size, rotation, elevation scale)
// - GPU-instanced scatter objects (trees, rocks, custom meshes)
// - Dynamic sun position & trajectory
// - On-demand PBR environment (soft shadows, height/aerial fog, atmospheric
//   scattering, GTAO)

class ViewportWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ViewportWidget(QWidget *parent = nullptr);
  ~ViewportWidget() override;

  // --- Multi-terrain management

  void add_or_update_terrain(const scene::TerrainLayerDescriptor &descriptor);
  void clear_terrains();
  [[nodiscard]] size_t get_active_cdlod_node_count() const noexcept;
  [[nodiscard]] size_t get_terrain_count() const noexcept;
  void                 remove_terrain(const std::string &id);
  void                 set_terrain_visible(const std::string &id, bool visible);
  void                 set_albedo_enabled(bool enabled);
  void                 set_normal_map_enabled(bool enabled);
  void                 set_height_scale_multiplier(float multiplier);
  [[nodiscard]] float  get_height_scale_multiplier() const noexcept;
  void                 update_terrain_transform(const std::string   &id,
                                                const terrain::Vec3 &center,
                                                float                lx,
                                                float                ly,
                                                float                rotation_deg);

  // --- Water layer management

  void add_or_update_water(const scene::WaterLayerDescriptor &descriptor);
  void clear_waters();
  void remove_water(const std::string &id);
  void set_water_visible(const std::string &id, bool visible);
  [[nodiscard]] const scene::WaterSettings &get_water_settings() const noexcept;
  void set_water_settings(const scene::WaterSettings &settings);

  // Helper to sample world position (x, y, z) on a named terrain at normalized
  // (u, v) in [0, 1]
  [[nodiscard]] terrain::Vec3 sample_terrain_world_pos(const std::string &id,
                                                       float              u,
                                                       float v) const;

  // --- Instanced object management (trees, rocks, scatter)

  void add_or_update_instanced_group(const scene::InstancedObjectGroup &group);
  void clear_instanced_groups();
  [[nodiscard]] size_t get_instanced_group_count() const noexcept;
  [[nodiscard]] size_t get_total_object_instance_count() const noexcept;
  void                 remove_instanced_group(const std::string &id);
  void                 set_instanced_group_visible(const std::string &id,
                                                   bool               visible);

  // --- Interactive path management

  void                 add_or_update_path(const scene::PathDescriptor &path);
  void                 clear_paths();
  [[nodiscard]] size_t get_path_count() const noexcept;
  void                 remove_path(const std::string &id);
  void                 set_path_visible(const std::string &id, bool visible);

  // --- Sun & Sky controls

  [[nodiscard]] const scene::SunSettings &get_sun_settings() const noexcept;
  void set_sun_angles(float azimuth_deg, float elevation_deg);
  void set_sun_settings(const scene::SunSettings &settings);

  // --- PBR Environment controls

  [[nodiscard]] const scene::EnvironmentSettings &get_environment_settings()
      const noexcept;
  [[nodiscard]] float get_ground_plane_elevation() const noexcept;
  [[nodiscard]] bool  is_ground_plane_enabled() const noexcept;
  void                set_atmospheric_scattering_enabled(bool enabled);
  void set_environment_settings(const scene::EnvironmentSettings &settings);
  void set_fog_enabled(bool enabled);
  void set_ground_plane_elevation(float elevation);
  void set_ground_plane_enabled(bool enabled);
  void set_pbr_environment_enabled(bool enabled);
  void set_soft_shadows_enabled(bool enabled);

  // --- CDLOD & Shading debug controls

  [[nodiscard]] size_t   get_grid_resolution() const noexcept;
  [[nodiscard]] float    get_lod_distance_scale() const noexcept;
  [[nodiscard]] uint32_t get_max_lod_levels() const noexcept;
  void                   set_grid_resolution(size_t resolution);
  void                   set_lod_color_debug_enabled(bool enabled);
  void                   set_lod_distance_scale(float scale);
  void                   set_max_lod_levels(uint32_t levels);
  void                   set_morphing_enabled(bool enabled);
  void                   set_shading_enabled(bool enabled);
  void                   set_wireframe_enabled(bool enabled);

  // --- Camera & Viewport properties

  [[nodiscard]] const QColor &get_background_color() const;
  [[nodiscard]] QVector3D     get_camera_pos() const noexcept;
  [[nodiscard]] QVector3D     get_camera_target() const noexcept;
  [[nodiscard]] float         get_fov_deg() const noexcept;
  [[nodiscard]] float         get_fps() const;
  [[nodiscard]] float         get_frame_time_ms() const;
  [[nodiscard]] QImage        grab_framebuffer();
  void                        reset_camera();
  void                        set_background_color(const QColor &color);
  void                        set_camera_orbit(const QVector3D &target,
                                               float            distance,
                                               float            yaw_deg,
                                               float            pitch_deg);
  void                        set_fov_deg(float fov_deg);

signals:
  void camera_moved(float x, float y, float z);
  void fps_updated(float fps, float frame_time_ms);
  void lod_selection_updated(size_t active_patch_nodes);
  void path_changed(const std::string                &id,
                    const std::vector<terrain::Vec3> &points);
  void path_point_selected(const std::string &id, int point_index);

protected:
  void          mouseDoubleClickEvent(QMouseEvent *event) override;
  void          mouseMoveEvent(QMouseEvent *event) override;
  void          mousePressEvent(QMouseEvent *event) override;
  void          mouseReleaseEvent(QMouseEvent *event) override;
  void          paintEvent(QPaintEvent *event) override;
  QPaintEngine *paintEngine() const override;
  void          resizeEvent(QResizeEvent *event) override;
  void          showEvent(QShowEvent *event) override;
  void          hideEvent(QHideEvent *event) override;
  void          wheelEvent(QWheelEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void init_filament();
  void recompute_all_lods();
  void render_frame();
  void update_camera();
  void update_environment_and_sun();
  void update_ground_plane();
  void update_instanced_renderables();
  void update_shared_grid_mesh();
  void update_sky_dome();
  void update_terrain_renderables();
  void update_water_renderables();

  QColor background_color_{30, 30, 35};
  float  current_fps_{0.0f};
  float  current_frame_time_ms_{0.0f};
  bool   is_dragging_{false};
  QPoint last_mouse_pos_{};

  // Orbit camera parameters
  float     camera_yaw_{-45.0f};
  float     camera_pitch_{32.0f};
  float     camera_distance_{900.0f};
  float     camera_fov_deg_{45.0f};
  QVector3D camera_target_{0.0f, 0.0f, 0.0f};

  // Terrain global settings
  float height_scale_multiplier_{1.0f};
  bool  albedo_enabled_{true};
  bool  normal_map_enabled_{true};

  // CDLOD global settings
  size_t   grid_res_{33};
  uint32_t max_lod_levels_{7};
  float    lod_dist_scale_{1.0f};
  bool     morphing_enabled_{true};
  bool     shading_enabled_{true};
  bool     wireframe_enabled_{false};
  bool     lod_color_debug_{false};

  // Sun, Environment and Water state
  scene::SunSettings         sun_settings_{};
  scene::EnvironmentSettings env_settings_{};
  scene::WaterSettings       water_settings_{};

  QTimer                               *render_timer_{nullptr};
  std::chrono::steady_clock::time_point last_frame_time_{};
  bool                                  filament_initialized_{false};
};

} // namespace oros::ui
