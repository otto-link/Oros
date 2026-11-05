/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "FastNoiseLite.h"

#include "cdlod_filament_widget.hpp"
#include "oros/logger.hpp"
#include "oros/terrain/grid_mesh_factory.hpp"

#include <QCoreApplication>
#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <geometry/SurfaceOrientation.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

// Include precompiled material bytecode
namespace
{
static const uint8_t MATERIAL_UNLIT_PACKAGE[] = {
#include "materials/cdlod_terrain_unlit.h"
};

static const uint8_t MATERIAL_LIT_PACKAGE[] = {
#include "materials/cdlod_terrain_lit.h"
};
} // namespace

namespace oros::ui
{

// --- Implementation

CdlodFilamentWidget::CdlodFilamentWidget(QWidget *parent) : QWidget(parent)
{
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);

  camera_entity_ = std::make_unique<utils::Entity>();
  sun_light_entity_ = std::make_unique<utils::Entity>();

  // Initialize CDLOD context
  context_.camera_pos = oros::terrain::Vec3{0.0f, 0.0f, 200.0f};
  context_.morph_ratio = 0.66f;

  // Initialize quadtree
  quadtree_.set_max_lod_levels(8);

  // Generate initial heightmap patch and normal map
  regenerate_noise();

  // Setup render loop timer
  render_timer_ = new QTimer(this);
  connect(render_timer_,
          &QTimer::timeout,
          this,
          &CdlodFilamentWidget::render_frame);
}

CdlodFilamentWidget::~CdlodFilamentWidget()
{
  if (render_timer_)
  {
    render_timer_->stop();
  }

  destroy_filament_resources();
}

void CdlodFilamentWidget::clear_all_nodes()
{
  if (!engine_)
  {
    return;
  }

  for (auto &node : node_pool_)
  {
    if (node.in_scene && node.entity)
    {
      scene_->remove(node.entity);
      node.in_scene = false;
    }

    if (node.entity)
    {
      engine_->destroy(node.entity);
      utils::EntityManager::get().destroy(node.entity);
      node.entity = {};
    }

    if (node.unlit_instance)
    {
      engine_->destroy(node.unlit_instance);
      node.unlit_instance = nullptr;
    }

    if (node.lit_instance)
    {
      engine_->destroy(node.lit_instance);
      node.lit_instance = nullptr;
    }
  }

  node_pool_.clear();
  active_node_count_ = 0;
}

void CdlodFilamentWidget::create_patch()
{
  if (!heightmap_data_)
  {
    return;
  }

  const auto patch_start = std::chrono::steady_clock::now();

  patch_ = std::make_unique<oros::terrain::HeightmapPatch>(
      "test_patch",
      heightmap_data_,
      oros::terrain::Vec3{0.0f, 0.0f, 0.0f},
      world_size_,
      world_size_,
      0.0f);

  const auto   patch_end = std::chrono::steady_clock::now();
  const double patch_ms = std::chrono::duration<double, std::milli>(patch_end -
                                                                    patch_start)
                              .count();

  oros::Logger::log()->info(
      "Patch generation: size {:.1f}x{:.1f} | time: {:.2f} ms",
      world_size_,
      world_size_,
      patch_ms);

  if (initialized_)
  {
    update_textures();
    recompute_lod();
  }
}

