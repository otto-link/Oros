/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "oros/logger.hpp"
#include "oros/terrain/cdlod_quadtree.hpp"
#include "oros/terrain/grid_mesh_factory.hpp"
#include "oros/ui/viewport_widget.hpp"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QtMath>
#include <filament/Fence.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Options.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <numbers>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

namespace
{

static const uint8_t MATERIAL_TERRAIN_UNLIT_PKG[] = {
#include "materials/cdlod_terrain_unlit.h"
};

static const uint8_t MATERIAL_TERRAIN_LIT_PKG[] = {
#include "materials/cdlod_terrain_lit.h"
};

static const uint8_t MATERIAL_INSTANCED_OBJECT_PKG[] = {
#include "materials/instanced_object_lit.h"
};

static const uint8_t MATERIAL_SKY_ATMOSPHERE_PKG[] = {
#include "materials/sky_atmosphere.h"
};

static const uint8_t MATERIAL_GROUND_PLANE_PKG[] = {
#include "materials/ground_plane_lit.h"
};

static const uint8_t MATERIAL_WATER_LIT_PKG[] = {
#include "materials/water_lit.h"
};

constexpr size_t MAX_INSTANCES_PER_BATCH = 16384;
constexpr int    INSTANCE_TEX_WIDTH = 128; // 128 instances per row (256 texels)

filament::math::quatf pack_normal_to_tangent_frame(
    const oros::terrain::Vec3 &normal)
{
  filament::math::float3 n(normal.x, normal.y, normal.z);
  const float            n_len = length(n);
  if (n_len > 1e-6f)
  {
    n /= n_len;
  }
  else
  {
    n = {0.0f, 0.0f, 1.0f};
  }

  const filament::math::float3 up = (std::abs(n.z) < 0.99f)
                                        ? filament::math::float3(0.0f,
                                                                 0.0f,
                                                                 1.0f)
                                        : filament::math::float3(1.0f,
                                                                 0.0f,
                                                                 0.0f);
  const filament::math::float3 t = normalize(cross(up, n));
  const filament::math::float3 b = cross(n, t);

  filament::math::mat3f tbn;
  tbn[0] = t;
  tbn[1] = b;
  tbn[2] = n;

  return filament::math::mat3f::packTangentFrame(tbn);
}

} // namespace

namespace oros::ui
{

// --- Internal Filament structures

struct PatchRenderNode
{
  utils::Entity               entity{};
  filament::MaterialInstance *unlit_instance{nullptr};
  filament::MaterialInstance *lit_instance{nullptr};
  bool                        in_scene{false};
};

struct TerrainGpuEntry
{
  scene::TerrainLayerDescriptor            descriptor{};
  std::unique_ptr<terrain::HeightmapPatch> patch{nullptr};
  std::vector<terrain::CDLODInstance>      lod_instances{};
  filament::Texture                       *heightmap_tex{nullptr};
  filament::Texture                       *normal_tex{nullptr};
  filament::Texture                       *albedo_tex{nullptr};
  bool                                     textures_dirty{true};
  bool                                     visible{true};
};

struct WaterPatchNode
{
  utils::Entity               entity{};
  filament::MaterialInstance *mat_instance{nullptr};
  bool                        in_scene{false};
};

struct WaterGpuEntry
{
  scene::WaterLayerDescriptor              descriptor{};
  std::unique_ptr<terrain::HeightmapPatch> patch{nullptr};
  std::vector<terrain::CDLODInstance>      lod_instances{};
  filament::Texture                       *heightmap_tex{nullptr};
  filament::Texture                       *depth_tex{nullptr};
  bool                                     textures_dirty{true};
  bool                                     visible{true};
};

struct InstancedBatchGpu
{
  utils::Entity               entity{};
  filament::MaterialInstance *mat_instance{nullptr};
  filament::Texture          *instance_data_tex{nullptr};
  bool                        in_scene{false};
};

struct InstancedGroupGpuEntry
{
  scene::InstancedObjectGroup    group{};
  filament::VertexBuffer        *vb{nullptr};
  filament::IndexBuffer         *ib{nullptr};
  uint32_t                       index_count{0};
  std::vector<InstancedBatchGpu> batches{};
  bool                           gpu_dirty{true};
  bool                           visible{true};
};

struct PathGpuEntry
{
  scene::PathDescriptor       path{};
  filament::VertexBuffer     *vb{nullptr};
  filament::IndexBuffer      *ib{nullptr};
  uint32_t                    index_count{0};
  filament::MaterialInstance *mat_instance{nullptr};
  filament::Texture          *instance_data_tex{nullptr};
  utils::Entity               entity{};
  bool                        in_scene{false};
  bool                        gpu_dirty{true};
  bool                        visible{true};
};

enum class PathDragMode : uint8_t
{
  NONE = 0,
  XY_PLANE,
  Z_AXIS
};

struct ViewportWidget::Impl
{
  filament::Engine        *engine{nullptr};
  filament::Renderer      *renderer{nullptr};
  filament::Scene         *scene{nullptr};
  filament::View          *view{nullptr};
  filament::Camera        *camera{nullptr};
  filament::SwapChain     *swap_chain{nullptr};
  filament::Skybox        *skybox{nullptr};
  filament::IndirectLight *indirect_light{nullptr};

  utils::Entity camera_entity{};
  utils::Entity sun_light_entity{};

  // Sky dome for atmospheric scattering
  utils::Entity               sky_dome_entity{};
  filament::VertexBuffer     *sky_dome_vb{nullptr};
  filament::IndexBuffer      *sky_dome_ib{nullptr};
  uint32_t                    sky_dome_index_count{0};
  filament::Material         *sky_material{nullptr};
  filament::MaterialInstance *sky_mat_instance{nullptr};
  bool                        sky_dome_in_scene{false};

  // Optional infinite ground plane
  utils::Entity               ground_plane_entity{};
  filament::VertexBuffer     *ground_plane_vb{nullptr};
  filament::IndexBuffer      *ground_plane_ib{nullptr};
  filament::Material         *ground_plane_mat{nullptr};
  filament::MaterialInstance *ground_plane_mat_instance{nullptr};
  bool                        ground_plane_in_scene{false};

  // Materials
  filament::Material *terrain_unlit_mat{nullptr};
  filament::Material *terrain_lit_mat{nullptr};
  filament::Material *instanced_obj_mat{nullptr};
  filament::Material *water_mat{nullptr};

  // Shared static grid mesh for CDLOD
  filament::VertexBuffer *shared_grid_vb{nullptr};
  filament::IndexBuffer  *shared_solid_ib{nullptr};
  filament::IndexBuffer  *shared_wire_ib{nullptr};
  uint32_t                solid_index_count{0};
  uint32_t                wire_index_count{0};

  // Fallback 1x1 white texture
  filament::Texture *fallback_white_tex{nullptr};

  // Multi-terrain state
  terrain::CDLODQuadTree                 quadtree{7};
  std::map<std::string, TerrainGpuEntry> terrains{};
  std::vector<PatchRenderNode>           terrain_node_pool{};
  size_t                                 active_cdlod_nodes{0};

  // Water layers state
  std::map<std::string, WaterGpuEntry> waters{};
  std::vector<WaterPatchNode>          water_node_pool{};

  // Instanced objects state
  std::map<std::string, InstancedGroupGpuEntry> instanced_groups{};

  // Interactive paths state
  std::map<std::string, PathGpuEntry> paths{};
  std::string                         selected_path_id{};
  int                                 selected_point_index{-1};
  PathDragMode                        path_drag_mode{PathDragMode::NONE};

  void destroy_path_gpu(PathGpuEntry &entry)
  {
    if (!engine)
    {
      return;
    }

    if (entry.in_scene && scene && entry.entity)
    {
      scene->remove(entry.entity);
      entry.in_scene = false;
    }

    if (entry.entity)
    {
      engine->destroy(entry.entity);
      utils::EntityManager::get().destroy(entry.entity);
      entry.entity = {};
    }

    if (entry.mat_instance)
    {
      engine->destroy(entry.mat_instance);
      entry.mat_instance = nullptr;
    }

    if (entry.instance_data_tex)
    {
      engine->destroy(entry.instance_data_tex);
      entry.instance_data_tex = nullptr;
    }

    if (entry.vb)
    {
      engine->destroy(entry.vb);
      entry.vb = nullptr;
    }

    if (entry.ib)
    {
      engine->destroy(entry.ib);
      entry.ib = nullptr;
    }

    entry.index_count = 0;
  }

  void destroy_terrain_textures(TerrainGpuEntry &entry)
  {
    if (!engine)
    {
      return;
    }

    if (entry.heightmap_tex)
    {
      engine->destroy(entry.heightmap_tex);
      entry.heightmap_tex = nullptr;
    }

    if (entry.normal_tex)
    {
      engine->destroy(entry.normal_tex);
      entry.normal_tex = nullptr;
    }

    if (entry.albedo_tex)
    {
      engine->destroy(entry.albedo_tex);
      entry.albedo_tex = nullptr;
    }
  }

  void destroy_instanced_group_gpu(InstancedGroupGpuEntry &entry)
  {
    if (!engine)
    {
      return;
    }

    for (auto &batch : entry.batches)
    {
      if (batch.in_scene && batch.entity)
      {
        scene->remove(batch.entity);
        batch.in_scene = false;
      }

      if (batch.entity)
      {
        engine->destroy(batch.entity);
        utils::EntityManager::get().destroy(batch.entity);
        batch.entity = {};
      }

      if (batch.mat_instance)
      {
        engine->destroy(batch.mat_instance);
        batch.mat_instance = nullptr;
      }

      if (batch.instance_data_tex)
      {
        engine->destroy(batch.instance_data_tex);
        batch.instance_data_tex = nullptr;
      }
    }
    entry.batches.clear();

    if (entry.vb)
    {
      engine->destroy(entry.vb);
      entry.vb = nullptr;
    }

    if (entry.ib)
    {
      engine->destroy(entry.ib);
      entry.ib = nullptr;
    }
  }

  void clear_terrain_node_pool()
  {
    if (!engine)
    {
      return;
    }

    for (auto &node : terrain_node_pool)
    {
      if (node.in_scene && node.entity)
      {
        scene->remove(node.entity);
        node.in_scene = false;
      }

      if (node.entity)
      {
        engine->destroy(node.entity);
        utils::EntityManager::get().destroy(node.entity);
        node.entity = {};
      }

      if (node.unlit_instance)
      {
        engine->destroy(node.unlit_instance);
        node.unlit_instance = nullptr;
      }

      if (node.lit_instance)
      {
        engine->destroy(node.lit_instance);
        node.lit_instance = nullptr;
      }
    }

    terrain_node_pool.clear();
    active_cdlod_nodes = 0;
  }

  void destroy_water_textures(WaterGpuEntry &entry)
  {
    if (!engine)
    {
      return;
    }

    if (entry.heightmap_tex)
    {
      engine->destroy(entry.heightmap_tex);
      entry.heightmap_tex = nullptr;
    }

    if (entry.depth_tex)
    {
      engine->destroy(entry.depth_tex);
      entry.depth_tex = nullptr;
    }
  }

  void clear_water_node_pool()
  {
    if (!engine)
    {
      return;
    }

    for (auto &node : water_node_pool)
    {
      if (node.in_scene && scene && node.entity)
      {
        scene->remove(node.entity);
        node.in_scene = false;
      }
      if (node.entity)
      {
        engine->destroy(node.entity);
        utils::EntityManager::get().destroy(node.entity);
        node.entity = {};
      }
      if (node.mat_instance)
      {
        engine->destroy(node.mat_instance);
        node.mat_instance = nullptr;
      }
    }

    water_node_pool.clear();
  }

