/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "FastNoiseLite.h"

#include "cdlod_gl_widget.hpp"
#include "oros/logger.hpp"
#include "oros/terrain/grid_mesh_factory.hpp"

#include <QCoreApplication>

namespace oros::ui
{

// --- Shaders

static const char *k_vertex_shader_source = R"(#version 330 core
layout(location = 0) in vec2 in_grid_pos;

// Instance attributes
layout(location = 1) in vec4 in_instance_offset_scale; // offset_u, offset_v, scale_u, scale_v
layout(location = 2) in vec4 in_instance_morph_lod;    // morph_start, morph_range, lod_level, unused

uniform mat4 u_mvp;
uniform vec3 u_camera_pos;
uniform vec2 u_world_scale; // world_size_x, world_size_y
uniform float u_height_scale;
uniform float u_grid_dim;   // e.g. 33.0
uniform int u_morph_enabled;

uniform sampler2D u_heightmap;

out float v_lod;
out float v_morph;
out vec2 v_uv;
out vec3 v_normal;
out vec3 v_world_pos;

void main()
{
  vec2 patch_offset = in_instance_offset_scale.xy;
  vec2 patch_scale  = in_instance_offset_scale.zw;
  float morph_start = in_instance_morph_lod.x;
  float morph_range = in_instance_morph_lod.y;
  float lod_level   = in_instance_morph_lod.z;

  // Local grid position in [0, 1]
  vec2 local_pos = in_grid_pos;

  // World position before morphing
  vec2 uv = patch_offset + local_pos * patch_scale;
  vec2 world_xy = (uv - vec2(0.5)) * u_world_scale;

  // Sample height for 3D distance calculation
  float h_sample = texture(u_heightmap, uv).r * u_height_scale;
  float dist = length(vec3(world_xy.x, world_xy.y, h_sample) - u_camera_pos);

  float morph_alpha = 0.0;
  if (u_morph_enabled != 0 && morph_range > 0.001)
  {
    morph_alpha = clamp((dist - morph_start) / morph_range, 0.0, 1.0);
  }

  // Coarse UV target (collapses odd vertices into even vertices)
  float mesh_quads = u_grid_dim - 1.0;
  vec2 frac_uv = fract(local_pos * (mesh_quads * 0.5)) / (mesh_quads * 0.5);
  vec2 uv_coarse = uv - frac_uv * patch_scale;

  // Morph coordinates and heights together to maintain C0 continuity
  vec2 uv_morphed = mix(uv, uv_coarse, morph_alpha);
  vec2 world_xy_morphed = (uv_morphed - vec2(0.5)) * u_world_scale;
  float height = texture(u_heightmap, uv_morphed).r * u_height_scale;

  v_lod = lod_level;
  v_morph = morph_alpha;
  v_uv = uv_morphed;
  v_world_pos = vec3(world_xy_morphed.x, world_xy_morphed.y, height);

  gl_Position = u_mvp * vec4(v_world_pos, 1.0);
}
)";

static const char *k_fragment_shader_source = R"(#version 330 core
in float v_lod;
in float v_morph;
in vec2 v_uv;
in vec3 v_world_pos;

uniform int u_shading_enabled;
uniform vec3 u_camera_pos;
uniform sampler2D u_normal_map;

out vec4 frag_color;

// Color palette for LOD visualization (Dracula theme, green is finest)
vec3 get_lod_color(int lod)
{
  if (lod == 0) return vec3(0.314, 0.980, 0.482); // Dracula Green  (#50fa7b)
  if (lod == 1) return vec3(0.545, 0.914, 0.992); // Dracula Cyan   (#8be9fd)
  if (lod == 2) return vec3(0.945, 0.980, 0.549); // Dracula Yellow (#f1fa8c)
  if (lod == 3) return vec3(1.000, 0.722, 0.424); // Dracula Orange (#ffb86c)
  if (lod == 4) return vec3(1.000, 0.475, 0.776); // Dracula Pink   (#ff79c6)
  if (lod == 5) return vec3(0.741, 0.576, 0.976); // Dracula Purple (#bd93f9)
  if (lod == 6) return vec3(1.000, 0.333, 0.333); // Dracula Red    (#ff5555)
  return vec3(0.384, 0.447, 0.643);                // Dracula Comment(#6272a4)
}