void CdlodFilamentWidget::destroy_filament_resources()
{
  if (!engine_)
  {
    return;
  }

  clear_all_nodes();

  if (shared_vb_)
  {
    engine_->destroy(shared_vb_);
    shared_vb_ = nullptr;
  }

  if (shared_solid_ib_)
  {
    engine_->destroy(shared_solid_ib_);
    shared_solid_ib_ = nullptr;
  }

  if (shared_wire_ib_)
  {
    engine_->destroy(shared_wire_ib_);
    shared_wire_ib_ = nullptr;
  }

  if (heightmap_texture_)
  {
    engine_->destroy(heightmap_texture_);
    heightmap_texture_ = nullptr;
  }

  if (normal_texture_)
  {
    engine_->destroy(normal_texture_);
    normal_texture_ = nullptr;
  }

  if (sun_light_entity_ && *sun_light_entity_)
  {
    scene_->remove(*sun_light_entity_);
    engine_->destroy(*sun_light_entity_);
    utils::EntityManager::get().destroy(*sun_light_entity_);
  }

  if (indirect_light_)
  {
    scene_->setIndirectLight(nullptr);
    engine_->destroy(indirect_light_);
    indirect_light_ = nullptr;
  }

  if (camera_entity_ && *camera_entity_)
  {
    engine_->destroyCameraComponent(*camera_entity_);
    utils::EntityManager::get().destroy(*camera_entity_);
  }

  if (skybox_)
  {
    scene_->setSkybox(nullptr);
    engine_->destroy(skybox_);
    skybox_ = nullptr;
  }

  if (lit_material_)
  {
    engine_->destroy(lit_material_);
    lit_material_ = nullptr;
  }

  if (unlit_material_)
  {
    engine_->destroy(unlit_material_);
    unlit_material_ = nullptr;
  }

  if (view_)
  {
    engine_->destroy(view_);
    view_ = nullptr;
  }

  if (scene_)
  {
    engine_->destroy(scene_);
    scene_ = nullptr;
  }

  if (renderer_)
  {
    engine_->destroy(renderer_);
    renderer_ = nullptr;
  }

  if (swap_chain_)
  {
    engine_->destroy(swap_chain_);
    swap_chain_ = nullptr;
  }

  filament::Engine::destroy(&engine_);
  engine_ = nullptr;
}