  void destroy_all()
  {
    if (!engine)
    {
      return;
    }

    clear_terrain_node_pool();
    clear_water_node_pool();

    for (auto &[id, t_entry] : terrains)
    {
      destroy_terrain_textures(t_entry);
    }

    for (auto &[id, w_entry] : waters)
    {
      destroy_water_textures(w_entry);
    }

    for (auto &[id, g_entry] : instanced_groups)
    {
      destroy_instanced_group_gpu(g_entry);
    }

    for (auto &[id, p_entry] : paths)
    {
      destroy_path_gpu(p_entry);
    }

    if (sky_dome_in_scene && sky_dome_entity)
    {
      scene->remove(sky_dome_entity);
      sky_dome_in_scene = false;
    }

    if (sky_dome_entity)
    {
      engine->destroy(sky_dome_entity);
      utils::EntityManager::get().destroy(sky_dome_entity);
      sky_dome_entity = {};
    }

    if (sky_mat_instance)
    {
      engine->destroy(sky_mat_instance);
      sky_mat_instance = nullptr;
    }

    if (sky_dome_vb)
    {
      engine->destroy(sky_dome_vb);
      sky_dome_vb = nullptr;
    }

    if (sky_dome_ib)
    {
      engine->destroy(sky_dome_ib);
      sky_dome_ib = nullptr;
    }

    if (ground_plane_in_scene && ground_plane_entity)
    {
      scene->remove(ground_plane_entity);
      ground_plane_in_scene = false;
    }

    if (ground_plane_entity)
    {
      engine->destroy(ground_plane_entity);
      utils::EntityManager::get().destroy(ground_plane_entity);
      ground_plane_entity = {};
    }

    if (ground_plane_mat_instance)
    {
      engine->destroy(ground_plane_mat_instance);
      ground_plane_mat_instance = nullptr;
    }

    if (ground_plane_vb)
    {
      engine->destroy(ground_plane_vb);
      ground_plane_vb = nullptr;
    }

    if (ground_plane_ib)
    {
      engine->destroy(ground_plane_ib);
      ground_plane_ib = nullptr;
    }

    if (ground_plane_mat)
    {
      engine->destroy(ground_plane_mat);
      ground_plane_mat = nullptr;
    }

    if (shared_grid_vb)
    {
      engine->destroy(shared_grid_vb);
      shared_grid_vb = nullptr;
    }

    if (shared_solid_ib)
    {
      engine->destroy(shared_solid_ib);
      shared_solid_ib = nullptr;
    }

    if (shared_wire_ib)
    {
      engine->destroy(shared_wire_ib);
      shared_wire_ib = nullptr;
    }

    if (fallback_white_tex)
    {
      engine->destroy(fallback_white_tex);
      fallback_white_tex = nullptr;
    }

    if (sun_light_entity)
    {
      scene->remove(sun_light_entity);
      engine->destroy(sun_light_entity);
      utils::EntityManager::get().destroy(sun_light_entity);
      sun_light_entity = {};
    }

    if (indirect_light)
    {
      scene->setIndirectLight(nullptr);
      engine->destroy(indirect_light);
      indirect_light = nullptr;
    }

    if (camera_entity)
    {
      engine->destroyCameraComponent(camera_entity);
      utils::EntityManager::get().destroy(camera_entity);
      camera_entity = {};
    }

    if (skybox)
    {
      scene->setSkybox(nullptr);
      engine->destroy(skybox);
      skybox = nullptr;
    }

    if (terrain_lit_mat)
    {
      engine->destroy(terrain_lit_mat);
      terrain_lit_mat = nullptr;
    }

    if (terrain_unlit_mat)
    {
      engine->destroy(terrain_unlit_mat);
      terrain_unlit_mat = nullptr;
    }

    if (instanced_obj_mat)
    {
      engine->destroy(instanced_obj_mat);
      instanced_obj_mat = nullptr;
    }

    if (sky_material)
    {
      engine->destroy(sky_material);
      sky_material = nullptr;
    }

    if (view)
    {
      engine->destroy(view);
      view = nullptr;
    }

    if (scene)
    {
      engine->destroy(scene);
      scene = nullptr;
    }

    if (renderer)
    {
      engine->destroy(renderer);
      renderer = nullptr;
    }

    if (swap_chain)
    {
      engine->destroy(swap_chain);
      swap_chain = nullptr;
    }

    filament::Engine::destroy(&engine);
    engine = nullptr;
  }
};

// --- Construction & Destruction

ViewportWidget::ViewportWidget(QWidget *parent)
    : QWidget(parent),
      impl_(std::make_unique<Impl>()),
      last_frame_time_(std::chrono::steady_clock::now())
{
  const QString platform = QGuiApplication::platformName();
  if (platform != "offscreen" && platform != "minimal")
  {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
  }
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  impl_->quadtree.set_max_lod_levels(max_lod_levels_);

  render_timer_ = new QTimer(this);
  connect(render_timer_, &QTimer::timeout, this, &ViewportWidget::render_frame);
}

ViewportWidget::~ViewportWidget()
{
  if (render_timer_)
  {
    render_timer_->stop();
  }

  impl_->destroy_all();
}

// --- Multi-terrain management

void ViewportWidget::add_or_update_terrain(
    const scene::TerrainLayerDescriptor &descriptor)
{
  if (descriptor.id.empty() || !descriptor.heightmap)
  {
    return;
  }

  auto &entry = impl_->terrains[descriptor.id];
  impl_->destroy_terrain_textures(entry);

  entry.descriptor = descriptor;
  if (entry.descriptor.normal_map_rgb8.empty())
  {
    entry.descriptor.normal_map_rgb8 = scene::compute_normal_map_rgb8(
        *descriptor.heightmap,
        descriptor.lx,
        descriptor.ly,
        descriptor.height_scale);
  }

  entry.patch = std::make_unique<terrain::HeightmapPatch>(
      descriptor.id,
      descriptor.heightmap,
      descriptor.center,
      descriptor.lx,
      descriptor.ly,
      descriptor.rotation_deg);
  entry.textures_dirty = true;

  recompute_all_lods();
}

void ViewportWidget::clear_terrains()
{
  for (auto &[id, entry] : impl_->terrains)
  {
    impl_->destroy_terrain_textures(entry);
  }

  impl_->terrains.clear();
  impl_->clear_terrain_node_pool();
  emit lod_selection_updated(0);
}

size_t ViewportWidget::get_active_cdlod_node_count() const noexcept
{
  return impl_->active_cdlod_nodes;
}

size_t ViewportWidget::get_terrain_count() const noexcept
{
  return impl_->terrains.size();
}

void ViewportWidget::remove_terrain(const std::string &id)
{
  auto it = impl_->terrains.find(id);
  if (it != impl_->terrains.end())
  {
    impl_->destroy_terrain_textures(it->second);
    impl_->terrains.erase(it);
    recompute_all_lods();
  }
}

terrain::Vec3 ViewportWidget::sample_terrain_world_pos(const std::string &id,
                                                       float              u,
                                                       float v) const
{
  auto it = impl_->terrains.find(id);
  if (it == impl_->terrains.end() || !it->second.patch)
  {
    return {0.0f, 0.0f, 0.0f};
  }

  const auto &entry = it->second;
  const float norm_z = entry.patch->sample_elevation(u, v);
  const float world_z_offset = norm_z * entry.descriptor.height_scale;

  return entry.patch->local_to_world(u, v, world_z_offset);
}

void ViewportWidget::update_terrain_transform(const std::string   &id,
                                              const terrain::Vec3 &center,
                                              float                lx,
                                              float                ly,
                                              float                rotation_deg)
{
  auto it = impl_->terrains.find(id);
  if (it == impl_->terrains.end() || !it->second.patch)
  {
    return;
  }

  auto &entry = it->second;
  entry.descriptor.center = center;
  entry.descriptor.lx = lx;
  entry.descriptor.ly = ly;
  entry.descriptor.rotation_deg = rotation_deg;

  entry.patch->set_center(center);
  entry.patch->set_dimensions(lx, ly);
  entry.patch->set_rotation_deg(rotation_deg);

  recompute_all_lods();
}

// --- Instanced object management (trees, rocks, scatter)

void ViewportWidget::add_or_update_instanced_group(
    const scene::InstancedObjectGroup &group)
{
  if (group.id.empty())
  {
    return;
  }

  auto &entry = impl_->instanced_groups[group.id];
  impl_->destroy_instanced_group_gpu(entry);

  entry.group = group;
  entry.gpu_dirty = true;

  if (filament_initialized_)
  {
    update_instanced_renderables();
  }
}

void ViewportWidget::clear_instanced_groups()
{
  for (auto &[id, entry] : impl_->instanced_groups)
  {
    impl_->destroy_instanced_group_gpu(entry);
  }

  impl_->instanced_groups.clear();
}

size_t ViewportWidget::get_instanced_group_count() const noexcept
{
  return impl_->instanced_groups.size();
}

size_t ViewportWidget::get_total_object_instance_count() const noexcept
{
  size_t total = 0;
  for (const auto &[id, entry] : impl_->instanced_groups)
  {
    total += entry.group.instances.size();
  }

  return total;
}

void ViewportWidget::remove_instanced_group(const std::string &id)
{
  auto it = impl_->instanced_groups.find(id);
  if (it != impl_->instanced_groups.end())
  {
    impl_->destroy_instanced_group_gpu(it->second);
    impl_->instanced_groups.erase(it);
  }
}

// --- Interactive path management

void ViewportWidget::add_or_update_path(const scene::PathDescriptor &path)
{
  auto &entry = impl_->paths[path.id];
  entry.path = path;
  entry.gpu_dirty = true;

  if (impl_->selected_path_id == path.id &&
      impl_->selected_point_index >= static_cast<int>(path.points.size()))
  {
    impl_->selected_point_index = -1;
    impl_->path_drag_mode = PathDragMode::NONE;
  }
}

void ViewportWidget::clear_paths()
{
  for (auto &[id, entry] : impl_->paths)
  {
    impl_->destroy_path_gpu(entry);
  }
  impl_->paths.clear();
  impl_->selected_path_id.clear();
  impl_->selected_point_index = -1;
  impl_->path_drag_mode = PathDragMode::NONE;
}

size_t ViewportWidget::get_path_count() const noexcept
{
  return impl_->paths.size();
}

void ViewportWidget::remove_path(const std::string &id)
{
  auto it = impl_->paths.find(id);
  if (it != impl_->paths.end())
  {
    impl_->destroy_path_gpu(it->second);
    impl_->paths.erase(it);

    if (impl_->selected_path_id == id)
    {
      impl_->selected_path_id.clear();
      impl_->selected_point_index = -1;
      impl_->path_drag_mode = PathDragMode::NONE;
    }
  }
}

// --- Sun & Sky controls

const scene::SunSettings &ViewportWidget::get_sun_settings() const noexcept
{
  return sun_settings_;
}

void ViewportWidget::set_sun_angles(float azimuth_deg, float elevation_deg)
{
  sun_settings_.azimuth_deg = azimuth_deg;
  sun_settings_.elevation_deg = elevation_deg;

  if (filament_initialized_)
  {
    update_environment_and_sun();
  }
}

void ViewportWidget::set_sun_settings(const scene::SunSettings &settings)
{
  sun_settings_ = settings;

  if (filament_initialized_)
  {
    update_environment_and_sun();
  }
}

// --- PBR Environment controls

const scene::EnvironmentSettings &ViewportWidget::get_environment_settings()
    const noexcept
{
  return env_settings_;
}

float ViewportWidget::get_ground_plane_elevation() const noexcept
{
  return env_settings_.ground_plane_elevation;
}

bool ViewportWidget::is_ground_plane_enabled() const noexcept
{
  return env_settings_.ground_plane_enabled;
}

void ViewportWidget::set_atmospheric_scattering_enabled(bool enabled)
{
  env_settings_.atmospheric_scattering_enabled = enabled;

  if (filament_initialized_)
  {
    update_environment_and_sun();
  }
}

void ViewportWidget::set_environment_settings(
    const scene::EnvironmentSettings &settings)
{
  env_settings_ = settings;

  if (filament_initialized_)
  {
    update_environment_and_sun();
    update_terrain_renderables();
  }
}

void ViewportWidget::set_fog_enabled(bool enabled)
{
  env_settings_.fog_enabled = enabled;

  if (filament_initialized_)
  {
    update_environment_and_sun();
  }
}

void ViewportWidget::set_ground_plane_elevation(float elevation)
{
  env_settings_.ground_plane_elevation = elevation;

  if (filament_initialized_)
  {
    update_ground_plane();
  }
}

void ViewportWidget::set_ground_plane_enabled(bool enabled)
{
  env_settings_.ground_plane_enabled = enabled;

  if (filament_initialized_)
  {
    update_ground_plane();
  }
}

void ViewportWidget::set_pbr_environment_enabled(bool enabled)
{
  env_settings_.pbr_enabled = enabled;

  if (filament_initialized_)
  {
    update_environment_and_sun();
    update_terrain_renderables();
  }
}

void ViewportWidget::set_soft_shadows_enabled(bool enabled)
{
  env_settings_.soft_shadows_enabled = enabled;

  if (filament_initialized_)
  {
    update_environment_and_sun();
  }
}

// --- CDLOD & Shading debug controls

size_t ViewportWidget::get_grid_resolution() const noexcept
{
  return grid_res_;
}

float ViewportWidget::get_lod_distance_scale() const noexcept
{
  return lod_dist_scale_;
}

uint32_t ViewportWidget::get_max_lod_levels() const noexcept
{
  return max_lod_levels_;
}

void ViewportWidget::set_grid_resolution(size_t resolution)
{
  if (grid_res_ != resolution && resolution >= 3)
  {
    grid_res_ = resolution;
    if (filament_initialized_)
    {
      impl_->clear_terrain_node_pool();
      update_shared_grid_mesh();
      update_terrain_renderables();
    }
  }
}

void ViewportWidget::set_lod_color_debug_enabled(bool enabled)
{
  lod_color_debug_ = enabled;
  if (filament_initialized_)
  {
    update_terrain_renderables();
  }
}

void ViewportWidget::set_lod_distance_scale(float scale)
{
  lod_dist_scale_ = std::max(0.05f, scale);
  recompute_all_lods();
}

void ViewportWidget::set_max_lod_levels(uint32_t levels)
{
  max_lod_levels_ = std::clamp(levels, 1u, 12u);
  impl_->quadtree.set_max_lod_levels(max_lod_levels_);
  recompute_all_lods();
}

void ViewportWidget::set_morphing_enabled(bool enabled)
{
  morphing_enabled_ = enabled;
  if (filament_initialized_)
  {
    update_terrain_renderables();
  }
}

void ViewportWidget::set_shading_enabled(bool enabled)
{
  shading_enabled_ = enabled;
  if (filament_initialized_)
  {
    update_terrain_renderables();
  }
}

void ViewportWidget::set_wireframe_enabled(bool enabled)
{
  wireframe_enabled_ = enabled;
  if (filament_initialized_)
  {
    update_terrain_renderables();
  }
}

// --- Camera & Viewport properties

const QColor &ViewportWidget::get_background_color() const
{
  return background_color_;
}

QVector3D ViewportWidget::get_camera_pos() const noexcept
{
  const float rad_pitch = qDegreesToRadians(camera_pitch_);
  const float rad_yaw = qDegreesToRadians(camera_yaw_);

  const float x = camera_target_.x() +
                  camera_distance_ * std::cos(rad_pitch) * std::cos(rad_yaw);
  const float y = camera_target_.y() +
                  camera_distance_ * std::cos(rad_pitch) * std::sin(rad_yaw);
  const float z = camera_target_.z() + camera_distance_ * std::sin(rad_pitch);

  return QVector3D(x, y, z);
}

QVector3D ViewportWidget::get_camera_target() const noexcept
{
  return camera_target_;
}

float ViewportWidget::get_fps() const
{
  return current_fps_;
}

float ViewportWidget::get_frame_time_ms() const
{
  return current_frame_time_ms_;
}

void ViewportWidget::set_background_color(const QColor &color)
{
  background_color_ = color;

  if (filament_initialized_ && impl_->renderer)
  {
    filament::Renderer::ClearOptions clear_options;
    clear_options.clearColor = {static_cast<float>(color.redF()),
                                static_cast<float>(color.greenF()),
                                static_cast<float>(color.blueF()),
                                1.0f};
    clear_options.clear = true;
    clear_options.discard = true;
    impl_->renderer->setClearOptions(clear_options);
  }
}

void ViewportWidget::set_camera_orbit(const QVector3D &target,
                                      float            distance,
                                      float            yaw_deg,
                                      float            pitch_deg)
{
  camera_target_ = target;
  camera_distance_ = std::clamp(distance, 5.0f, 15000.0f);
  camera_yaw_ = yaw_deg;
  camera_pitch_ = std::clamp(pitch_deg, -89.0f, 89.0f);

  recompute_all_lods();
}

// --- Protected Qt events

namespace
{

void compute_camera_ray(const QPoint    &pos,
                        int              width,
                        int              height,
                        const QVector3D &eye,
                        const QVector3D &target,
                        QVector3D       &ray_origin,
                        QVector3D       &ray_dir)
{
  const float w = static_cast<float>(std::max(width, 1));
  const float h = static_cast<float>(std::max(height, 1));
  const float ndc_x = (2.0f * static_cast<float>(pos.x()) / w) - 1.0f;
  const float ndc_y = 1.0f - (2.0f * static_cast<float>(pos.y()) / h);

  const QVector3D forward = (target - eye).normalized();
  const QVector3D world_up(0.0f, 0.0f, 1.0f);
  QVector3D       right = QVector3D::crossProduct(forward, world_up);
  if (right.lengthSquared() < 1e-6f)
  {
    right = QVector3D(1.0f, 0.0f, 0.0f);
  }
  else
  {
    right.normalize();
  }
  const QVector3D up = QVector3D::crossProduct(right, forward).normalized();

  const float fov_rad = qDegreesToRadians(45.0f);
  const float tan_half_fov = std::tan(fov_rad * 0.5f);
  const float aspect = w / h;

  ray_origin = eye;
  ray_dir = (forward + right * (ndc_x * tan_half_fov * aspect) +
             up * (ndc_y * tan_half_fov))
                .normalized();
}

bool ray_sphere_intersect(const QVector3D &origin,
                          const QVector3D &dir,
                          const QVector3D &center,
                          float            radius,
                          float           &t_out)
{
  const QVector3D oc = origin - center;
  const float     b = QVector3D::dotProduct(oc, dir);
  const float     c = QVector3D::dotProduct(oc, oc) - radius * radius;
  const float     disc = b * b - c;
  if (disc < 0.0f)
  {
    return false;
  }

  const float sqrt_disc = std::sqrt(disc);
  float       t = -b - sqrt_disc;
  if (t < 0.0f)
  {
    t = -b + sqrt_disc;
  }
  if (t < 0.0f)
  {
    return false;
  }

  t_out = t;
  return true;
}

bool ray_plane_intersect(const QVector3D &origin,
                         const QVector3D &dir,
                         const QVector3D &plane_point,
                         const QVector3D &plane_normal,
                         QVector3D       &hit_out)
{
  const float denom = QVector3D::dotProduct(dir, plane_normal);
  if (std::abs(denom) < 1e-6f)
  {
    return false;
  }

  const float t = QVector3D::dotProduct(plane_point - origin, plane_normal) /
                  denom;
  if (t < 0.0f)
  {
    return false;
  }

  hit_out = origin + dir * t;
  return true;
}

float ray_segment_closest_point(const QVector3D &ray_origin,
                                const QVector3D &ray_dir,
                                const QVector3D &seg_a,
                                const QVector3D &seg_b,
                                float           &seg_u_out)
{
  const QVector3D u = ray_dir;
  const QVector3D v = seg_b - seg_a;
  const QVector3D w = ray_origin - seg_a;

  const float a = QVector3D::dotProduct(u, u);
  const float b = QVector3D::dotProduct(u, v);
  const float c = QVector3D::dotProduct(v, v);
  const float d = QVector3D::dotProduct(u, w);
  const float e = QVector3D::dotProduct(v, w);
  const float denom = a * c - b * b;

  float sc = 0.0f;
  float tc = 0.0f;

  if (denom < 1e-6f)
  {
    sc = 0.0f;
    tc = (b > c ? d / b : e / c);
  }
  else
  {
    sc = (b * e - c * d) / denom;
    tc = (a * e - b * d) / denom;
  }

  sc = std::max(sc, 0.0f);
  tc = std::clamp(tc, 0.0f, 1.0f);

  const QVector3D closest_ray = ray_origin + u * sc;
  const QVector3D closest_seg = seg_a + v * tc;
  seg_u_out = tc;
  return (closest_ray - closest_seg).length();
}

void append_sphere_mesh(std::vector<filament::math::float3> &positions,
                        std::vector<uint32_t>               &indices,
                        const terrain::Vec3                 &center,
                        float                                radius,
                        int                                  rings = 8,
                        int                                  sectors = 12)
{
  const uint32_t  base_idx = static_cast<uint32_t>(positions.size());
  constexpr float pi = 3.14159265358979323846f;

  for (int r = 0; r <= rings; ++r)
  {
    const float phi = pi * static_cast<float>(r) / static_cast<float>(rings);
    const float sin_phi = std::sin(phi);
    const float cos_phi = std::cos(phi);

    for (int s = 0; s <= sectors; ++s)
    {
      const float theta = 2.0f * pi * static_cast<float>(s) /
                          static_cast<float>(sectors);
      const float x = center.x + radius * sin_phi * std::cos(theta);
      const float y = center.y + radius * sin_phi * std::sin(theta);
      const float z = center.z + radius * cos_phi;
      positions.emplace_back(x, y, z);
    }
  }

  for (int r = 0; r < rings; ++r)
  {
    for (int s = 0; s < sectors; ++s)
    {
      const uint32_t i0 = base_idx +
                          static_cast<uint32_t>(r * (sectors + 1) + s);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + static_cast<uint32_t>(sectors + 1);
      const uint32_t i3 = i2 + 1;

      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);

      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }
}

void append_cylinder_mesh(std::vector<filament::math::float3> &positions,
                          std::vector<uint32_t>               &indices,
                          const terrain::Vec3                 &p0,
                          const terrain::Vec3                 &p1,
                          float                                radius,
                          int                                  sectors = 8)
{
  const QVector3D a(p0.x, p0.y, p0.z);
  const QVector3D b(p1.x, p1.y, p1.z);
  const QVector3D dir = b - a;
  if (dir.lengthSquared() < 1e-8f)
  {
    return;
  }

  const QVector3D axis = dir.normalized();
  const QVector3D ref = (std::abs(axis.z()) < 0.9f)
                            ? QVector3D(0.0f, 0.0f, 1.0f)
                            : QVector3D(1.0f, 0.0f, 0.0f);
  const QVector3D u = QVector3D::crossProduct(axis, ref).normalized();
  const QVector3D v = QVector3D::crossProduct(axis, u).normalized();

  const uint32_t  base_idx = static_cast<uint32_t>(positions.size());
  constexpr float pi = 3.14159265358979323846f;

  for (int s = 0; s <= sectors; ++s)
  {
    const float theta = 2.0f * pi * static_cast<float>(s) /
                        static_cast<float>(sectors);
    const QVector3D offset = (u * std::cos(theta) + v * std::sin(theta)) *
                             radius;
    const QVector3D va = a + offset;
    const QVector3D vb = b + offset;
    positions.emplace_back(va.x(), va.y(), va.z());
    positions.emplace_back(vb.x(), vb.y(), vb.z());
  }

  for (int s = 0; s < sectors; ++s)
  {
    const uint32_t i0 = base_idx + static_cast<uint32_t>(s * 2);
    const uint32_t i1 = i0 + 1;
    const uint32_t i2 = i0 + 2;
    const uint32_t i3 = i0 + 3;

    indices.push_back(i0);
    indices.push_back(i2);
    indices.push_back(i1);

    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i3);
  }
}

} // namespace

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && !impl_->paths.empty())
  {
    QVector3D ray_origin;
    QVector3D ray_dir;
    compute_camera_ray(event->pos(),
                       width(),
                       height(),
                       get_camera_pos(),
                       get_camera_target(),
                       ray_origin,
                       ray_dir);

    std::string best_path_id{};
    size_t      best_seg_idx = 0;
    float       best_seg_u = 0.5f;
    float       best_dist = std::numeric_limits<float>::max();

    for (const auto &[id, entry] : impl_->paths)
    {
      const auto &pts = entry.path.points;
      if (pts.size() < 2)
      {
        continue;
      }

      const float hit_threshold = std::max(entry.path.point_radius * 1.5f,
                                           2.0f);
      for (size_t i = 0; i + 1 < pts.size(); ++i)
      {
        const QVector3D a(pts[i].x, pts[i].y, pts[i].z);
        const QVector3D b(pts[i + 1].x, pts[i + 1].y, pts[i + 1].z);
        float           seg_u = 0.5f;
        const float     dist = ray_segment_closest_point(ray_origin,
                                                     ray_dir,
                                                     a,
                                                     b,
                                                     seg_u);
        if (dist < hit_threshold && dist < best_dist)
        {
          best_dist = dist;
          best_path_id = id;
          best_seg_idx = i;
          best_seg_u = seg_u;
        }
      }
    }

    if (!best_path_id.empty())
    {
      auto               &entry = impl_->paths[best_path_id];
      const auto         &a = entry.path.points[best_seg_idx];
      const auto         &b = entry.path.points[best_seg_idx + 1];
      const terrain::Vec3 new_pt{a.x + (b.x - a.x) * best_seg_u,
                                 a.y + (b.y - a.y) * best_seg_u,
                                 a.z + (b.z - a.z) * best_seg_u};

      const int new_idx = static_cast<int>(best_seg_idx + 1);
      entry.path.points.insert(entry.path.points.begin() + new_idx, new_pt);
      entry.gpu_dirty = true;

      impl_->selected_path_id = best_path_id;
      impl_->selected_point_index = new_idx;

      emit path_point_selected(best_path_id, new_idx);
      emit path_changed(best_path_id, entry.path.points);
      event->accept();
      return;
    }
  }

  QWidget::mouseDoubleClickEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (impl_->path_drag_mode != PathDragMode::NONE &&
      !impl_->selected_path_id.empty() && impl_->selected_point_index >= 0)
  {
    auto it = impl_->paths.find(impl_->selected_path_id);
    if (it != impl_->paths.end() &&
        impl_->selected_point_index <
            static_cast<int>(it->second.path.points.size()))
    {
      QVector3D ray_origin;
      QVector3D ray_dir;
      compute_camera_ray(event->pos(),
                         width(),
                         height(),
                         get_camera_pos(),
                         get_camera_target(),
                         ray_origin,
                         ray_dir);

      auto           &pt = it->second.path.points[impl_->selected_point_index];
      const QVector3D cur_pt(pt.x, pt.y, pt.z);
      QVector3D       hit;

      if (impl_->path_drag_mode == PathDragMode::XY_PLANE)
      {
        if (ray_plane_intersect(ray_origin,
                                ray_dir,
                                cur_pt,
                                QVector3D(0.0f, 0.0f, 1.0f),
                                hit))
        {
          pt.x = hit.x();
          pt.y = hit.y();
          it->second.gpu_dirty = true;
          emit path_changed(impl_->selected_path_id, it->second.path.points);
        }
      }
      else if (impl_->path_drag_mode == PathDragMode::Z_AXIS)
      {
        QVector3D cam_horiz = get_camera_target() - get_camera_pos();
        cam_horiz.setZ(0.0f);
        if (cam_horiz.lengthSquared() < 1e-6f)
        {
          cam_horiz = QVector3D(1.0f, 0.0f, 0.0f);
        }
        else
        {
          cam_horiz.normalize();
        }

        if (ray_plane_intersect(ray_origin, ray_dir, cur_pt, cam_horiz, hit))
        {
          pt.z = hit.z();
          it->second.gpu_dirty = true;
          emit path_changed(impl_->selected_path_id, it->second.path.points);
        }
      }

      last_mouse_pos_ = event->pos();
      event->accept();
      return;
    }
  }

  const QPoint delta = event->pos() - last_mouse_pos_;
  last_mouse_pos_ = event->pos();

  if (event->buttons() & Qt::LeftButton)
  {
    camera_yaw_ -= static_cast<float>(delta.x()) * 0.4f;
    camera_pitch_ += static_cast<float>(delta.y()) * 0.4f;
    camera_pitch_ = std::clamp(camera_pitch_, -89.0f, 89.0f);
    recompute_all_lods();
  }
  else if (event->buttons() & (Qt::RightButton | Qt::MiddleButton))
  {
    const float rad_yaw = qDegreesToRadians(camera_yaw_);
    const float rad_pitch = qDegreesToRadians(camera_pitch_);

    // Camera view-plane right and up vectors in 3D world space (Z-up)
    const QVector3D cam_right(-std::sin(rad_yaw), std::cos(rad_yaw), 0.0f);
    const QVector3D cam_up(-std::sin(rad_pitch) * std::cos(rad_yaw),
                           -std::sin(rad_pitch) * std::sin(rad_yaw),
                           std::cos(rad_pitch));

    const float pan_speed = camera_distance_ * 0.0014f;
    camera_target_ -= cam_right * (static_cast<float>(delta.x()) * pan_speed);
    camera_target_ += cam_up * (static_cast<float>(delta.y()) * pan_speed);
    recompute_all_lods();
  }

  QWidget::mouseMoveEvent(event);
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && !impl_->paths.empty())
  {
    QVector3D ray_origin;
    QVector3D ray_dir;
    compute_camera_ray(event->pos(),
                       width(),
                       height(),
                       get_camera_pos(),
                       get_camera_target(),
                       ray_origin,
                       ray_dir);

    // 1. Check Z-axis gizmo handle of the currently selected point first
    if (!impl_->selected_path_id.empty() && impl_->selected_point_index >= 0)
    {
      auto it = impl_->paths.find(impl_->selected_path_id);
      if (it != impl_->paths.end() &&
          impl_->selected_point_index <
              static_cast<int>(it->second.path.points.size()))
      {
        const auto &pt = it->second.path.points[impl_->selected_point_index];
        const float r = it->second.path.point_radius;
        const QVector3D z_handle_pos(pt.x, pt.y, pt.z + r * 3.5f);
        float           t_hit = 0.0f;
        if (ray_sphere_intersect(ray_origin,
                                 ray_dir,
                                 z_handle_pos,
                                 r * 1.1f,
                                 t_hit))
        {
          impl_->path_drag_mode = PathDragMode::Z_AXIS;
          last_mouse_pos_ = event->pos();
          event->accept();
          return;
        }
      }
    }

    // 2. Check all path control points
    std::string best_path_id{};
    int         best_pt_idx = -1;
    float       best_t = std::numeric_limits<float>::max();

    for (const auto &[id, entry] : impl_->paths)
    {
      const float hit_r = std::max(entry.path.point_radius * 1.25f, 1.5f);
      for (size_t i = 0; i < entry.path.points.size(); ++i)
      {
        const auto     &pt = entry.path.points[i];
        const QVector3D center(pt.x, pt.y, pt.z);
        float           t_hit = 0.0f;
        if (ray_sphere_intersect(ray_origin, ray_dir, center, hit_r, t_hit) &&
            t_hit < best_t)
        {
          best_t = t_hit;
          best_path_id = id;
          best_pt_idx = static_cast<int>(i);
        }
      }
    }

    if (!best_path_id.empty())
    {
      const std::string prev_id = impl_->selected_path_id;
      const int         prev_idx = impl_->selected_point_index;

      if (!prev_id.empty() && prev_id != best_path_id)
      {
        auto prev_it = impl_->paths.find(prev_id);
        if (prev_it != impl_->paths.end())
        {
          prev_it->second.gpu_dirty = true;
        }
      }

      impl_->selected_path_id = best_path_id;
      impl_->selected_point_index = best_pt_idx;
      impl_->paths[best_path_id].gpu_dirty = true;
      impl_->path_drag_mode = (event->modifiers() & Qt::ShiftModifier)
                                  ? PathDragMode::Z_AXIS
                                  : PathDragMode::XY_PLANE;

      if (prev_id != best_path_id || prev_idx != best_pt_idx)
      {
        emit path_point_selected(best_path_id, best_pt_idx);
      }

      last_mouse_pos_ = event->pos();
      event->accept();
      return;
    }
  }

  is_dragging_ = true;
  last_mouse_pos_ = event->pos();
  QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *event)
{
  if (impl_->path_drag_mode != PathDragMode::NONE)
  {
    impl_->path_drag_mode = PathDragMode::NONE;
    event->accept();
    return;
  }

  is_dragging_ = false;
  QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);
}

