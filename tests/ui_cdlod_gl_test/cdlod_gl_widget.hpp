/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <memory>
#include <vector>

#include "oros/terrain/cdlod_quadtree.hpp"
#include "oros/terrain/heightmap_data.hpp"
#include "oros/terrain/heightmap_patch.hpp"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QWheelEvent>

namespace oros::ui
{

// --- CdlodGlWidget
// 3D OpenGL visual testing widget for CDLOD wireframe terrain rendering.

class CdlodGlWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
  Q_OBJECT

public:
  explicit CdlodGlWidget(QWidget *parent = nullptr);
  ~CdlodGlWidget() override;

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
  void initializeGL() override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void paintGL() override;
  void resizeGL(int w, int h) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  void init_shaders();
  void recompute_lod();
  void update_grid_mesh();
  void update_heightmap_texture();
  void update_instance_buffer();

  // Terrain generation parameters
  size_t heightmap_res_{4096 * 2};
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

  // OpenGL resources
  std::unique_ptr<QOpenGLShaderProgram>     shader_program_{nullptr};
  std::unique_ptr<QOpenGLVertexArrayObject> vao_{nullptr};
  std::unique_ptr<QOpenGLBuffer>            vbo_grid_{nullptr};
  std::unique_ptr<QOpenGLBuffer>            ebo_grid_{nullptr};
  std::unique_ptr<QOpenGLBuffer>            vbo_instances_{nullptr};
  std::unique_ptr<QOpenGLTexture>           heightmap_texture_{nullptr};
  std::unique_ptr<QOpenGLTexture>           normal_texture_{nullptr};
  std::vector<uint8_t>                      normal_map_data_{};
  size_t                                    index_count_{0};
};

} // namespace oros::ui