void CdlodFilamentWidget::generate_noise()
{
  const auto gen_start = std::chrono::steady_clock::now();

  std::vector<float> heights(heightmap_res_ * heightmap_res_);
  const float        inv_res = (heightmap_res_ > 1)
                                   ? (1.0f / static_cast<float>(heightmap_res_ - 1))
                                   : 1.0f;

  const unsigned int hw_threads = std::thread::hardware_concurrency();
  const size_t num_threads = std::max(1u, hw_threads > 0 ? hw_threads : 8u);

  // 1. Parallel Noise Generation with std::jthread
  {
    std::vector<std::jthread> workers;
    workers.reserve(num_threads);

    const size_t rows_per_thread = (heightmap_res_ + num_threads - 1) /
                                   num_threads;

    for (size_t t = 0; t < num_threads; ++t)
    {
      const size_t y_start = t * rows_per_thread;
      const size_t y_end = std::min(heightmap_res_, y_start + rows_per_thread);

      if (y_start >= heightmap_res_)
      {
        break;
      }

      workers.emplace_back(
          [this, &heights, inv_res, y_start, y_end]()
          {
            FastNoiseLite noise;
            noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            noise.SetFractalType(FastNoiseLite::FractalType_FBm);
            noise.SetFractalOctaves(16);
            noise.SetFrequency(noise_frequency_);
            noise.SetFractalWeightedStrength(0.3f);

            for (size_t y = y_start; y < y_end; ++y)
            {
              const float  ny = static_cast<float>(y) * inv_res;
              const size_t row_idx = y * heightmap_res_;

              for (size_t x = 0; x < heightmap_res_; ++x)
              {
                const float nx = static_cast<float>(x) * inv_res;
                const float val = (noise.GetNoise(nx, ny) + 1.0f) * 0.5f;
                heights[row_idx + x] = val;
              }
            }
          });
    }
  }

  QCoreApplication::processEvents();

  const auto noise_end = std::chrono::steady_clock::now();

  heightmap_data_ = std::make_shared<oros::terrain::HeightmapData>(
      heightmap_res_,
      heightmap_res_,
      std::move(heights));

  QCoreApplication::processEvents();

  // 2. Parallel Normal Map Generation with std::jthread
  const auto  normal_start = std::chrono::steady_clock::now();
  const auto &elevations = heightmap_data_->get_data();
  normal_map_data_.resize(heightmap_res_ * heightmap_res_ * 3);

  const float inv_w = 1.0f / static_cast<float>(heightmap_res_);
  const float step_x = 2.0f * inv_w * world_size_;
  const float step_y = 2.0f * inv_w * world_size_;

  {
    std::vector<std::jthread> workers;
    workers.reserve(num_threads);

    const size_t rows_per_thread = (heightmap_res_ + num_threads - 1) /
                                   num_threads;

    for (size_t t = 0; t < num_threads; ++t)
    {
      const size_t y_start = t * rows_per_thread;
      const size_t y_end = std::min(heightmap_res_, y_start + rows_per_thread);

      if (y_start >= heightmap_res_)
      {
        break;
      }

      workers.emplace_back(
          [this, &elevations, step_x, step_y, y_start, y_end]()
          {
            for (size_t y = y_start; y < y_end; ++y)
            {
              const size_t y_prev = (y > 0) ? (y - 1) : 0;
              const size_t y_next = (y + 1 < heightmap_res_) ? (y + 1) : y;

              for (size_t x = 0; x < heightmap_res_; ++x)
              {
                const size_t x_prev = (x > 0) ? (x - 1) : 0;
                const size_t x_next = (x + 1 < heightmap_res_) ? (x + 1) : x;

                const float hL = elevations[y * heightmap_res_ + x_prev] *
                                 height_scale_;
                const float hR = elevations[y * heightmap_res_ + x_next] *
                                 height_scale_;
                const float hD = elevations[y_prev * heightmap_res_ + x] *
                                 height_scale_;
                const float hU = elevations[y_next * heightmap_res_ + x] *
                                 height_scale_;

                const float nx = -(hR - hL) / step_x;
                const float ny = -(hU - hD) / step_y;
                const float nz = 1.0f;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);

                const size_t idx = (y * heightmap_res_ + x) * 3;
                normal_map_data_[idx + 0] = static_cast<uint8_t>(
                    std::clamp((nx / len * 0.5f + 0.5f) * 255.0f,
                               0.0f,
                               255.0f));
                normal_map_data_[idx + 1] = static_cast<uint8_t>(
                    std::clamp((ny / len * 0.5f + 0.5f) * 255.0f,
                               0.0f,
                               255.0f));
                normal_map_data_[idx + 2] = static_cast<uint8_t>(
                    std::clamp((nz / len * 0.5f + 0.5f) * 255.0f,
                               0.0f,
                               255.0f));
              }
            }
          });
    }
  }

  QCoreApplication::processEvents();

  const auto normal_end = std::chrono::steady_clock::now();

  const double noise_ms =
      std::chrono::duration<double, std::milli>(noise_end - gen_start).count();
  const double normal_ms = std::chrono::duration<double, std::milli>(
                               normal_end - normal_start)
                               .count();

  oros::Logger::log()->info(
      "Noise generation: {}x{} ({} threads) | noise: {:.1f} ms | normal map: "
      "{:.1f} ms | total: {:.1f} ms",
      heightmap_res_,
      heightmap_res_,
      num_threads,
      noise_ms,
      normal_ms,
      noise_ms + normal_ms);
}

QVector3D CdlodFilamentWidget::get_camera_pos() const noexcept
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

size_t CdlodFilamentWidget::get_instance_count() const noexcept
{
  return instances_.size();
}