QPaintEngine *ViewportWidget::paintEngine() const
{
  return nullptr;
}

void ViewportWidget::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);

  if (filament_initialized_ && impl_->view)
  {
    impl_->view->setViewport(
        filament::Viewport(0, 0, std::max(1, width()), std::max(1, height())));
    update_camera();
  }
}

void ViewportWidget::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);

  if (!filament_initialized_)
  {
    // Avoid creating OpenGL/X11 swapchains in headless offscreen unit tests
    const QString platform = QGuiApplication::platformName();
    if (platform != "offscreen" && platform != "minimal")
    {
      init_filament();
    }
  }

  if (filament_initialized_ && render_timer_ && !render_timer_->isActive())
  {
    render_timer_->start(16);
  }
}

void ViewportWidget::hideEvent(QHideEvent *event)
{
  QWidget::hideEvent(event);

  if (render_timer_ && render_timer_->isActive())
  {
    render_timer_->stop();
  }
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
  const float delta = static_cast<float>(event->angleDelta().y());
  camera_distance_ *= (delta > 0.0f) ? 0.88f : 1.12f;
  camera_distance_ = std::clamp(camera_distance_, 10.0f, 15000.0f);
  recompute_all_lods();

  QWidget::wheelEvent(event);
}

// --- Internal Filament methods