void main()
{
  int lod = int(round(v_lod));
  vec3 base_color = get_lod_color(lod);

  if (u_shading_enabled != 0)
  {
    // High-frequency detail normal sampled from the baked normal map
    vec3 n_tex = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;
    vec3 n = normalize(n_tex);

    // Directional light from top-front-right
    vec3 light_dir = normalize(vec3(0.5, 0.3, 0.8));

    float diff = max(dot(n, light_dir), 0.0);
    float ambient = 0.25;

    // Subtle specular highlight
    vec3 view_dir = normalize(u_camera_pos - v_world_pos);
    vec3 half_dir = normalize(light_dir + view_dir);
    float spec = pow(max(dot(n, half_dir), 0.0), 16.0) * 0.15;

    vec3 shaded = base_color * (ambient + diff * 0.75) + vec3(spec);
    frag_color = vec4(shaded, 1.0);
  }
  else
  {
    frag_color = vec4(base_color, 1.0);
  }
}
)";

// --- Implementation

CdlodGlWidget::CdlodGlWidget(QWidget *parent) : QOpenGLWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);

  // Initialize CDLOD context
  context_.camera_pos = oros::terrain::Vec3{0.0f, 0.0f, 200.0f};
  context_.morph_ratio = 0.66f;

  // Initialize quadtree
  quadtree_.set_max_lod_levels(8);

  // Generate initial heightmap patch
  regenerate_noise();
}

CdlodGlWidget::~CdlodGlWidget()
{
  makeCurrent();
  vbo_grid_.reset();
  ebo_grid_.reset();
  vbo_instances_.reset();
  vao_.reset();
  heightmap_texture_.reset();
  shader_program_.reset();
  doneCurrent();
}

QVector3D CdlodGlWidget::get_camera_pos() const noexcept
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

size_t CdlodGlWidget::get_instance_count() const noexcept
{
  return instances_.size();
}

void CdlodGlWidget::initializeGL()
{
  initializeOpenGLFunctions();

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  glEnable(GL_POLYGON_OFFSET_LINE);
  glPolygonOffset(-1.0f, -1.0f);

  // Dracula Background (#282a36)
  glClearColor(0.157f, 0.165f, 0.212f, 1.0f);

  init_shaders();
  update_grid_mesh();
  update_heightmap_texture();
  recompute_lod();
}

void CdlodGlWidget::init_shaders()
{
  shader_program_ = std::make_unique<QOpenGLShaderProgram>();
  shader_program_->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           k_vertex_shader_source);
  shader_program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           k_fragment_shader_source);
  shader_program_->link();
}

void CdlodGlWidget::mouseMoveEvent(QMouseEvent *event)
{
  const QPoint delta = event->pos() - last_mouse_pos_;
  last_mouse_pos_ = event->pos();

  if (event->buttons() & Qt::LeftButton)
  {
    camera_yaw_ -= static_cast<float>(delta.x()) * 0.4f;
    camera_pitch_ += static_cast<float>(delta.y()) * 0.4f;
    camera_pitch_ = std::clamp(camera_pitch_, -89.0f, 89.0f);
    recompute_lod();
    update();
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
    update();
  }
}

void CdlodGlWidget::mousePressEvent(QMouseEvent *event)
{
  last_mouse_pos_ = event->pos();
}