void CdlodFilamentWidget::init_filament()
{
  if (initialized_)
  {
    return;
  }

  // Create Filament engine with OpenGL backend
  engine_ = filament::Engine::create(filament::backend::Backend::OPENGL);
  if (!engine_)
  {
    oros::Logger::log()->error("Failed to create Filament engine!");
    return;
  }

  void *native_window = reinterpret_cast<void *>(winId());
  swap_chain_ = engine_->createSwapChain(native_window);
  renderer_ = engine_->createRenderer();
  scene_ = engine_->createScene();
  view_ = engine_->createView();

  // Dracula background clear (#282a36)
  filament::Renderer::ClearOptions clear_options;
  clear_options.clearColor = {0.157f, 0.165f, 0.212f, 1.0f};
  clear_options.clear = true;
  clear_options.discard = true;
  renderer_->setClearOptions(clear_options);

  // View configuration
  view_->setPostProcessingEnabled(true);
  view_->setDithering(filament::Dithering::TEMPORAL);
  view_->setShadowingEnabled(false);

  // Background Skybox
  skybox_ = filament::Skybox::Builder()
                .color({0.157f, 0.165f, 0.212f, 1.0f})
                .build(*engine_);
  scene_->setSkybox(skybox_);

  // Camera
  *camera_entity_ = utils::EntityManager::get().create();
  camera_ = engine_->createCamera(*camera_entity_);
  view_->setCamera(camera_);
  view_->setScene(scene_);
  view_->setViewport(filament::Viewport(0, 0, width(), height()));

  // Directional sun light
  *sun_light_entity_ = utils::EntityManager::get().create();
  filament::LightManager::Builder(filament::LightManager::Type::DIRECTIONAL)
      .color(
          filament::Color::toLinear<filament::ACCURATE>({1.0f, 0.98f, 0.92f}))
      .intensity(90000.0f)
      .direction(normalize(filament::math::float3(0.5f, 0.3f, 0.8f)))
      .castShadows(false)
      .build(*engine_, *sun_light_entity_);
  scene_->addEntity(*sun_light_entity_);

  // Indirect light (ambient)
  filament::math::float3 ambient_sh[1] = {{0.35f, 0.38f, 0.45f}};
  indirect_light_ = filament::IndirectLight::Builder()
                        .irradiance(1, ambient_sh)
                        .intensity(30000.0f)
                        .build(*engine_);
  scene_->setIndirectLight(indirect_light_);

  init_materials();
  update_shared_grid_mesh();
  update_textures();

  initialized_ = true;
  update_camera();
  recompute_lod();

  // Start frame loop at ~60 FPS
  render_timer_->start(16);
}

void CdlodFilamentWidget::init_materials()
{
  unlit_material_ = filament::Material::Builder()
                        .package(MATERIAL_UNLIT_PACKAGE,
                                 sizeof(MATERIAL_UNLIT_PACKAGE))
                        .build(*engine_);

  lit_material_ = filament::Material::Builder()
                      .package(MATERIAL_LIT_PACKAGE,
                               sizeof(MATERIAL_LIT_PACKAGE))
                      .build(*engine_);
}

void CdlodFilamentWidget::mouseMoveEvent(QMouseEvent *event)
{
  const QPoint delta = event->pos() - last_mouse_pos_;
  last_mouse_pos_ = event->pos();

  if (event->buttons() & Qt::LeftButton)
  {
    camera_yaw_ -= static_cast<float>(delta.x()) * 0.4f;
    camera_pitch_ += static_cast<float>(delta.y()) * 0.4f;
    camera_pitch_ = std::clamp(camera_pitch_, -89.0f, 89.0f);
    recompute_lod();
  }
  else if (event->buttons() & Qt::RightButton)
  {
    // Pan camera target
    // Look direction from eye to target in XY plane is (-cos(yaw), -sin(yaw))
    // Screen right vector in XY plane is (-sin(yaw), cos(yaw))
    // Screen up (forward planar) vector is (-cos(yaw), -sin(yaw))
    const float     rad_yaw = qDegreesToRadians(camera_yaw_);
    const QVector3D cam_right(-std::sin(rad_yaw), std::cos(rad_yaw), 0.0f);
    const QVector3D cam_up(-std::cos(rad_yaw), -std::sin(rad_yaw), 0.0f);

    const float pan_speed = camera_distance_ * 0.0015f;
    camera_target_ -= cam_right * (static_cast<float>(delta.x()) * pan_speed);
    camera_target_ += cam_up * (static_cast<float>(delta.y()) * pan_speed);
    recompute_lod();
  }
}

