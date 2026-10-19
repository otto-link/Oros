/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <memory>
#include <vector>

#include "oros/terrain/cdlod_quadtree.hpp"
#include "oros/terrain/heightmap_data.hpp"
#include "oros/terrain/heightmap_patch.hpp"

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QWidget>

namespace oros::ui
{

// --- CdlodViewWidget
// 2D visual testing widget for CDLOD quadtree selection.

class CdlodViewWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CdlodViewWidget(QWidget *parent = nullptr);

  [[nodiscard]] oros::terrain::Vec3 get_camera_pos() const noexcept;

  [[nodiscard]] size_t get_instance_count() const noexcept;

  void regenerate_noise();

  void set_camera_z(float z);

  void set_lod_distance_scale(float scale);

  void set_max_lod_levels(uint32_t levels);

  void set_noise_frequency(float freq);

signals:
  void camera_moved(float x, float y, float z);

  void selection_updated(size_t node_count);

protected:
  void mouseMoveEvent(QMouseEvent *event) override;

  void mousePressEvent(QMouseEvent *event) override;

  void paintEvent(QPaintEvent *event) override;

private:
  void recompute_lod();

  void render_background_image();

  void update_camera_from_widget_coords(const QPointF &pos);

  size_t heightmap_res_{256};
  float  noise_frequency_{0.015f};

  float world_size_{1000.0f};
  float camera_z_{100.0f};
  float lod_dist_scale_{1.0f};

  std::shared_ptr<oros::terrain::HeightmapData>  heightmap_data_{nullptr};
  std::unique_ptr<oros::terrain::HeightmapPatch> patch_{nullptr};
  oros::terrain::CDLODQuadTree                   quadtree_{6};
  oros::terrain::CDLODSelectionContext           context_{};

  std::vector<oros::terrain::CDLODInstance> instances_{};

  QImage background_image_{};
};

} // namespace oros::ui