void CdlodGlWidget::paintGL()
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!shader_program_ || instances_.empty())
  {
    return;
  }

  if (wireframe_mode_)
  {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
  else
  {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  shader_program_->bind();

  // Matrices
  QMatrix4x4 projection;
  projection.perspective(45.0f,
                         static_cast<float>(width()) /
                             static_cast<float>(height()),
                         1.0f,
                         5000.0f);

  const QVector3D eye = get_camera_pos();
  QMatrix4x4      view;
  view.lookAt(eye, camera_target_, QVector3D(0.0f, 0.0f, 1.0f));

  const QMatrix4x4 mvp = projection * view;
  shader_program_->setUniformValue("u_mvp", mvp);
  shader_program_->setUniformValue("u_camera_pos", eye);
  shader_program_->setUniformValue("u_world_scale",
                                   QVector2D(world_size_, world_size_));
  shader_program_->setUniformValue("u_height_scale", height_scale_);
  shader_program_->setUniformValue("u_grid_dim", static_cast<float>(grid_res_));
  shader_program_->setUniformValue("u_morph_enabled",
                                   morphing_enabled_ ? 1 : 0);
  shader_program_->setUniformValue("u_shading_enabled",
                                   shading_enabled_ ? 1 : 0);

  // Bind heightmap texture
  if (heightmap_texture_)
  {
    heightmap_texture_->bind(0);
    shader_program_->setUniformValue("u_heightmap", 0);
  }

  // Bind normal map texture
  if (normal_texture_)
  {
    normal_texture_->bind(1);
    shader_program_->setUniformValue("u_normal_map", 1);
  }

  // Draw instanced mesh
  vao_->bind();
  glDrawElementsInstanced(GL_TRIANGLES,
                          static_cast<GLsizei>(index_count_),
                          GL_UNSIGNED_INT,
                          nullptr,
                          static_cast<GLsizei>(instances_.size()));
  vao_->release();

  shader_program_->release();

  // Reset polygon mode for any subsequent Qt internal painting
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void CdlodGlWidget::recompute_lod()
{
  if (!patch_)
  {
    return;
  }

  const auto start_time = std::chrono::steady_clock::now();

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

  const auto select_start = std::chrono::steady_clock::now();
  instances_ = quadtree_.select_lod(*patch_, context_);
  const auto select_end = std::chrono::steady_clock::now();

  const auto buffer_start = std::chrono::steady_clock::now();
  update_instance_buffer();
  const auto buffer_end = std::chrono::steady_clock::now();

  const auto total_end = std::chrono::steady_clock::now();

  const double select_us = std::chrono::duration<double, std::micro>(
                               select_end - select_start)
                               .count();
  const double buffer_us = std::chrono::duration<double, std::micro>(
                               buffer_end - buffer_start)
                               .count();
  const double total_us =
      std::chrono::duration<double, std::micro>(total_end - start_time).count();

  // oros::Logger::log()->info(
  //     "CDLOD Selection: {} instances | select: {:.1f} µs | GPU upload: {:.1f}
  //     µs | total: {:.1f} µs", instances_.size(), select_us, buffer_us,
  //     total_us);

  emit camera_moved(eye.x(), eye.y(), eye.z());
  emit selection_updated(instances_.size());
}

void CdlodGlWidget::create_patch()
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

  if (isValid())
  {
    makeCurrent();
    update_heightmap_texture();
    doneCurrent();
    recompute_lod();
    update();
  }
}

void CdlodGlWidget::generate_noise()
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
            // Thread-local noise generator instance
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
  } // All std::jthread join automatically upon scope exit

  QCoreApplication::processEvents();

  const auto noise_end = std::chrono::steady_clock::now();

  heightmap_data_ = std::make_shared<oros::terrain::HeightmapData>(
      heightmap_res_,
      heightmap_res_,
      std::move(heights));

  QCoreApplication::processEvents();

  const auto pyramid_end = std::chrono::steady_clock::now();

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
  const double pyramid_ms = std::chrono::duration<double, std::milli>(
                                pyramid_end - noise_end)
                                .count();
  const double normal_ms = std::chrono::duration<double, std::milli>(
                               normal_end - normal_start)
                               .count();

  oros::Logger::log()->info(
      "Noise generation: {}x{} ({} threads) | noise: {:.1f} ms | pyramid: "
      "{:.1f} ms | normal map: {:.1f} ms | total: {:.1f} ms",
      heightmap_res_,
      heightmap_res_,
      num_threads,
      noise_ms,
      pyramid_ms,
      normal_ms,
      noise_ms + pyramid_ms + normal_ms);
}

void CdlodGlWidget::regenerate_noise()
{
  generate_noise();
  create_patch();
}

void CdlodGlWidget::resizeGL(int w, int h)
{
  glViewport(0, 0, w, h);
}

void CdlodGlWidget::set_grid_resolution(size_t resolution)
{
  if (grid_res_ != resolution)
  {
    grid_res_ = resolution;
    if (isValid())
    {
      makeCurrent();
      update_grid_mesh();
      doneCurrent();
      update();
    }
  }
}

void CdlodGlWidget::set_lod_distance_scale(float scale)
{
  lod_dist_scale_ = scale;
  recompute_lod();
  update();
}

void CdlodGlWidget::set_max_lod_levels(uint32_t levels)
{
  quadtree_.set_max_lod_levels(levels);
  recompute_lod();
  update();
}

void CdlodGlWidget::set_morphing_enabled(bool enabled)
{
  morphing_enabled_ = enabled;
  update();
}

void CdlodGlWidget::set_noise_frequency(float freq)
{
  noise_frequency_ = freq;
}

void CdlodGlWidget::set_shading_mode(bool enabled)
{
  shading_enabled_ = enabled;
  update();
}

void CdlodGlWidget::set_wireframe_mode(bool enabled)
{
  wireframe_mode_ = enabled;
  update();
}