void CdlodFilamentWidget::mousePressEvent(QMouseEvent *event)
{
  last_mouse_pos_ = event->pos();
}

void CdlodFilamentWidget::recompute_lod()
{
  if (!patch_)
  {
    return;
  }

  const QVector3D eye = get_camera_pos();
  context_.camera_pos = oros::terrain::Vec3{eye.x(), eye.y(), eye.z()};

  // Recompute distance bands
  context_.lod_distances.clear();
  const uint32_t levels = quadtree_.get_max_lod_levels();
  float          base_dist = 60.0f * lod_dist_scale_;

  for (uint32_t i = 0; i < levels; ++i)
  {
    context_.lod_distances.push_back(base_dist);
    base_dist *= 2.0f;
  }

  instances_ = quadtree_.select_lod(*patch_, context_);

  if (initialized_)
  {
    update_camera();
    update_patch_renderables();
  }

  emit camera_moved(eye.x(), eye.y(), eye.z());
  emit selection_updated(instances_.size());
}

void CdlodFilamentWidget::regenerate_noise()
{
  generate_noise();
  create_patch();
}

void CdlodFilamentWidget::render_frame()
{
  if (!initialized_ || !renderer_ || !swap_chain_ || !view_)
  {
    return;
  }

  if (renderer_->beginFrame(swap_chain_))
  {
    renderer_->render(view_);
    renderer_->endFrame();
  }
}

void CdlodFilamentWidget::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);

  if (initialized_ && view_)
  {
    view_->setViewport(filament::Viewport(0, 0, width(), height()));
    update_camera();
  }
}

void CdlodFilamentWidget::set_grid_resolution(size_t resolution)
{
  if (grid_res_ != resolution)
  {
    grid_res_ = resolution;
    if (initialized_)
    {
      clear_all_nodes();
      update_shared_grid_mesh();
      recompute_lod();
    }
  }
}

void CdlodFilamentWidget::set_lod_distance_scale(float scale)
{
  lod_dist_scale_ = scale;
  recompute_lod();
}

void CdlodFilamentWidget::set_max_lod_levels(uint32_t levels)
{
  quadtree_.set_max_lod_levels(levels);
  recompute_lod();
}

void CdlodFilamentWidget::set_morphing_enabled(bool enabled)
{
  morphing_enabled_ = enabled;
  if (initialized_)
  {
    update_patch_renderables();
  }
}

void CdlodFilamentWidget::set_noise_frequency(float freq)
{
  noise_frequency_ = freq;
}

void CdlodFilamentWidget::set_shading_mode(bool enabled)
{
  shading_enabled_ = enabled;
  if (initialized_)
  {
    update_patch_renderables();
  }
}

void CdlodFilamentWidget::set_wireframe_mode(bool enabled)
{
  wireframe_mode_ = enabled;
  if (initialized_)
  {
    update_patch_renderables();
  }
}

void CdlodFilamentWidget::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);

  if (!initialized_)
  {
    init_filament();
  }
}

void CdlodFilamentWidget::update_camera()
{
  if (!camera_)
  {
    return;
  }

  const QVector3D eye = get_camera_pos();
  const float     aspect = static_cast<float>(width()) /
                       static_cast<float>(std::max(1, height()));

  camera_->setProjection(45.0,
                         aspect,
                         1.0,
                         5000.0,
                         filament::Camera::Fov::VERTICAL);
  camera_->lookAt(filament::math::float3(eye.x(), eye.y(), eye.z()),
                  filament::math::float3(camera_target_.x(),
                                         camera_target_.y(),
                                         camera_target_.z()),
                  filament::math::float3(0.0f, 0.0f, 1.0f));
}

