/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <memory>
#include <vector>

#include "oros/terrain/cdlod_quadtree.hpp"
#include "oros/terrain/heightmap_data.hpp"
#include "oros/terrain/heightmap_patch.hpp"

#include <QMouseEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVector3D>
#include <QWheelEvent>
#include <QWidget>
#include <utils/Entity.h>

// Forward declarations for Filament
namespace filament
{
class Camera;
class Engine;
class IndexBuffer;
class IndirectLight;
class Material;
class MaterialInstance;
class Renderer;
class Scene;
class Skybox;
class SwapChain;
class Texture;
class VertexBuffer;
class View;
} // namespace filament

namespace oros::ui
{

// --- PatchRenderNode
// Reusable pool node holding a Filament Entity and its MaterialInstances.

struct PatchRenderNode
{
  utils::Entity               entity{};
  filament::MaterialInstance *unlit_instance{nullptr};
  filament::MaterialInstance *lit_instance{nullptr};
  bool                        in_scene{false};
};

// --- CdlodFilamentWidget
// High-performance 3D Google Filament visual testing widget for CDLOD terrain
// rendering. Implements GPU vertex displacement via compiled Filament
// materials, static shared unit grid meshes, texture sampling, and entity
// pooling.

class CdlodFilamentWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CdlodFilamentWidget(QWidget *parent = nullptr);
  ~CdlodFilamentWidget() override;

  [[nodiscard]] size_t    get_instance_count() const noexcept;
  [[nodiscard]] QVector3D get_camera_pos() const noexcept;

  void create_patch();
  void generate_noise();
  void regenerate_noise();
  void set_grid_resolution(size_t resolution);
  void set_lod_distance_scale(float scale);
  void set_max_lod_levels(uint32_t levels);
  void set_morphing_enabled(bool enabled);
  void set_noise_frequency(float freq);
  void set_shading_mode(bool enabled);
  void set_wireframe_mode(bool enabled);

signals:
  void camera_moved(float x, float y, float z);
  void selection_updated(size_t node_count);

protected:
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override
  {
    Q_UNUSED(event);
  }
  QPaintEngine *paintEngine() const override
  {
    return nullptr;
  }
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  void clear_all_nodes();
  void destroy_filament_resources();
  void init_filament();
  void init_materials();
  void recompute_lod();
  void render_frame();
  void update_camera();
  void update_patch_renderables();
  void update_shared_grid_mesh();
  void update_textures();

  // Terrain generation parameters
  size_t heightmap_res_{16 * 512};
  size_t grid_res_{33};
  float  noise_frequency_{2.0f};
  float  world_size_{1000.0f};
  float  height_scale_{150.0f};
  float  lod_dist_scale_{1.0f};
  bool   morphing_enabled_{true};
  bool   wireframe_mode_{true};
  bool   shading_enabled_{true};

  // Camera parameters (Orbit Camera)
  float     camera_yaw_{-45.0f};
  float     camera_pitch_{35.0f};
  float     camera_distance_{600.0f};
  QVector3D camera_target_{0.0f, 0.0f, 0.0f};
  QPoint    last_mouse_pos_{};

  // Terrain representation
  std::shared_ptr<oros::terrain::HeightmapData>  heightmap_data_{nullptr};
  std::unique_ptr<oros::terrain::HeightmapPatch> patch_{nullptr};
  oros::terrain::CDLODQuadTree                   quadtree_{8};
  oros::terrain::CDLODSelectionContext           context_{};
  std::vector<oros::terrain::CDLODInstance>      instances_{};
  std::vector<uint8_t>                           normal_map_data_{};

  // Filament core objects
  filament::Engine        *engine_{nullptr};
  filament::Renderer      *renderer_{nullptr};
  filament::Scene         *scene_{nullptr};
  filament::View          *view_{nullptr};
  filament::Camera        *camera_{nullptr};
  filament::SwapChain     *swap_chain_{nullptr};
  filament::Skybox        *skybox_{nullptr};
  filament::IndirectLight *indirect_light_{nullptr};

  std::unique_ptr<utils::Entity> camera_entity_;
  std::unique_ptr<utils::Entity> sun_light_entity_;

  // Materials
  filament::Material *unlit_material_{nullptr};
  filament::Material *lit_material_{nullptr};

  // Shared static grid geometry
  filament::VertexBuffer *shared_vb_{nullptr};
  filament::IndexBuffer  *shared_solid_ib_{nullptr};
  filament::IndexBuffer  *shared_wire_ib_{nullptr};
  uint32_t                solid_index_count_{0};
  uint32_t                wire_index_count_{0};

  // Textures
  filament::Texture *heightmap_texture_{nullptr};
  filament::Texture *normal_texture_{nullptr};

  // Entity pool for fast O(1) reuse without GPU buffer churn
  std::vector<PatchRenderNode> node_pool_{};
  size_t                       active_node_count_{0};

  QTimer *render_timer_{nullptr};
  bool    initialized_{false};
};

} // namespace oros::ui