void ViewportWidget::init_filament()
{
  if (filament_initialized_)
  {
    return;
  }

  filament::Engine::Config engine_config;
  engine_config.driverHandleArenaSizeMB = 32;
  engine_config.commandBufferSizeMB = 32;
  engine_config.perRenderPassArenaSizeMB = 32;

  impl_->engine = filament::Engine::Builder()
                      .backend(filament::backend::Backend::OPENGL)
                      .config(&engine_config)
                      .build();
  if (!impl_->engine)
  {
    Logger::log()->error("Failed to create Filament engine in ViewportWidget!");
    return;
  }

  void *native_window = reinterpret_cast<void *>(winId());
  impl_->swap_chain = impl_->engine->createSwapChain(native_window);
  impl_->renderer = impl_->engine->createRenderer();
  impl_->scene = impl_->engine->createScene();
  impl_->view = impl_->engine->createView();

  // Clear options
  filament::Renderer::ClearOptions clear_options;
  clear_options.clearColor = {static_cast<float>(background_color_.redF()),
                              static_cast<float>(background_color_.greenF()),
                              static_cast<float>(background_color_.blueF()),
                              1.0f};
  clear_options.clear = true;
  clear_options.discard = true;
  impl_->renderer->setClearOptions(clear_options);

  // View configuration
  impl_->view->setPostProcessingEnabled(true);
  impl_->view->setShadowType(filament::ShadowType::PCF);

  filament::View::BloomOptions bloom_options;
  bloom_options.enabled = true;
  bloom_options.strength = 0.08f;
  impl_->view->setBloomOptions(bloom_options);

  filament::View::SoftShadowOptions soft_shadow_options;
  soft_shadow_options.penumbraScale = 1.2f;
  soft_shadow_options.penumbraRatioScale = 1.2f;
  impl_->view->setSoftShadowOptions(soft_shadow_options);

  // Skybox fallback (used only when atmospheric sky dome is disabled)
  impl_->skybox = filament::Skybox::Builder()
                      .color({0.14f, 0.16f, 0.22f, 1.0f})
                      .showSun(true)
                      .build(*impl_->engine);
  impl_->scene->setSkybox(nullptr);

  // Camera
  impl_->camera_entity = utils::EntityManager::get().create();
  impl_->camera = impl_->engine->createCamera(impl_->camera_entity);
  impl_->view->setCamera(impl_->camera);
  impl_->view->setScene(impl_->scene);
  impl_->view->setViewport(
      filament::Viewport(0, 0, std::max(1, width()), std::max(1, height())));

  // Directional Sun Light with 4-cascade soft shadows
  impl_->sun_light_entity = utils::EntityManager::get().create();

  const float max_dom = std::max(domain_lx_, domain_ly_);
  const float cam_near = 5.0f;
  const float cam_far = std::max(4500.0f, max_dom * 4.5f);

  filament::LightManager::ShadowOptions shadow_options;
  shadow_options.mapSize = 4096;
  shadow_options.shadowCascades = 4;
  shadow_options.shadowNearHint = cam_near;
  shadow_options.shadowFar = max_dom * 2.5f;
  shadow_options.shadowFarHint = max_dom * 2.5f;
  shadow_options.normalBias = 0.25f;
  shadow_options.constantBias = 0.0005f;
  shadow_options.stable = true;
  shadow_options.screenSpaceContactShadows = false;

  filament::LightManager::ShadowCascades::computePracticalSplits(
      shadow_options.cascadeSplitPositions,
      shadow_options.shadowCascades,
      cam_near,
      cam_far,
      0.75f);

  filament::LightManager::Builder(filament::LightManager::Type::SUN)
      .color(
          filament::Color::toLinear<filament::ACCURATE>({1.0f, 0.98f, 0.92f}))
      .intensity(sun_settings_.intensity)
      .direction({-0.5f, -0.3f, -0.8f})
      .sunAngularRadius(sun_settings_.sun_angular_radius_deg)
      .sunHaloSize(10.0f)
      .sunHaloFalloff(80.0f)
      .castShadows(env_settings_.soft_shadows_enabled)
      .shadowOptions(shadow_options)
      .build(*impl_->engine, impl_->sun_light_entity);
  impl_->scene->addEntity(impl_->sun_light_entity);

  // Compile Materials
  impl_->terrain_unlit_mat = filament::Material::Builder()
                                 .package(MATERIAL_TERRAIN_UNLIT_PKG,
                                          sizeof(MATERIAL_TERRAIN_UNLIT_PKG))
                                 .build(*impl_->engine);

  impl_->terrain_lit_mat = filament::Material::Builder()
                               .package(MATERIAL_TERRAIN_LIT_PKG,
                                        sizeof(MATERIAL_TERRAIN_LIT_PKG))
                               .build(*impl_->engine);

  impl_->instanced_obj_mat = filament::Material::Builder()
                                 .package(MATERIAL_INSTANCED_OBJECT_PKG,
                                          sizeof(MATERIAL_INSTANCED_OBJECT_PKG))
                                 .build(*impl_->engine);

  impl_->sky_material = filament::Material::Builder()
                            .package(MATERIAL_SKY_ATMOSPHERE_PKG,
                                     sizeof(MATERIAL_SKY_ATMOSPHERE_PKG))
                            .build(*impl_->engine);

  impl_->ground_plane_mat = filament::Material::Builder()
                                .package(MATERIAL_GROUND_PLANE_PKG,
                                         sizeof(MATERIAL_GROUND_PLANE_PKG))
                                .build(*impl_->engine);

  impl_->water_mat = filament::Material::Builder()
                         .package(MATERIAL_WATER_LIT_PKG,
                                  sizeof(MATERIAL_WATER_LIT_PKG))
                         .build(*impl_->engine);

  // Create fallback 1x1 white texture
  impl_->fallback_white_tex = filament::Texture::Builder()
                                  .width(1)
                                  .height(1)
                                  .levels(1)
                                  .sampler(
                                      filament::Texture::Sampler::SAMPLER_2D)
                                  .format(
                                      filament::Texture::InternalFormat::RGB8)
                                  .build(*impl_->engine);

  auto *white_px = new std::vector<uint8_t>{255, 255, 255};
  filament::Texture::PixelBufferDescriptor white_buf(
      white_px->data(),
      white_px->size(),
      filament::Texture::Format::RGB,
      filament::Texture::Type::UBYTE,
      [](void * /*buffer*/, size_t /*size*/, void *user)
      { delete static_cast<std::vector<uint8_t> *>(user); },
      white_px);
  impl_->fallback_white_tex->setImage(*impl_->engine, 0, std::move(white_buf));

  // Build Sky Dome Sphere Mesh (radius = 9500)
  {
    const uint32_t lat_steps = 24;
    const uint32_t lon_steps = 40;
    const float    sky_radius = 6000.0f;
    const float    pi = std::numbers::pi_v<float>;

    std::vector<filament::math::float3> sky_verts;
    std::vector<uint32_t>               sky_indices;

    for (uint32_t lat = 0; lat <= lat_steps; ++lat)
    {
      const float theta = static_cast<float>(lat) /
                          static_cast<float>(lat_steps) * pi;
      const float sin_t = std::sin(theta);
      const float cos_t = std::cos(theta);

      for (uint32_t lon = 0; lon <= lon_steps; ++lon)
      {
        const float phi = static_cast<float>(lon) /
                          static_cast<float>(lon_steps) * (2.0f * pi);
        sky_verts.push_back({sky_radius * sin_t * std::cos(phi),
                             sky_radius * sin_t * std::sin(phi),
                             sky_radius * cos_t});
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

        sky_indices.push_back(i0);
        sky_indices.push_back(i1);
        sky_indices.push_back(i2);

        sky_indices.push_back(i1);
        sky_indices.push_back(i3);
        sky_indices.push_back(i2);
      }
    }

    impl_->sky_dome_index_count = static_cast<uint32_t>(sky_indices.size());
    impl_->sky_dome_vb = filament::VertexBuffer::Builder()
                             .vertexCount(
                                 static_cast<uint32_t>(sky_verts.size()))
                             .bufferCount(1)
                             .attribute(
                                 filament::VertexAttribute::POSITION,
                                 0,
                                 filament::VertexBuffer::AttributeType::FLOAT3,
                                 0,
                                 sizeof(filament::math::float3))
                             .build(*impl_->engine);

    auto *sv_copy = new std::vector<filament::math::float3>(
        std::move(sky_verts));
    impl_->sky_dome_vb->setBufferAt(
        *impl_->engine,
        0,
        filament::VertexBuffer::BufferDescriptor(
            sv_copy->data(),
            sv_copy->size() * sizeof(filament::math::float3),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<filament::math::float3> *>(u); },
            sv_copy));

    impl_->sky_dome_ib = filament::IndexBuffer::Builder()
                             .indexCount(impl_->sky_dome_index_count)
                             .bufferType(filament::IndexBuffer::IndexType::UINT)
                             .build(*impl_->engine);

    auto *si_copy = new std::vector<uint32_t>(std::move(sky_indices));
    impl_->sky_dome_ib->setBuffer(
        *impl_->engine,
        filament::IndexBuffer::BufferDescriptor(
            si_copy->data(),
            si_copy->size() * sizeof(uint32_t),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<uint32_t> *>(u); },
            si_copy));

    impl_->sky_dome_entity = utils::EntityManager::get().create();
    impl_->sky_mat_instance = impl_->sky_material->createInstance();

    const float gz = env_settings_.ground_plane_elevation;
    filament::RenderableManager::Builder(1)
        .boundingBox({{-domain_lx_ * 2.0f, -domain_ly_ * 2.0f, gz - 5.0f},
                      {domain_lx_ * 2.0f, domain_ly_ * 2.0f, gz + 5.0f}})
        .material(0, impl_->sky_mat_instance)
        .geometry(0,
                  filament::RenderableManager::PrimitiveType::TRIANGLES,
                  impl_->sky_dome_vb,
                  impl_->sky_dome_ib,
                  0,
                  impl_->sky_dome_index_count)
        .channel(0)
        .priority(0)
        .castShadows(false)
        .receiveShadows(false)
        .fog(false)
        .culling(false)
        .build(*impl_->engine, impl_->sky_dome_entity);
  }

  // Build Optional Infinite Ground Plane Mesh (large quad centered on camera)
  {
    const float                         extent = 50000.0f;
    std::vector<filament::math::float3> gp_verts = {{-extent, -extent, 0.0f},
                                                    {extent, -extent, 0.0f},
                                                    {extent, extent, 0.0f},
                                                    {-extent, extent, 0.0f}};

    filament::math::mat3f tbn;
    tbn[0] = filament::math::float3(1.0f, 0.0f, 0.0f);
    tbn[1] = filament::math::float3(0.0f, 1.0f, 0.0f);
    tbn[2] = filament::math::float3(0.0f, 0.0f, 1.0f);
    const filament::math::quatf flat_tangent =
        filament::math::mat3f::packTangentFrame(tbn);
    std::vector<filament::math::quatf> gp_tangents(4, flat_tangent);

    std::vector<uint32_t> gp_indices = {0, 1, 2, 0, 2, 3};

    impl_->ground_plane_vb =
        filament::VertexBuffer::Builder()
            .vertexCount(4)
            .bufferCount(2)
            .attribute(filament::VertexAttribute::POSITION,
                       0,
                       filament::VertexBuffer::AttributeType::FLOAT3,
                       0,
                       sizeof(filament::math::float3))
            .attribute(filament::VertexAttribute::TANGENTS,
                       1,
                       filament::VertexBuffer::AttributeType::FLOAT4,
                       0,
                       sizeof(filament::math::quatf))
            .build(*impl_->engine);

    auto *gp_v_copy = new std::vector<filament::math::float3>(
        std::move(gp_verts));
    impl_->ground_plane_vb->setBufferAt(
        *impl_->engine,
        0,
        filament::VertexBuffer::BufferDescriptor(
            gp_v_copy->data(),
            gp_v_copy->size() * sizeof(filament::math::float3),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<filament::math::float3> *>(u); },
            gp_v_copy));

    auto *gp_t_copy = new std::vector<filament::math::quatf>(
        std::move(gp_tangents));
    impl_->ground_plane_vb->setBufferAt(
        *impl_->engine,
        1,
        filament::VertexBuffer::BufferDescriptor(
            gp_t_copy->data(),
            gp_t_copy->size() * sizeof(filament::math::quatf),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<filament::math::quatf> *>(u); },
            gp_t_copy));

    impl_->ground_plane_ib = filament::IndexBuffer::Builder()
                                 .indexCount(6)
                                 .bufferType(
                                     filament::IndexBuffer::IndexType::UINT)
                                 .build(*impl_->engine);

    auto *gp_i_copy = new std::vector<uint32_t>(std::move(gp_indices));
    impl_->ground_plane_ib->setBuffer(
        *impl_->engine,
        filament::IndexBuffer::BufferDescriptor(
            gp_i_copy->data(),
            gp_i_copy->size() * sizeof(uint32_t),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<uint32_t> *>(u); },
            gp_i_copy));

    impl_->ground_plane_entity = utils::EntityManager::get().create();
    impl_->ground_plane_mat_instance = impl_->ground_plane_mat
                                           ->createInstance();

    filament::RenderableManager::Builder(1)
        .boundingBox({{-100000.0f, -100000.0f, -10000.0f},
                      {100000.0f, 100000.0f, 10000.0f}})
        .material(0, impl_->ground_plane_mat_instance)
        .geometry(0,
                  filament::RenderableManager::PrimitiveType::TRIANGLES,
                  impl_->ground_plane_vb,
                  impl_->ground_plane_ib,
                  0,
                  6)
        .castShadows(false)
        .receiveShadows(true)
        .culling(false)
        .build(*impl_->engine, impl_->ground_plane_entity);
  }

  update_shared_grid_mesh();

  filament_initialized_ = true;

  update_camera();
  update_environment_and_sun();
  recompute_all_lods();
  update_instanced_renderables();

  render_timer_->start(16);
}