void CdlodFilamentWidget::update_patch_renderables()
{
  if (!initialized_ || !engine_ || !shared_vb_ || instances_.empty())
  {
    return;
  }

  const QVector3D              eye = get_camera_pos();
  const filament::math::float3 cam_pos(eye.x(), eye.y(), eye.z());

  const bool          use_lit = shading_enabled_ && !wireframe_mode_;
  filament::Material *active_material = use_lit ? lit_material_
                                                : unlit_material_;
  if (!active_material)
  {
    return;
  }

  filament::IndexBuffer *active_ib = wireframe_mode_ ? shared_wire_ib_
                                                     : shared_solid_ib_;
  const uint32_t active_index_count = wireframe_mode_ ? wire_index_count_
                                                      : solid_index_count_;
  const filament::RenderableManager::PrimitiveType prim_type =
      wireframe_mode_ ? filament::RenderableManager::PrimitiveType::LINES
                      : filament::RenderableManager::PrimitiveType::TRIANGLES;

  filament::TextureSampler linear_sampler(
      filament::TextureSampler::MinFilter::LINEAR,
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

  auto &rm = engine_->getRenderableManager();

  // Expand node pool if needed
  if (node_pool_.size() < instances_.size())
  {
    const size_t prev_size = node_pool_.size();
    node_pool_.resize(instances_.size());

    for (size_t i = prev_size; i < node_pool_.size(); ++i)
    {
      auto &node = node_pool_[i];
      node.entity = utils::EntityManager::get().create();
      node.unlit_instance = unlit_material_->createInstance();
      node.lit_instance = lit_material_->createInstance();
      node.in_scene = false;

      // Build renderable with geometry linked to shared static buffers
      filament::RenderableManager::Builder(1)
          .boundingBox({{-world_size_, -world_size_, -height_scale_ * 2.0f},
                        {world_size_, world_size_, height_scale_ * 2.0f}})
          .material(0, use_lit ? node.lit_instance : node.unlit_instance)
          .geometry(0, prim_type, shared_vb_, active_ib, 0, active_index_count)
          .culling(false)
          .build(*engine_, node.entity);
    }
  }

  // Update parameters and scene visibility for all instances
  for (size_t i = 0; i < instances_.size(); ++i)
  {
    const auto &inst = instances_[i];
    auto       &node = node_pool_[i];

    auto renderable_instance = rm.getInstance(node.entity);
    if (renderable_instance)
    {
      rm.setMaterialInstanceAt(renderable_instance,
                               0,
                               use_lit ? node.lit_instance
                                       : node.unlit_instance);
      rm.setGeometryAt(renderable_instance,
                       0,
                       prim_type,
                       shared_vb_,
                       active_ib,
                       0,
                       active_index_count);

      // Compute precise world bounding box for this patch
      const float min_x = (inst.offset_u - 0.5f) * world_size_;
      const float max_x = (inst.offset_u + inst.scale_u - 0.5f) * world_size_;
      const float min_y = (inst.offset_v - 0.5f) * world_size_;
      const float max_y = (inst.offset_v + inst.scale_v - 0.5f) * world_size_;

      filament::Box patch_box;
      patch_box.set({min_x, min_y, -10.0f},
                    {max_x, max_y, height_scale_ * 1.5f});
      rm.setAxisAlignedBoundingBox(renderable_instance, patch_box);
    }

    filament::MaterialInstance *mi = use_lit ? node.lit_instance
                                             : node.unlit_instance;

    mi->setParameter("cameraPos", cam_pos);
    mi->setParameter("patchOffsetScale",
                     filament::math::float4(inst.offset_u,
                                            inst.offset_v,
                                            inst.scale_u,
                                            inst.scale_v));
    mi->setParameter("patchMorphLod",
                     filament::math::float4(inst.morph_start,
                                            inst.morph_range,
                                            static_cast<float>(inst.lod_level),
                                            0.0f));
    mi->setParameter("heightScale", height_scale_);
    mi->setParameter("worldScale",
                     filament::math::float2(world_size_, world_size_));
    mi->setParameter("gridDim", static_cast<float>(grid_res_));
    mi->setParameter("morphEnabled", morphing_enabled_);

    if (heightmap_texture_)
    {
      mi->setParameter("heightmap", heightmap_texture_, linear_sampler);
    }

    if (use_lit && normal_texture_)
    {
      mi->setParameter("normalMap", normal_texture_, linear_sampler);
    }

    if (!node.in_scene)
    {
      scene_->addEntity(node.entity);
      node.in_scene = true;
    }
  }

  // Remove excess pooled nodes from scene
  for (size_t i = instances_.size(); i < node_pool_.size(); ++i)
  {
    auto &node = node_pool_[i];
    if (node.in_scene)
    {
      scene_->remove(node.entity);
      node.in_scene = false;
    }
  }

  active_node_count_ = instances_.size();
}

void CdlodFilamentWidget::update_shared_grid_mesh()
{
  if (!engine_)
  {
    return;
  }

  if (shared_vb_)
  {
    engine_->destroy(shared_vb_);
    shared_vb_ = nullptr;
  }

  if (shared_solid_ib_)
  {
    engine_->destroy(shared_solid_ib_);
    shared_solid_ib_ = nullptr;
  }

  if (shared_wire_ib_)
  {
    engine_->destroy(shared_wire_ib_);
    shared_wire_ib_ = nullptr;
  }

  const auto grid = oros::terrain::GridMeshFactory::create_unit_grid(grid_res_);
  const size_t vertex_count = grid.vertices.size();

  // Convert Vertex2D to float3 (x, y, 0.0)
  std::vector<filament::math::float3> positions(vertex_count);
  for (size_t i = 0; i < vertex_count; ++i)
  {
    positions[i] = {grid.vertices[i].u, grid.vertices[i].v, 0.0f};
  }

  // Generate default flat tangents (T = (1, 0, 0), B = (0, 1, 0), N = (0, 0,
  // 1))
  filament::math::mat3f tbn;
  tbn[0] = filament::math::float3(1.0f, 0.0f, 0.0f);
  tbn[1] = filament::math::float3(0.0f, 1.0f, 0.0f);
  tbn[2] = filament::math::float3(0.0f, 0.0f, 1.0f);
  const filament::math::quatf flat_tangent =
      filament::math::mat3f::packTangentFrame(tbn);
  std::vector<filament::math::quatf> tangents(vertex_count, flat_tangent);

  shared_vb_ = filament::VertexBuffer::Builder()
                   .vertexCount(vertex_count)
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
                   .build(*engine_);

  auto *pos_copy = new std::vector<filament::math::float3>(
      std::move(positions));
  shared_vb_->setBufferAt(
      *engine_,
      0,
      filament::VertexBuffer::BufferDescriptor(
          pos_copy->data(),
          pos_copy->size() * sizeof(filament::math::float3),
          [](void *buffer, size_t size, void *user)
          { delete static_cast<std::vector<filament::math::float3> *>(user); },
          pos_copy));

  auto *tangents_copy = new std::vector<filament::math::quatf>(
      std::move(tangents));
  shared_vb_->setBufferAt(
      *engine_,
      1,
      filament::VertexBuffer::BufferDescriptor(
          tangents_copy->data(),
          tangents_copy->size() * sizeof(filament::math::quatf),
          [](void *buffer, size_t size, void *user)
          { delete static_cast<std::vector<filament::math::quatf> *>(user); },
          tangents_copy));

  // 1. Solid Index Buffer (TRIANGLES)
  solid_index_count_ = static_cast<uint32_t>(grid.indices.size());
  shared_solid_ib_ = filament::IndexBuffer::Builder()
                         .indexCount(solid_index_count_)
                         .bufferType(filament::IndexBuffer::IndexType::UINT)
                         .build(*engine_);

  auto *solid_copy = new std::vector<uint32_t>(grid.indices);
  shared_solid_ib_->setBuffer(
      *engine_,
      filament::IndexBuffer::BufferDescriptor(
          solid_copy->data(),
          solid_copy->size() * sizeof(uint32_t),
          [](void *buffer, size_t size, void *user)
          { delete static_cast<std::vector<uint32_t> *>(user); },
          solid_copy));

  // 2. Wireframe Index Buffer (LINES)
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

  wire_index_count_ = static_cast<uint32_t>(wire_indices.size());
  shared_wire_ib_ = filament::IndexBuffer::Builder()
                        .indexCount(wire_index_count_)
                        .bufferType(filament::IndexBuffer::IndexType::UINT)
                        .build(*engine_);

  auto *wire_copy = new std::vector<uint32_t>(std::move(wire_indices));
  shared_wire_ib_->setBuffer(
      *engine_,
      filament::IndexBuffer::BufferDescriptor(
          wire_copy->data(),
          wire_copy->size() * sizeof(uint32_t),
          [](void *buffer, size_t size, void *user)
          { delete static_cast<std::vector<uint32_t> *>(user); },
          wire_copy));
}

void CdlodFilamentWidget::update_textures()
{
  if (!engine_ || !heightmap_data_)
  {
    return;
  }

  if (heightmap_texture_)
  {
    engine_->destroy(heightmap_texture_);
    heightmap_texture_ = nullptr;
  }

  if (normal_texture_)
  {
    engine_->destroy(normal_texture_);
    normal_texture_ = nullptr;
  }

  // Upload Heightmap (R32F)
  heightmap_texture_ = filament::Texture::Builder()
                           .width(static_cast<uint32_t>(heightmap_res_))
                           .height(static_cast<uint32_t>(heightmap_res_))
                           .levels(1)
                           .sampler(filament::Texture::Sampler::SAMPLER_2D)
                           .format(filament::Texture::InternalFormat::R32F)
                           .build(*engine_);

  auto *h_copy = new std::vector<float>(heightmap_data_->get_data());
  filament::Texture::PixelBufferDescriptor h_buf(
      h_copy->data(),
      h_copy->size() * sizeof(float),
      filament::Texture::Format::R,
      filament::Texture::Type::FLOAT,
      [](void *buffer, size_t size, void *user)
      { delete static_cast<std::vector<float> *>(user); },
      h_copy);
  heightmap_texture_->setImage(*engine_, 0, std::move(h_buf));

  // Upload Normal Map (RGB8)
  if (!normal_map_data_.empty())
  {
    normal_texture_ = filament::Texture::Builder()
                          .width(static_cast<uint32_t>(heightmap_res_))
                          .height(static_cast<uint32_t>(heightmap_res_))
                          .levels(1)
                          .sampler(filament::Texture::Sampler::SAMPLER_2D)
                          .format(filament::Texture::InternalFormat::RGB8)
                          .build(*engine_);

    auto *n_copy = new std::vector<uint8_t>(normal_map_data_);
    filament::Texture::PixelBufferDescriptor n_buf(
        n_copy->data(),
        n_copy->size(),
        filament::Texture::Format::RGB,
        filament::Texture::Type::UBYTE,
        [](void *buffer, size_t size, void *user)
        { delete static_cast<std::vector<uint8_t> *>(user); },
        n_copy);
    normal_texture_->setImage(*engine_, 0, std::move(n_buf));
  }
}

void CdlodFilamentWidget::wheelEvent(QWheelEvent *event)
{
  const float delta = static_cast<float>(event->angleDelta().y());
  camera_distance_ *= (delta > 0.0f) ? 0.9f : 1.1f;
  camera_distance_ = std::clamp(camera_distance_, 10.0f, 3000.0f);
  recompute_lod();
}

} // namespace oros::ui