void CdlodGlWidget::update_grid_mesh()
{
  const auto grid = oros::terrain::GridMeshFactory::create_unit_grid(grid_res_);
  index_count_ = grid.indices.size();

  if (!vao_)
  {
    vao_ = std::make_unique<QOpenGLVertexArrayObject>();
    vao_->create();
  }
  vao_->bind();

  // Grid vertex buffer (2D positions in [0, 1])
  if (!vbo_grid_)
  {
    vbo_grid_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    vbo_grid_->create();
  }
  vbo_grid_->bind();
  vbo_grid_->allocate(
      grid.vertices.data(),
      static_cast<int>(grid.vertices.size() * sizeof(oros::terrain::Vertex2D)));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        sizeof(oros::terrain::Vertex2D),
                        nullptr);

  // Grid index buffer
  if (!ebo_grid_)
  {
    ebo_grid_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
    ebo_grid_->create();
  }
  ebo_grid_->bind();
  ebo_grid_->allocate(grid.indices.data(),
                      static_cast<int>(grid.indices.size() * sizeof(uint32_t)));

  // Instance buffer
  if (!vbo_instances_)
  {
    vbo_instances_ = std::make_unique<QOpenGLBuffer>(
        QOpenGLBuffer::VertexBuffer);
    vbo_instances_->create();
  }
  vbo_instances_->bind();

  // Attrib 1: vec4 in_instance_offset_scale (offset_u, offset_v, scale_u,
  // scale_v)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        4,
                        GL_FLOAT,
                        GL_FALSE,
                        8 * sizeof(float),
                        reinterpret_cast<void *>(0));
  glVertexAttribDivisor(1, 1);

  // Attrib 2: vec4 in_instance_morph_lod (morph_start, morph_range, lod_level,
  // 0.0)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2,
                        4,
                        GL_FLOAT,
                        GL_FALSE,
                        8 * sizeof(float),
                        reinterpret_cast<void *>(4 * sizeof(float)));
  glVertexAttribDivisor(2, 1);

  vao_->release();
  vbo_grid_->release();
  vbo_instances_->release();
}

void CdlodGlWidget::update_heightmap_texture()
{
  if (!heightmap_data_)
  {
    return;
  }

  heightmap_texture_ = std::make_unique<QOpenGLTexture>(
      QOpenGLTexture::Target2D);
  heightmap_texture_->setFormat(QOpenGLTexture::R32F);
  heightmap_texture_->setSize(static_cast<int>(heightmap_res_),
                              static_cast<int>(heightmap_res_));
  heightmap_texture_->allocateStorage(QOpenGLTexture::Red,
                                      QOpenGLTexture::Float32);

  heightmap_texture_->setData(QOpenGLTexture::Red,
                              QOpenGLTexture::Float32,
                              heightmap_data_->get_data().data());

  heightmap_texture_->setMinificationFilter(QOpenGLTexture::Linear);
  heightmap_texture_->setMagnificationFilter(QOpenGLTexture::Linear);
  heightmap_texture_->setWrapMode(QOpenGLTexture::ClampToEdge);

  if (!normal_map_data_.empty())
  {
    normal_texture_ = std::make_unique<QOpenGLTexture>(
        QOpenGLTexture::Target2D);
    normal_texture_->setFormat(QOpenGLTexture::RGB8_UNorm);
    normal_texture_->setSize(static_cast<int>(heightmap_res_),
                             static_cast<int>(heightmap_res_));
    normal_texture_->allocateStorage(QOpenGLTexture::RGB,
                                     QOpenGLTexture::UInt8);

    normal_texture_->setData(QOpenGLTexture::RGB,
                             QOpenGLTexture::UInt8,
                             normal_map_data_.data());

    normal_texture_->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    normal_texture_->setMagnificationFilter(QOpenGLTexture::Linear);
    normal_texture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    normal_texture_->generateMipMaps();
  }
}

void CdlodGlWidget::update_instance_buffer()
{
  if (!vbo_instances_ || instances_.empty())
  {
    return;
  }

  // Pack instances into 8 floats each
  std::vector<float> packed_data;
  packed_data.reserve(instances_.size() * 8);

  for (const auto &inst : instances_)
  {
    packed_data.push_back(inst.offset_u);
    packed_data.push_back(inst.offset_v);
    packed_data.push_back(inst.scale_u);
    packed_data.push_back(inst.scale_v);

    packed_data.push_back(inst.morph_start);
    packed_data.push_back(inst.morph_range);
    packed_data.push_back(static_cast<float>(inst.lod_level));
    packed_data.push_back(0.0f); // padding
  }

  vbo_instances_->bind();
  vbo_instances_->allocate(
      packed_data.data(),
      static_cast<int>(packed_data.size() * sizeof(float)));
  vbo_instances_->release();
}

void CdlodGlWidget::wheelEvent(QWheelEvent *event)
{
  const float delta = static_cast<float>(event->angleDelta().y());
  camera_distance_ *= (delta > 0.0f) ? 0.9f : 1.1f;
  camera_distance_ = std::clamp(camera_distance_, 10.0f, 3000.0f);
  recompute_lod();
  update();
}

} // namespace oros::ui