void ViewportWidget::recompute_all_lods()
{
  const QVector3D                eye = get_camera_pos();
  terrain::CDLODSelectionContext ctx;
  ctx.camera_pos = terrain::Vec3{eye.x(), eye.y(), eye.z()};
  ctx.morph_ratio = 0.66f;

  float base_dist = 75.0f * lod_dist_scale_;
  for (uint32_t i = 0; i < max_lod_levels_; ++i)
  {
    ctx.lod_distances.push_back(base_dist);
    base_dist *= 2.0f;
  }

  size_t total_nodes = 0;
  for (auto &[id, entry] : impl_->terrains)
  {
    if (entry.patch)
    {
      entry.lod_instances = impl_->quadtree.select_lod(*entry.patch, ctx);
      total_nodes += entry.lod_instances.size();
    }
  }

  impl_->active_cdlod_nodes = total_nodes;

  if (filament_initialized_)
  {
    update_camera();
    update_sky_dome();
    update_ground_plane();
    update_terrain_renderables();
  }

  emit camera_moved(eye.x(), eye.y(), eye.z());
  emit lod_selection_updated(total_nodes);
}

void ViewportWidget::render_frame()
{
  if (!filament_initialized_ || !impl_->renderer || !impl_->swap_chain ||
      !impl_->view)
  {
    return;
  }

  // Update dirty interactive path meshes
  if (impl_->engine && impl_->scene && impl_->instanced_obj_mat)
  {
    filament::TextureSampler nearest_sampler(
        filament::TextureSampler::MinFilter::NEAREST,
        filament::TextureSampler::MagFilter::NEAREST,
        filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

    for (auto &[id, entry] : impl_->paths)
    {
      if (!entry.gpu_dirty)
      {
        continue;
      }

      impl_->destroy_path_gpu(entry);

      const auto &pts = entry.path.points;
      if (pts.empty())
      {
        entry.gpu_dirty = false;
        continue;
      }

      std::vector<filament::math::float3> positions;
      std::vector<filament::math::float4> colors;
      std::vector<uint32_t>               indices;

      const filament::math::float4 line_col(entry.path.line_color.x,
                                            entry.path.line_color.y,
                                            entry.path.line_color.z,
                                            1.0f);
      const filament::math::float4 pt_col(entry.path.point_color.x,
                                          entry.path.point_color.y,
                                          entry.path.point_color.z,
                                          1.0f);
      const filament::math::float4 sel_col(entry.path.selected_color.x,
                                           entry.path.selected_color.y,
                                           entry.path.selected_color.z,
                                           1.0f);

      for (size_t i = 0; i + 1 < pts.size(); ++i)
      {
        append_cylinder_mesh(positions,
                             indices,
                             pts[i],
                             pts[i + 1],
                             entry.path.line_width);
        colors.resize(positions.size(), line_col);
      }

      for (size_t i = 0; i < pts.size(); ++i)
      {
        const bool  is_sel = (id == impl_->selected_path_id &&
                             static_cast<int>(i) ==
                                 impl_->selected_point_index);
        const float r = is_sel ? entry.path.point_radius * 1.25f
                               : entry.path.point_radius;
        append_sphere_mesh(positions, indices, pts[i], r);
        colors.resize(positions.size(), is_sel ? sel_col : pt_col);

        if (is_sel)
        {
          const terrain::Vec3 z_top{pts[i].x,
                                    pts[i].y,
                                    pts[i].z + entry.path.point_radius * 3.5f};
          append_cylinder_mesh(positions,
                               indices,
                               pts[i],
                               z_top,
                               entry.path.line_width * 0.8f);
          append_sphere_mesh(positions,
                             indices,
                             z_top,
                             entry.path.point_radius * 0.85f);
          colors.resize(positions.size(),
                        filament::math::float4(0.2f, 1.0f, 0.3f, 1.0f));
        }
      }

      if (positions.empty() || indices.empty())
      {
        entry.gpu_dirty = false;
        continue;
      }

      auto *pos_copy = new std::vector<filament::math::float3>(
          std::move(positions));
      auto *tan_copy = new std::vector<filament::math::quatf>(
          pos_copy->size(),
          filament::math::quatf(0.0f, 0.0f, 0.0f, 1.0f));
      auto *col_copy = new std::vector<filament::math::float4>(
          std::move(colors));
      auto *idx_copy = new std::vector<uint32_t>(std::move(indices));
      entry.index_count = static_cast<uint32_t>(idx_copy->size());

      entry.vb = filament::VertexBuffer::Builder()
                     .vertexCount(static_cast<uint32_t>(pos_copy->size()))
                     .bufferCount(3)
                     .attribute(filament::VertexAttribute::POSITION,
                                0,
                                filament::VertexBuffer::AttributeType::FLOAT3,
                                0,
                                sizeof(filament::math::float3))
                     .attribute(filament::VertexAttribute::TANGENTS,
                                1,
                                filament::VertexBuffer::AttributeType::FLOAT4,
                                0,
                                sizeof(filament::math::quatf))
                     .attribute(filament::VertexAttribute::COLOR,
                                2,
                                filament::VertexBuffer::AttributeType::FLOAT4,
                                0,
                                sizeof(filament::math::float4))
                     .build(*impl_->engine);

      entry.vb->setBufferAt(
          *impl_->engine,
          0,
          filament::VertexBuffer::BufferDescriptor(
              pos_copy->data(),
              pos_copy->size() * sizeof(filament::math::float3),
              [](void *, size_t, void *user)
              {
                delete static_cast<std::vector<filament::math::float3> *>(user);
              },
              pos_copy));

      entry.vb->setBufferAt(
          *impl_->engine,
          1,
          filament::VertexBuffer::BufferDescriptor(
              tan_copy->data(),
              tan_copy->size() * sizeof(filament::math::quatf),
              [](void *, size_t, void *user)
              {
                delete static_cast<std::vector<filament::math::quatf> *>(user);
              },
              tan_copy));

      entry.vb->setBufferAt(
          *impl_->engine,
          2,
          filament::VertexBuffer::BufferDescriptor(
              col_copy->data(),
              col_copy->size() * sizeof(filament::math::float4),
              [](void *, size_t, void *user)
              {
                delete static_cast<std::vector<filament::math::float4> *>(user);
              },
              col_copy));

      entry.ib = filament::IndexBuffer::Builder()
                     .indexCount(entry.index_count)
                     .bufferType(filament::IndexBuffer::IndexType::UINT)
                     .build(*impl_->engine);

      entry.ib->setBuffer(
          *impl_->engine,
          filament::IndexBuffer::BufferDescriptor(
              idx_copy->data(),
              idx_copy->size() * sizeof(uint32_t),
              [](void *, size_t, void *user)
              { delete static_cast<std::vector<uint32_t> *>(user); },
              idx_copy));

      // Identity single-instance texture (pos = 0, scale = 1, rot = 0)
      auto *tex_data = new std::vector<filament::math::float4>{
          {0.0f, 0.0f, 0.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f},
      };

      entry.instance_data_tex =
          filament::Texture::Builder()
              .width(2)
              .height(1)
              .levels(1)
              .sampler(filament::Texture::Sampler::SAMPLER_2D)
              .format(filament::Texture::InternalFormat::RGBA32F)
              .build(*impl_->engine);

      filament::Texture::PixelBufferDescriptor pbd(
          tex_data->data(),
          tex_data->size() * sizeof(filament::math::float4),
          filament::Texture::Format::RGBA,
          filament::Texture::Type::FLOAT,
          [](void *, size_t, void *user)
          { delete static_cast<std::vector<filament::math::float4> *>(user); },
          tex_data);
      entry.instance_data_tex->setImage(*impl_->engine, 0, std::move(pbd));

      entry.mat_instance = impl_->instanced_obj_mat->createInstance();
      entry.mat_instance->setParameter("instanceDataMap",
                                       entry.instance_data_tex,
                                       nearest_sampler);
      entry.mat_instance->setParameter("instanceTexWidth", 2u);
      entry.mat_instance->setParameter(
          "baseColorTint",
          filament::math::float3(1.0f, 1.0f, 1.0f));
      entry.mat_instance->setParameter("roughness", 0.4f);
      entry.mat_instance->setParameter("metallic", 0.0f);

      entry.entity = utils::EntityManager::get().create();

      filament::RenderableManager::Builder(1)
          .boundingBox({{-1e6f, -1e6f, -1e6f}, {1e6f, 1e6f, 1e6f}})
          .material(0, entry.mat_instance)
          .geometry(0,
                    filament::RenderableManager::PrimitiveType::TRIANGLES,
                    entry.vb,
                    entry.ib,
                    0,
                    entry.index_count)
          .instances(1)
          .culling(false)
          .castShadows(false)
          .receiveShadows(false)
          .build(*impl_->engine, entry.entity);

      impl_->scene->addEntity(entry.entity);
      entry.in_scene = true;
      entry.gpu_dirty = false;
    }
  }

  const auto   now = std::chrono::steady_clock::now();
  const double dt_ms =
      std::chrono::duration<double, std::milli>(now - last_frame_time_).count();
  last_frame_time_ = now;

  if (dt_ms > 0.1)
  {
    const float instant_fps = static_cast<float>(1000.0 / dt_ms);
    current_fps_ = (current_fps_ < 1.0f)
                       ? instant_fps
                       : (0.9f * current_fps_ + 0.1f * instant_fps);
    current_frame_time_ms_ = static_cast<float>(dt_ms);
    emit fps_updated(current_fps_, current_frame_time_ms_);
  }

  if (impl_->renderer->beginFrame(impl_->swap_chain))
  {
    impl_->renderer->render(impl_->view);
    impl_->renderer->endFrame();
  }
}

void ViewportWidget::update_camera()
{
  if (!impl_->camera)
  {
    return;
  }

  const QVector3D eye = get_camera_pos();
  const float     aspect = static_cast<float>(std::max(1, width())) /
                       static_cast<float>(std::max(1, height()));

  const float cam_far =
      std::max(4500.0f, std::max(domain_lx_, domain_ly_) * 4.5f);

  impl_->camera->setProjection(camera_fov_deg_,
                               aspect,
                               5.0,
                               cam_far,
                               filament::Camera::Fov::VERTICAL);
  impl_->camera->lookAt(filament::math::float3(eye.x(), eye.y(), eye.z()),
                        filament::math::float3(camera_target_.x(),
                                               camera_target_.y(),
                                               camera_target_.z()),
                        filament::math::float3(0.0f, 0.0f, 1.0f));
}

void ViewportWidget::update_environment_and_sun()
{
  if (!filament_initialized_ || !impl_->engine || !impl_->view)
  {
    return;
  }

  const bool pbr = env_settings_.pbr_enabled;

  // 1. Compute Sun Direction and Color
  const terrain::Vec3 sun_ray_dir = scene::compute_sun_direction(
      sun_settings_.azimuth_deg,
      sun_settings_.elevation_deg);

  const terrain::Vec3 sun_color = sun_settings_.auto_color_temperature
                                      ? scene::compute_sun_color_from_elevation(
                                            sun_settings_.elevation_deg)
                                      : sun_settings_.base_color;

  const float intensity_mult = sun_settings_.auto_color_temperature
                                   ? scene::compute_sun_intensity_multiplier(
                                         sun_settings_.elevation_deg)
                                   : 1.0f;

  const bool cast_shadows = pbr && env_settings_.soft_shadows_enabled;

  impl_->view->setShadowingEnabled(cast_shadows);

  if (impl_->sun_light_entity)
  {
    auto &lm = impl_->engine->getLightManager();
    auto  li = lm.getInstance(impl_->sun_light_entity);
    if (li)
    {
      lm.setDirection(
          li,
          filament::math::float3(sun_ray_dir.x, sun_ray_dir.y, sun_ray_dir.z));
      lm.setColor(li,
                  filament::Color::toLinear<filament::ACCURATE>(
                      {sun_color.x, sun_color.y, sun_color.z}));
      lm.setIntensity(li, sun_settings_.intensity * intensity_mult);
      lm.setShadowCaster(li, cast_shadows);
      lm.setSunAngularRadius(li,
                             sun_settings_.render_sun_disk
                                 ? sun_settings_.sun_angular_radius_deg
                                 : 0.0f);
    }
  }

  // 2. Dynamic Indirect Light (Hemispherical Sky Irradiance)
  if (impl_->indirect_light)
  {
    impl_->scene->setIndirectLight(nullptr);
    impl_->engine->destroy(impl_->indirect_light);
    impl_->indirect_light = nullptr;
  }

  const float day_factor = std::clamp((sun_settings_.elevation_deg + 8.0f) /
                                          60.0f,
                                      0.08f,
                                      1.0f);

  // 2-band (4-coefficient) spherical harmonics in Z-up world space:
  // Band 0: omnidirectional sky ambient
  // Band 1 (+Z up): bright sky dome above (+Z) and darker ground bounce below
  // (-Z)
  const filament::math::float3 sky_irradiance{
      (0.32f + 0.14f * sun_color.x) * day_factor,
      (0.40f + 0.10f * sun_color.y) * day_factor,
      0.54f * day_factor};
  const filament::math::float3 z_hemisphere{0.10f * day_factor,
                                            0.14f * day_factor,
                                            0.22f * day_factor};

  filament::math::float3 ambient_sh[4] = {sky_irradiance,
                                          {0.0f, 0.0f, 0.0f},
                                          z_hemisphere,
                                          {0.0f, 0.0f, 0.0f}};

  impl_->indirect_light = filament::IndirectLight::Builder()
                              .irradiance(2, ambient_sh)
                              .intensity(pbr ? 28000.0f : 35000.0f)
                              .build(*impl_->engine);
  impl_->scene->setIndirectLight(impl_->indirect_light);

  // 3. Ambient Occlusion (GTAO)
  filament::View::AmbientOcclusionOptions ao_options =
      impl_->view->getAmbientOcclusionOptions();
  ao_options.enabled = pbr && env_settings_.ambient_occlusion_enabled;
  ao_options.aoType =
      filament::View::AmbientOcclusionOptions::AmbientOcclusionType::GTAO;
  ao_options.quality = filament::View::QualityLevel::HIGH;
  ao_options.lowPassFilter = filament::View::QualityLevel::HIGH;
  ao_options.upsampling = filament::View::QualityLevel::HIGH;
  ao_options.bentNormals = true;
  impl_->view->setAmbientOcclusionOptions(ao_options);

  // 4. Atmospheric / Height Fog with Sun In-Scattering
  // Align Filament's Y-up fog volume with our Z-up world coordinate system
  const utils::Entity fog_entity = impl_->view->getFogEntity();
  if (fog_entity)
  {
    auto &tm = impl_->engine->getTransformManager();
    if (!tm.hasComponent(fog_entity))
    {
      tm.create(fog_entity);
    }
    auto fog_ti = tm.getInstance(fog_entity);

    // Maps world Z (elevation) into fog local Y (altitude):
    // local_x = world_x, local_y = world_z, local_z = -world_y
    filament::math::mat4f z_up_to_y_up(
        filament::math::float4(1.0f, 0.0f, 0.0f, 0.0f),
        filament::math::float4(0.0f, 0.0f, 1.0f, 0.0f),
        filament::math::float4(0.0f, -1.0f, 0.0f, 0.0f),
        filament::math::float4(0.0f, 0.0f, 0.0f, 1.0f));
    tm.setTransform(fog_ti, z_up_to_y_up);
  }

  filament::View::FogOptions fog_options;
  fog_options.enabled = pbr && env_settings_.fog_enabled;
  fog_options.distance = env_settings_.fog_start;
  fog_options.cutOffDistance = std::numeric_limits<float>::infinity();
  fog_options.density = env_settings_.fog_density;
  fog_options.height = env_settings_.fog_height;
  fog_options.heightFalloff = env_settings_.fog_height_falloff;
  fog_options.maximumOpacity = 1.0f;

  // Match fog tint to sky_atmosphere.mat horizonCol for seamless horizon
  // blending
  const float sun_elev_sin = std::sin(
      qDegreesToRadians(sun_settings_.elevation_deg));
  const float sky_day = std::clamp((sun_elev_sin + 0.12f) / 0.65f, 0.0f, 1.0f);
  const float sky_sunset = std::clamp(1.0f -
                                          std::abs(sun_elev_sin - 0.06f) * 4.5f,
                                      0.0f,
                                      1.0f);
  const float h_blend = std::clamp(sky_day + sky_sunset * 0.7f, 0.0f, 1.0f);
  const float s_blend = sky_sunset * 0.85f;

  const float day_sunset_r = 0.45f * (1.0f - s_blend) + 0.95f * s_blend;
  const float day_sunset_g = 0.68f * (1.0f - s_blend) + 0.42f * s_blend;
  const float day_sunset_b = 0.92f * (1.0f - s_blend) + 0.16f * s_blend;

  const float fog_r = 0.03f * (1.0f - h_blend) + day_sunset_r * h_blend;
  const float fog_g = 0.04f * (1.0f - h_blend) + day_sunset_g * h_blend;
  const float fog_b = 0.08f * (1.0f - h_blend) + day_sunset_b * h_blend;
  fog_options.color = {fog_r, fog_g, fog_b};

  // Disable Filament's built-in fog in-scattering pow(max(dot, 0.0), size)
  // because GLSL pow(0.0, size) evaluates to NaN when looking away from the
  // sun; Mie forward scattering is already computed in sky_atmosphere.mat.
  fog_options.inScatteringSize = -1.0f;

  impl_->view->setFogOptions(fog_options);

  // 5. Update Atmospheric Sky Dome & Optional Ground Plane
  update_sky_dome();
  update_ground_plane();
}

void ViewportWidget::update_ground_plane()
{
  if (!filament_initialized_ || !impl_->engine ||
      !impl_->ground_plane_mat_instance)
  {
    return;
  }

  if (env_settings_.ground_plane_enabled)
  {
    const QVector3D eye = get_camera_pos();
    impl_->ground_plane_mat_instance->setParameter(
        "cameraPos",
        filament::math::float3(eye.x(), eye.y(), eye.z()));
    impl_->ground_plane_mat_instance->setParameter(
        "groundElevation",
        env_settings_.ground_plane_elevation);
    impl_->ground_plane_mat_instance->setParameter(
        "groundColor",
        filament::math::float3(env_settings_.ground_plane_color.x,
                               env_settings_.ground_plane_color.y,
                               env_settings_.ground_plane_color.z));
    impl_->ground_plane_mat_instance->setParameter(
        "groundRoughness",
        env_settings_.ground_plane_roughness);

    if (!impl_->ground_plane_in_scene)
    {
      impl_->scene->addEntity(impl_->ground_plane_entity);
      impl_->ground_plane_in_scene = true;
    }
  }
  else
  {
    if (impl_->ground_plane_in_scene)
    {
      impl_->scene->remove(impl_->ground_plane_entity);
      impl_->ground_plane_in_scene = false;
    }
  }
}

void ViewportWidget::update_instanced_renderables()
{
  if (!filament_initialized_ || !impl_->engine || !impl_->instanced_obj_mat)
  {
    return;
  }

  filament::TextureSampler nearest_sampler(
      filament::TextureSampler::MinFilter::NEAREST,
      filament::TextureSampler::MagFilter::NEAREST,
      filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

  for (auto &[id, entry] : impl_->instanced_groups)
  {
    if (!entry.gpu_dirty)
    {
      continue;
    }

    impl_->destroy_instanced_group_gpu(entry);

    if (entry.group.instances.empty())
    {
      entry.gpu_dirty = false;
      continue;
    }

    // Resolve mesh geometry
    scene::MeshGeometryData mesh;
    switch (entry.group.mesh_type)
    {
    case scene::ScatterMeshType::ROCK:
      mesh = scene::ScatterMeshFactory::create_rock_mesh();
      break;
    case scene::ScatterMeshType::PINE_TREE:
      mesh = scene::ScatterMeshFactory::create_pine_tree_mesh();
      break;
    case scene::ScatterMeshType::DECIDUOUS_TREE:
      mesh = scene::ScatterMeshFactory::create_deciduous_tree_mesh();
      break;
    case scene::ScatterMeshType::CUSTOM: mesh = entry.group.custom_mesh; break;
    }

    if (mesh.positions.empty() || mesh.indices.empty())
    {
      entry.gpu_dirty = false;
      continue;
    }

    const size_t                        v_count = mesh.positions.size();
    std::vector<filament::math::float3> positions(v_count);
    std::vector<filament::math::quatf>  tangents(v_count);
    std::vector<filament::math::float4> colors(v_count);

    for (size_t i = 0; i < v_count; ++i)
    {
      positions[i] = {mesh.positions[i].x,
                      mesh.positions[i].y,
                      mesh.positions[i].z};

      const terrain::Vec3 n = (i < mesh.normals.size())
                                  ? mesh.normals[i]
                                  : terrain::Vec3{0.0f, 0.0f, 1.0f};
      tangents[i] = pack_normal_to_tangent_frame(n);

      const terrain::Vec3 c = (i < mesh.colors.size())
                                  ? mesh.colors[i]
                                  : terrain::Vec3{1.0f, 1.0f, 1.0f};
      colors[i] = {c.x, c.y, c.z, 1.0f};
    }

    entry.vb = filament::VertexBuffer::Builder()
                   .vertexCount(static_cast<uint32_t>(v_count))
                   .bufferCount(3)
                   .attribute(filament::VertexAttribute::POSITION,
                              0,
                              filament::VertexBuffer::AttributeType::FLOAT3,
                              0,
                              sizeof(filament::math::float3))
                   .attribute(filament::VertexAttribute::TANGENTS,
                              1,
                              filament::VertexBuffer::AttributeType::FLOAT4,
                              0,
                              sizeof(filament::math::quatf))
                   .attribute(filament::VertexAttribute::COLOR,
                              2,
                              filament::VertexBuffer::AttributeType::FLOAT4,
                              0,
                              sizeof(filament::math::float4))
                   .build(*impl_->engine);

    auto *pos_copy = new std::vector<filament::math::float3>(
        std::move(positions));
    entry.vb->setBufferAt(
        *impl_->engine,
        0,
        filament::VertexBuffer::BufferDescriptor(
            pos_copy->data(),
            pos_copy->size() * sizeof(filament::math::float3),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<filament::math::float3> *>(u); },
            pos_copy));

    auto *tan_copy = new std::vector<filament::math::quatf>(
        std::move(tangents));
    entry.vb->setBufferAt(
        *impl_->engine,
        1,
        filament::VertexBuffer::BufferDescriptor(
            tan_copy->data(),
            tan_copy->size() * sizeof(filament::math::quatf),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<filament::math::quatf> *>(u); },
            tan_copy));

    auto *col_copy = new std::vector<filament::math::float4>(std::move(colors));
    entry.vb->setBufferAt(
        *impl_->engine,
        2,
        filament::VertexBuffer::BufferDescriptor(
            col_copy->data(),
            col_copy->size() * sizeof(filament::math::float4),
            [](void * /*b*/, size_t /*s*/, void *u)
            { delete static_cast<std::vector<filament::math::float4> *>(u); },
            col_copy));

    entry.index_count = static_cast<uint32_t>(mesh.indices.size());
    entry.ib = filament::IndexBuffer::Builder()
                   .indexCount(entry.index_count)
                   .bufferType(filament::IndexBuffer::IndexType::UINT)
                   .build(*impl_->engine);

    auto *idx_copy = new std::vector<uint32_t>(std::move(mesh.indices));
    entry.ib->setBuffer(*impl_->engine,
                        filament::IndexBuffer::BufferDescriptor(
                            idx_copy->data(),
                            idx_copy->size() * sizeof(uint32_t),
                            [](void * /*b*/, size_t /*s*/, void *u)
                            { delete static_cast<std::vector<uint32_t> *>(u); },
                            idx_copy));

    // Split instances into batches of up to MAX_INSTANCES_PER_BATCH (16384)
    const size_t total_inst = entry.group.instances.size();
    const size_t batch_count = (total_inst + MAX_INSTANCES_PER_BATCH - 1) /
                               MAX_INSTANCES_PER_BATCH;

    entry.batches.resize(batch_count);

    for (size_t b = 0; b < batch_count; ++b)
    {
      const size_t start_idx = b * MAX_INSTANCES_PER_BATCH;
      const size_t count = std::min(MAX_INSTANCES_PER_BATCH,
                                    total_inst - start_idx);

      const uint32_t tex_w = static_cast<uint32_t>(INSTANCE_TEX_WIDTH * 2);
      const uint32_t tex_h = static_cast<uint32_t>(
          (count + INSTANCE_TEX_WIDTH - 1) / INSTANCE_TEX_WIDTH);

      auto *tex_data = new std::vector<filament::math::float4>(
          static_cast<size_t>(tex_w) * tex_h,
          filament::math::float4(0.0f));

      float min_x = std::numeric_limits<float>::max();
      float min_y = std::numeric_limits<float>::max();
      float min_z = std::numeric_limits<float>::max();
      float max_x = std::numeric_limits<float>::lowest();
      float max_y = std::numeric_limits<float>::lowest();
      float max_z = std::numeric_limits<float>::lowest();

      for (size_t i = 0; i < count; ++i)
      {
        const auto  &inst = entry.group.instances[start_idx + i];
        const size_t tx = i % INSTANCE_TEX_WIDTH;
        const size_t ty = i / INSTANCE_TEX_WIDTH;
        const size_t pixel_base = ty * tex_w + tx * 2;

        const float rad = inst.rotation_deg *
                          (std::numbers::pi_v<float> / 180.0f);
        (*tex_data)[pixel_base + 0] = {inst.position.x,
                                       inst.position.y,
                                       inst.position.z,
                                       inst.scale};
        (*tex_data)[pixel_base + 1] = {std::cos(rad),
                                       std::sin(rad),
                                       0.0f,
                                       0.0f};

        const float pad = 25.0f * std::abs(inst.scale);
        min_x = std::min(min_x, inst.position.x - pad);
        min_y = std::min(min_y, inst.position.y - pad);
        min_z = std::min(min_z, inst.position.z - pad);
        max_x = std::max(max_x, inst.position.x + pad);
        max_y = std::max(max_y, inst.position.y + pad);
        max_z = std::max(max_z, inst.position.z + pad);
      }

      auto &batch = entry.batches[b];
      batch.instance_data_tex =
          filament::Texture::Builder()
              .width(tex_w)
              .height(tex_h)
              .levels(1)
              .sampler(filament::Texture::Sampler::SAMPLER_2D)
              .format(filament::Texture::InternalFormat::RGBA32F)
              .build(*impl_->engine);

      filament::Texture::PixelBufferDescriptor pbd(
          tex_data->data(),
          tex_data->size() * sizeof(filament::math::float4),
          filament::Texture::Format::RGBA,
          filament::Texture::Type::FLOAT,
          [](void * /*buf*/, size_t /*sz*/, void *user)
          { delete static_cast<std::vector<filament::math::float4> *>(user); },
          tex_data);
      batch.instance_data_tex->setImage(*impl_->engine, 0, std::move(pbd));

      batch.mat_instance = impl_->instanced_obj_mat->createInstance();
      batch.mat_instance->setParameter("instanceDataMap",
                                       batch.instance_data_tex,
                                       nearest_sampler);
      batch.mat_instance->setParameter("instanceTexWidth", INSTANCE_TEX_WIDTH);
      batch.mat_instance->setParameter(
          "baseColorTint",
          filament::math::float3(entry.group.base_color.x,
                                 entry.group.base_color.y,
                                 entry.group.base_color.z));
      batch.mat_instance->setParameter("roughness", entry.group.roughness);
      batch.mat_instance->setParameter("metallic", entry.group.metallic);

      batch.entity = utils::EntityManager::get().create();

      filament::Box batch_box;
      batch_box.set({min_x, min_y, min_z}, {max_x, max_y, max_z});

      filament::RenderableManager::Builder(1)
          .boundingBox(batch_box)
          .material(0, batch.mat_instance)
          .geometry(0,
                    filament::RenderableManager::PrimitiveType::TRIANGLES,
                    entry.vb,
                    entry.ib,
                    0,
                    entry.index_count)
          .instances(count)
          .castShadows(true)
          .receiveShadows(true)
          .culling(false)
          .build(*impl_->engine, batch.entity);

      impl_->scene->addEntity(batch.entity);
      batch.in_scene = true;
    }

    entry.gpu_dirty = false;
  }
}

void ViewportWidget::update_shared_grid_mesh()
{
  if (!impl_->engine)
  {
    return;
  }

  if (impl_->shared_grid_vb)
  {
    impl_->engine->destroy(impl_->shared_grid_vb);
    impl_->shared_grid_vb = nullptr;
  }

  if (impl_->shared_solid_ib)
  {
    impl_->engine->destroy(impl_->shared_solid_ib);
    impl_->shared_solid_ib = nullptr;
  }

  if (impl_->shared_wire_ib)
  {
    impl_->engine->destroy(impl_->shared_wire_ib);
    impl_->shared_wire_ib = nullptr;
  }

  const auto   grid = terrain::GridMeshFactory::create_unit_grid(grid_res_);
  const size_t vertex_count = grid.vertices.size();

  std::vector<filament::math::float3> positions(vertex_count);
  for (size_t i = 0; i < vertex_count; ++i)
  {
    positions[i] = {grid.vertices[i].u, grid.vertices[i].v, 0.0f};
  }

  const filament::math::quatf flat_tangent(1.0f, 0.0f, 0.0f, 0.0f);
  std::vector<filament::math::quatf> tangents(vertex_count, flat_tangent);

  impl_->shared_grid_vb = filament::VertexBuffer::Builder()
                              .vertexCount(static_cast<uint32_t>(vertex_count))
                              .bufferCount(2)
                              .attribute(
                                  filament::VertexAttribute::POSITION,
                                  0,
                                  filament::VertexBuffer::AttributeType::FLOAT3,
                                  0,
                                  sizeof(filament::math::float3))
                              .attribute(
                                  filament::VertexAttribute::TANGENTS,
                                  1,
                                  filament::VertexBuffer::AttributeType::FLOAT4,
                                  0,
                                  sizeof(filament::math::quatf))
                              .build(*impl_->engine);

  auto *pos_copy = new std::vector<filament::math::float3>(
      std::move(positions));
  impl_->shared_grid_vb->setBufferAt(
      *impl_->engine,
      0,
      filament::VertexBuffer::BufferDescriptor(
          pos_copy->data(),
          pos_copy->size() * sizeof(filament::math::float3),
          [](void * /*b*/, size_t /*s*/, void *u)
          { delete static_cast<std::vector<filament::math::float3> *>(u); },
          pos_copy));

  auto *tan_copy = new std::vector<filament::math::quatf>(std::move(tangents));
  impl_->shared_grid_vb->setBufferAt(
      *impl_->engine,
      1,
      filament::VertexBuffer::BufferDescriptor(
          tan_copy->data(),
          tan_copy->size() * sizeof(filament::math::quatf),
          [](void * /*b*/, size_t /*s*/, void *u)
          { delete static_cast<std::vector<filament::math::quatf> *>(u); },
          tan_copy));

  // Solid triangle index buffer
  impl_->solid_index_count = static_cast<uint32_t>(grid.indices.size());
  impl_->shared_solid_ib = filament::IndexBuffer::Builder()
                               .indexCount(impl_->solid_index_count)
                               .bufferType(
                                   filament::IndexBuffer::IndexType::UINT)
                               .build(*impl_->engine);

  auto *solid_copy = new std::vector<uint32_t>(grid.indices);
  impl_->shared_solid_ib->setBuffer(
      *impl_->engine,
      filament::IndexBuffer::BufferDescriptor(
          solid_copy->data(),
          solid_copy->size() * sizeof(uint32_t),
          [](void * /*b*/, size_t /*s*/, void *u)
          { delete static_cast<std::vector<uint32_t> *>(u); },
          solid_copy));

  // Wireframe line index buffer
  const size_t          quads_per_side = grid_res_ - 1;
  std::vector<uint32_t> wire_indices;
  wire_indices.reserve(quads_per_side * quads_per_side * 6);

  for (size_t y = 0; y < quads_per_side; ++y)
  {
    for (size_t x = 0; x < quads_per_side; ++x)
    {
      const uint32_t i0 = static_cast<uint32_t>(y * grid_res_ + x);
      const uint32_t i1 = static_cast<uint32_t>(y * grid_res_ + (x + 1));
      const uint32_t i2 = static_cast<uint32_t>((y + 1) * grid_res_ + x);
      const uint32_t i3 = static_cast<uint32_t>((y + 1) * grid_res_ + (x + 1));

      wire_indices.push_back(i0);
      wire_indices.push_back(i1);
      wire_indices.push_back(i0);
      wire_indices.push_back(i2);
      wire_indices.push_back(i1);
      wire_indices.push_back(i2);

      if (x == quads_per_side - 1)
      {
        wire_indices.push_back(i1);
        wire_indices.push_back(i3);
      }

      if (y == quads_per_side - 1)
      {
        wire_indices.push_back(i2);
        wire_indices.push_back(i3);
      }
    }
  }

  impl_->wire_index_count = static_cast<uint32_t>(wire_indices.size());
  impl_->shared_wire_ib = filament::IndexBuffer::Builder()
                              .indexCount(impl_->wire_index_count)
                              .bufferType(
                                  filament::IndexBuffer::IndexType::UINT)
                              .build(*impl_->engine);

  auto *wire_copy = new std::vector<uint32_t>(std::move(wire_indices));
  impl_->shared_wire_ib->setBuffer(
      *impl_->engine,
      filament::IndexBuffer::BufferDescriptor(
          wire_copy->data(),
          wire_copy->size() * sizeof(uint32_t),
          [](void * /*b*/, size_t /*s*/, void *u)
          { delete static_cast<std::vector<uint32_t> *>(u); },
          wire_copy));
}

void ViewportWidget::update_sky_dome()
{
  if (!filament_initialized_ || !impl_->engine || !impl_->sky_mat_instance)
  {
    return;
  }

  const bool show_atmosphere = env_settings_.pbr_enabled &&
                               env_settings_.atmospheric_scattering_enabled;

  if (show_atmosphere)
  {
    const QVector3D     eye = get_camera_pos();
    const terrain::Vec3 sun_ray = scene::compute_sun_direction(
        sun_settings_.azimuth_deg,
        sun_settings_.elevation_deg);
    const terrain::Vec3 sun_to_sky{-sun_ray.x, -sun_ray.y, -sun_ray.z};

    const terrain::Vec3 sun_col = sun_settings_.auto_color_temperature
                                      ? scene::compute_sun_color_from_elevation(
                                            sun_settings_.elevation_deg)
                                      : sun_settings_.base_color;

    impl_->sky_mat_instance->setParameter(
        "cameraPos",
        filament::math::float3(eye.x(), eye.y(), eye.z()));
    impl_->sky_mat_instance->setParameter(
        "sunDirToSky",
        filament::math::float3(sun_to_sky.x, sun_to_sky.y, sun_to_sky.z));
    impl_->sky_mat_instance->setParameter(
        "sunColor",
        filament::math::float3(sun_col.x, sun_col.y, sun_col.z));
    impl_->sky_mat_instance->setParameter("rayleighStrength",
                                          env_settings_.rayleigh_strength);
    impl_->sky_mat_instance->setParameter("mieStrength",
                                          env_settings_.mie_strength);
    impl_->sky_mat_instance->setParameter(
        "sunDiskAngularRadius",
        sun_settings_.sun_angular_radius_deg *
            (std::numbers::pi_v<float> / 180.0f) * 3.0f);
    impl_->sky_mat_instance->setParameter("renderSunDisk",
                                          sun_settings_.render_sun_disk);

    if (!impl_->sky_dome_in_scene)
    {
      impl_->scene->setSkybox(nullptr);
      impl_->scene->addEntity(impl_->sky_dome_entity);
      impl_->sky_dome_in_scene = true;
    }
  }
  else
  {
    if (impl_->sky_dome_in_scene)
    {
      impl_->scene->remove(impl_->sky_dome_entity);
      impl_->sky_dome_in_scene = false;
    }
    impl_->scene->setSkybox(impl_->skybox);
  }
}

void ViewportWidget::update_terrain_renderables()
{
  if (!filament_initialized_ || !impl_->engine || !impl_->shared_grid_vb)
  {
    return;
  }

  filament::TextureSampler linear_sampler(
      filament::TextureSampler::MinFilter::LINEAR,
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

  // 1. Ensure GPU textures are uploaded for each terrain
  for (auto &[id, entry] : impl_->terrains)
  {
    if (!entry.textures_dirty || !entry.descriptor.heightmap)
    {
      continue;
    }

    impl_->destroy_terrain_textures(entry);

    const auto  &hdata = *entry.descriptor.heightmap;
    const size_t w = hdata.get_width();
    const size_t h = hdata.get_height();

    entry.heightmap_tex = filament::Texture::Builder()
                              .width(static_cast<uint32_t>(w))
                              .height(static_cast<uint32_t>(h))
                              .levels(1)
                              .sampler(filament::Texture::Sampler::SAMPLER_2D)
                              .format(filament::Texture::InternalFormat::R32F)
                              .build(*impl_->engine);

    auto *h_copy = new std::vector<float>(hdata.get_data());
    filament::Texture::PixelBufferDescriptor h_buf(
        h_copy->data(),
        h_copy->size() * sizeof(float),
        filament::Texture::Format::R,
        filament::Texture::Type::FLOAT,
        [](void * /*b*/, size_t /*s*/, void *u)
        { delete static_cast<std::vector<float> *>(u); },
        h_copy);
    entry.heightmap_tex->setImage(*impl_->engine, 0, std::move(h_buf));

    if (!entry.descriptor.normal_map_rgb8.empty())
    {
      entry.normal_tex = filament::Texture::Builder()
                             .width(static_cast<uint32_t>(w))
                             .height(static_cast<uint32_t>(h))
                             .levels(1)
                             .sampler(filament::Texture::Sampler::SAMPLER_2D)
                             .format(filament::Texture::InternalFormat::RGB8)
                             .build(*impl_->engine);

      auto *n_copy = new std::vector<uint8_t>(entry.descriptor.normal_map_rgb8);
      filament::Texture::PixelBufferDescriptor n_buf(
          n_copy->data(),
          n_copy->size(),
          filament::Texture::Format::RGB,
          filament::Texture::Type::UBYTE,
          [](void * /*b*/, size_t /*s*/, void *u)
          { delete static_cast<std::vector<uint8_t> *>(u); },
          n_copy);
      entry.normal_tex->setImage(*impl_->engine, 0, std::move(n_buf));
    }

    if (!entry.descriptor.albedo_map_rgb8.empty())
    {
      const size_t tex_w = (entry.descriptor.texture_width > 0)
                               ? entry.descriptor.texture_width
                               : w;
      const size_t tex_h = (entry.descriptor.texture_height > 0)
                               ? entry.descriptor.texture_height
                               : h;

      entry.albedo_tex = filament::Texture::Builder()
                             .width(static_cast<uint32_t>(tex_w))
                             .height(static_cast<uint32_t>(tex_h))
                             .levels(1)
                             .sampler(filament::Texture::Sampler::SAMPLER_2D)
                             .format(filament::Texture::InternalFormat::RGB8)
                             .build(*impl_->engine);

      auto *a_copy = new std::vector<uint8_t>(entry.descriptor.albedo_map_rgb8);
      filament::Texture::PixelBufferDescriptor a_buf(
          a_copy->data(),
          a_copy->size(),
          filament::Texture::Format::RGB,
          filament::Texture::Type::UBYTE,
          [](void * /*b*/, size_t /*s*/, void *u)
          { delete static_cast<std::vector<uint8_t> *>(u); },
          a_copy);
      entry.albedo_tex->setImage(*impl_->engine, 0, std::move(a_buf));
    }

    entry.textures_dirty = false;
  }

  // 2. Count total active CDLOD instances across all terrains
  size_t total_instances = 0;
  for (const auto &[id, entry] : impl_->terrains)
  {
    total_instances += entry.lod_instances.size();
  }

  const QVector3D              eye = get_camera_pos();
  const filament::math::float3 cam_pos(eye.x(), eye.y(), eye.z());
  const bool             use_lit = shading_enabled_ && !wireframe_enabled_;
  filament::IndexBuffer *active_ib = wireframe_enabled_
                                         ? impl_->shared_wire_ib
                                         : impl_->shared_solid_ib;
  const uint32_t         active_idx_count = wireframe_enabled_
                                                ? impl_->wire_index_count
                                                : impl_->solid_index_count;
  const filament::RenderableManager::PrimitiveType prim_type =
      wireframe_enabled_
          ? filament::RenderableManager::PrimitiveType::LINES
          : filament::RenderableManager::PrimitiveType::TRIANGLES;

  auto &rm = impl_->engine->getRenderableManager();

  // Expand pool if needed
  if (impl_->terrain_node_pool.size() < total_instances)
  {
    const size_t prev_size = impl_->terrain_node_pool.size();
    impl_->terrain_node_pool.resize(total_instances);

    for (size_t i = prev_size; i < total_instances; ++i)
    {
      auto &node = impl_->terrain_node_pool[i];
      node.entity = utils::EntityManager::get().create();
      node.unlit_instance = impl_->terrain_unlit_mat->createInstance();
      node.lit_instance = impl_->terrain_lit_mat->createInstance();
      node.in_scene = false;

      filament::RenderableManager::Builder(1)
          .boundingBox({{-domain_lx_ * 0.6f,
                         -domain_ly_ * 0.6f,
                         -domain_max_height_ * 0.1f},
                        {domain_lx_ * 0.6f,
                         domain_ly_ * 0.6f,
                         domain_max_height_}})
          .material(0, use_lit ? node.lit_instance : node.unlit_instance)
          .geometry(0,
                    prim_type,
                    impl_->shared_grid_vb,
                    active_ib,
                    0,
                    active_idx_count)
          .castShadows(true)
          .receiveShadows(true)
          .culling(false)
          .build(*impl_->engine, node.entity);
    }
  }

  // 3. Populate pooled nodes for each terrain's CDLOD instances
  size_t pool_idx = 0;
  for (const auto &[id, entry] : impl_->terrains)
  {
    if (!entry.patch || !entry.heightmap_tex)
    {
      continue;
    }

    const auto &desc = entry.descriptor;
    const float rad = desc.rotation_deg * (std::numbers::pi_v<float> / 180.0f);
    const float cos_t = std::cos(rad);
    const float sin_t = std::sin(rad);

    for (const auto &inst : entry.lod_instances)
    {
      auto &node = impl_->terrain_node_pool[pool_idx++];

      auto ri = rm.getInstance(node.entity);
      if (ri)
      {
        rm.setMaterialInstanceAt(ri,
                                 0,
                                 use_lit ? node.lit_instance
                                         : node.unlit_instance);
        rm.setGeometryAt(ri,
                         0,
                         prim_type,
                         impl_->shared_grid_vb,
                         active_ib,
                         0,
                         active_idx_count);

        const terrain::AABB node_aabb = entry.patch
                                            ->compute_subregion_world_aabb(
                                                inst.offset_u,
                                                inst.offset_v,
                                                inst.offset_u + inst.scale_u,
                                                inst.offset_v + inst.scale_v,
                                                -10.0f,
                                                desc.height_scale * 1.25f);

        filament::Box box;
        box.set({node_aabb.min.x, node_aabb.min.y, node_aabb.min.z},
                {node_aabb.max.x, node_aabb.max.y, node_aabb.max.z});
        rm.setAxisAlignedBoundingBox(ri, box);
      }

      filament::MaterialInstance *mi = use_lit ? node.lit_instance
                                               : node.unlit_instance;

      mi->setParameter("cameraPos", cam_pos);
      mi->setParameter("patchOffsetScale",
                       filament::math::float4(inst.offset_u,
                                              inst.offset_v,
                                              inst.scale_u,
                                              inst.scale_v));
      mi->setParameter(
          "patchMorphLod",
          filament::math::float4(inst.morph_start,
                                 inst.morph_range,
                                 static_cast<float>(inst.lod_level),
                                 0.0f));
      mi->setParameter(
          "patchCenter",
          filament::math::float3(desc.center.x, desc.center.y, desc.center.z));
      mi->setParameter("patchSize", filament::math::float2(desc.lx, desc.ly));
      mi->setParameter("patchCosSin", filament::math::float2(cos_t, sin_t));
      mi->setParameter("heightScale", desc.height_scale);
      mi->setParameter("gridDim", static_cast<float>(grid_res_));
      mi->setParameter("morphEnabled", morphing_enabled_);
      mi->setParameter("heightmap", entry.heightmap_tex, linear_sampler);

      if (use_lit)
      {
        mi->setParameter("useLodColors", lod_color_debug_);
        mi->setParameter("useAlbedoTexture", entry.albedo_tex != nullptr);
        mi->setParameter("normalMap",
                         entry.normal_tex ? entry.normal_tex
                                          : impl_->fallback_white_tex,
                         linear_sampler);
        mi->setParameter("albedoMap",
                         entry.albedo_tex ? entry.albedo_tex
                                          : impl_->fallback_white_tex,
                         linear_sampler);
      }

      if (!node.in_scene)
      {
        impl_->scene->addEntity(node.entity);
        node.in_scene = true;
      }
    }
  }

  // Hide unused pooled nodes
  for (size_t i = pool_idx; i < impl_->terrain_node_pool.size(); ++i)
  {
    auto &node = impl_->terrain_node_pool[i];
    if (node.in_scene)
    {
      impl_->scene->remove(node.entity);
      node.in_scene = false;
    }
  }

  impl_->active_cdlod_nodes = pool_idx;
}

void ViewportWidget::set_terrain_visible(const std::string &id, bool visible)
{
  auto it = impl_->terrains.find(id);
  if (it != impl_->terrains.end() && it->second.visible != visible)
  {
    it->second.visible = visible;
    if (filament_initialized_)
    {
      update_terrain_renderables();
    }
  }
}

void ViewportWidget::set_albedo_enabled(bool enabled)
{
  if (albedo_enabled_ != enabled)
  {
    albedo_enabled_ = enabled;
    if (filament_initialized_)
    {
      update_terrain_renderables();
    }
  }
}

void ViewportWidget::set_normal_map_enabled(bool enabled)
{
  if (normal_map_enabled_ != enabled)
  {
    normal_map_enabled_ = enabled;
    if (filament_initialized_)
    {
      update_terrain_renderables();
    }
  }
}

void ViewportWidget::set_height_scale_multiplier(float multiplier)
{
  const float clamped = std::max(0.01f, multiplier);
  if (std::abs(height_scale_multiplier_ - clamped) > 1e-5f)
  {
    height_scale_multiplier_ = clamped;
    if (filament_initialized_)
    {
      update_terrain_renderables();
      update_water_renderables();
    }
  }
}

float ViewportWidget::get_height_scale_multiplier() const noexcept
{
  return height_scale_multiplier_;
}

void ViewportWidget::add_or_update_water(
    const scene::WaterLayerDescriptor &descriptor)
{
  auto &entry = impl_->waters[descriptor.id];
  entry.descriptor = descriptor;
  entry.textures_dirty = true;

  if (filament_initialized_)
  {
    update_water_renderables();
  }
}

void ViewportWidget::clear_waters()
{
  impl_->clear_water_node_pool();
  for (auto &[id, entry] : impl_->waters)
  {
    impl_->destroy_water_textures(entry);
  }
  impl_->waters.clear();
}

void ViewportWidget::remove_water(const std::string &id)
{
  auto it = impl_->waters.find(id);
  if (it != impl_->waters.end())
  {
    impl_->destroy_water_textures(it->second);
    impl_->waters.erase(it);
    if (filament_initialized_)
    {
      update_water_renderables();
    }
  }
}

void ViewportWidget::set_water_visible(const std::string &id, bool visible)
{
  auto it = impl_->waters.find(id);
  if (it != impl_->waters.end() && it->second.visible != visible)
  {
    it->second.visible = visible;
    if (filament_initialized_)
    {
      update_water_renderables();
    }
  }
}

const scene::WaterSettings &ViewportWidget::get_water_settings() const noexcept
{
  return water_settings_;
}

void ViewportWidget::set_water_settings(const scene::WaterSettings &settings)
{
  water_settings_ = settings;
  if (filament_initialized_)
  {
    update_water_renderables();
  }
}

void ViewportWidget::set_instanced_group_visible(const std::string &id,
                                                 bool               visible)
{
  auto it = impl_->instanced_groups.find(id);
  if (it != impl_->instanced_groups.end() && it->second.visible != visible)
  {
    it->second.visible = visible;
    if (impl_->scene)
    {
      for (auto &batch : it->second.batches)
      {
        if (visible && !batch.in_scene && batch.entity)
        {
          impl_->scene->addEntity(batch.entity);
          batch.in_scene = true;
        }
        else if (!visible && batch.in_scene && batch.entity)
        {
          impl_->scene->remove(batch.entity);
          batch.in_scene = false;
        }
      }
    }
  }
}

void ViewportWidget::set_path_visible(const std::string &id, bool visible)
{
  auto it = impl_->paths.find(id);
  if (it != impl_->paths.end() && it->second.visible != visible)
  {
    it->second.visible = visible;
    if (impl_->scene && it->second.entity)
    {
      if (visible && !it->second.in_scene)
      {
        impl_->scene->addEntity(it->second.entity);
        it->second.in_scene = true;
      }
      else if (!visible && it->second.in_scene)
      {
        impl_->scene->remove(it->second.entity);
        it->second.in_scene = false;
      }
    }
  }
}

void ViewportWidget::set_rendering_domain(float lx, float ly, float max_height)
{
  domain_lx_ = std::max(0.1f, lx);
  domain_ly_ = std::max(0.1f, ly);
  domain_max_height_ = std::max(0.1f, max_height);

  if (filament_initialized_ && impl_->engine)
  {
    auto &rm = impl_->engine->getRenderableManager();
    const filament::Box box(
        {-domain_lx_ * 0.6f, -domain_ly_ * 0.6f, -domain_max_height_ * 0.1f},
        {domain_lx_ * 0.6f, domain_ly_ * 0.6f, domain_max_height_});
    for (const auto &node : impl_->terrain_node_pool)
    {
      if (node.entity)
      {
        const auto inst = rm.getInstance(node.entity);
        if (inst)
        {
          rm.setAxisAlignedBoundingBox(inst, box);
        }
      }
    }
  }
}

void ViewportWidget::reset_camera()
{
  const float dist = std::max(domain_lx_, domain_ly_) * 0.9f;
  set_camera_orbit(QVector3D(0.0f, 0.0f, 0.0f), dist, -45.0f, 32.0f);
  set_fov_deg(45.0f);
}

void ViewportWidget::set_fov_deg(float fov_deg)
{
  camera_fov_deg_ = std::clamp(fov_deg, 10.0f, 120.0f);
  if (filament_initialized_)
  {
    update_camera();
  }
}

float ViewportWidget::get_fov_deg() const noexcept
{
  return camera_fov_deg_;
}

QImage ViewportWidget::grab_framebuffer()
{
  if (!filament_initialized_ || !impl_->engine || !impl_->renderer ||
      !impl_->swap_chain || !impl_->view)
  {
    return QImage();
  }

  const uint32_t w = static_cast<uint32_t>(std::max(1, width()));
  const uint32_t h = static_cast<uint32_t>(std::max(1, height()));

  QImage img(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGBA8888);
  const size_t byte_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
  auto        *buf = new uint8_t[byte_count];

  bool done = false;
  if (impl_->renderer->beginFrame(impl_->swap_chain))
  {
    impl_->renderer->render(impl_->view);

    filament::backend::PixelBufferDescriptor pbd(
        buf,
        byte_count,
        filament::backend::PixelBufferDescriptor::PixelDataFormat::RGBA,
        filament::backend::PixelBufferDescriptor::PixelDataType::UBYTE,
        [](void *buffer, size_t, void *user)
        {
          delete[] static_cast<uint8_t *>(buffer);
          if (user)
          {
            *static_cast<bool *>(user) = true;
          }
        },
        &done);

    impl_->renderer->readPixels(0, 0, w, h, std::move(pbd));
    impl_->renderer->endFrame();
  }
  else
  {
    delete[] buf;
    return QImage();
  }

  filament::Fence *fence = impl_->engine->createFence();
  if (fence)
  {
    fence->wait(filament::Fence::Mode::FLUSH);
    impl_->engine->destroy(fence);
  }

  // Pump a frame if callback hasn't fired yet
  if (!done)
  {
    return QImage();
  }

  return img;
}

void ViewportWidget::update_water_renderables()
{
  if (!filament_initialized_ || !impl_->engine || !impl_->water_mat ||
      !impl_->shared_grid_vb)
  {
    return;
  }

  filament::TextureSampler linear_sampler(
      filament::TextureSampler::MinFilter::LINEAR,
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

  size_t pool_idx = 0;

  for (auto &[id, entry] : impl_->waters)
  {
    if (!entry.visible || !entry.descriptor.terrain_heightmap ||
        !entry.descriptor.water_depth_map)
    {
      continue;
    }

    if (entry.textures_dirty)
    {
      impl_->destroy_water_textures(entry);

      const auto  &hdata = *entry.descriptor.terrain_heightmap;
      const auto  &ddata = *entry.descriptor.water_depth_map;
      const size_t w = hdata.get_width();
      const size_t h = hdata.get_height();

      if (w > 0 && h > 0 && ddata.get_width() == w && ddata.get_height() == h)
      {
        entry.heightmap_tex =
            filament::Texture::Builder()
                .width(static_cast<uint32_t>(w))
                .height(static_cast<uint32_t>(h))
                .levels(1)
                .sampler(filament::Texture::Sampler::SAMPLER_2D)
                .format(filament::Texture::InternalFormat::R32F)
                .build(*impl_->engine);

        const size_t nbytes = w * h * sizeof(float);
        float       *h_copy = new float[w * h];
        std::memcpy(h_copy, hdata.get_data().data(), nbytes);
        entry.heightmap_tex->setImage(
            *impl_->engine,
            0,
            filament::Texture::PixelBufferDescriptor(
                h_copy,
                nbytes,
                filament::Texture::Format::R,
                filament::Texture::Type::FLOAT,
                [](void *buf, size_t, void *)
                { delete[] static_cast<float *>(buf); }));

        entry.depth_tex =
            filament::Texture::Builder()
                .width(static_cast<uint32_t>(w))
                .height(static_cast<uint32_t>(h))
                .levels(1)
                .sampler(filament::Texture::Sampler::SAMPLER_2D)
                .format(filament::Texture::InternalFormat::R32F)
                .build(*impl_->engine);

        float *d_copy = new float[w * h];
        std::memcpy(d_copy, ddata.get_data().data(), nbytes);
        entry.depth_tex->setImage(
            *impl_->engine,
            0,
            filament::Texture::PixelBufferDescriptor(
                d_copy,
                nbytes,
                filament::Texture::Format::R,
                filament::Texture::Type::FLOAT,
                [](void *buf, size_t, void *)
                { delete[] static_cast<float *>(buf); }));
      }

      entry.textures_dirty = false;
    }

    if (!entry.heightmap_tex || !entry.depth_tex)
    {
      continue;
    }

    const float rad = qDegreesToRadians(entry.descriptor.rotation_deg);
    const filament::math::float2 cos_sin(std::cos(rad), std::sin(rad));
    const float h_scale = entry.descriptor.height_scale *
                          height_scale_multiplier_;

    // Single full-domain patch for water surface
    if (pool_idx >= impl_->water_node_pool.size())
    {
      WaterPatchNode node;
      node.entity = utils::EntityManager::get().create();
      node.mat_instance = impl_->water_mat->createInstance();

      filament::Box bounds(filament::math::float3(-5000.0f, -5000.0f, -1000.0f),
                           filament::math::float3(5000.0f, 5000.0f, 5000.0f));

      filament::RenderableManager::Builder(1)
          .boundingBox(bounds)
          .material(0, node.mat_instance)
          .geometry(0,
                    filament::RenderableManager::PrimitiveType::TRIANGLES,
                    impl_->shared_grid_vb,
                    impl_->shared_solid_ib,
                    0,
                    impl_->solid_index_count)
          .castShadows(false)
          .receiveShadows(true)
          .culling(false)
          .build(*impl_->engine, node.entity);

      impl_->water_node_pool.push_back(node);
    }

    auto &node = impl_->water_node_pool[pool_idx++];
    auto *mi = node.mat_instance;
    mi->setParameter("heightmap", entry.heightmap_tex, linear_sampler);
    mi->setParameter("waterDepthMap", entry.depth_tex, linear_sampler);
    mi->setParameter("patchOffsetScale",
                     filament::math::float4(0.0f, 0.0f, 1.0f, 1.0f));
    mi->setParameter("patchCenter",
                     filament::math::float3(entry.descriptor.center.x,
                                            entry.descriptor.center.y,
                                            entry.descriptor.center.z));
    mi->setParameter("patchSize",
                     filament::math::float2(entry.descriptor.lx,
                                            entry.descriptor.ly));
    mi->setParameter("patchCosSin", cos_sin);
    mi->setParameter("heightScale", h_scale);
    mi->setParameter("shallowColor",
                     filament::math::float4(water_settings_.shallow_color.x,
                                            water_settings_.shallow_color.y,
                                            water_settings_.shallow_color.z,
                                            1.0f));
    mi->setParameter("deepColor",
                     filament::math::float4(water_settings_.deep_color.x,
                                            water_settings_.deep_color.y,
                                            water_settings_.deep_color.z,
                                            1.0f));
    mi->setParameter("waterParams",
                     filament::math::float4(water_settings_.depth_falloff,
                                            water_settings_.opacity,
                                            water_settings_.roughness,
                                            0.0f));

    if (!node.in_scene)
    {
      impl_->scene->addEntity(node.entity);
      node.in_scene = true;
    }
  }

  for (size_t i = pool_idx; i < impl_->water_node_pool.size(); ++i)
  {
    auto &node = impl_->water_node_pool[i];
    if (node.in_scene)
    {
      impl_->scene->remove(node.entity);
      node.in_scene = false;
    }
  }
}

} // namespace oros::ui
