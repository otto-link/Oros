/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <algorithm>
#include <cmath>

#include "cdlod_view_widget.hpp"

#include <FastNoiseLite.h>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>

namespace oros::ui
{

namespace
{

// Palette for LOD levels (finest to coarsest)
const std::vector<QColor> LOD_COLORS = {
    QColor(0, 230, 118),  // Level 0 (Finest): Bright green
    QColor(255, 235, 59), // Level 1: Yellow
    QColor(255, 152, 0),  // Level 2: Orange
    QColor(244, 67, 54),  // Level 3: Red
    QColor(156, 39, 176), // Level 4: Purple
    QColor(33, 150, 243), // Level 5: Blue
    QColor(0, 188, 212),  // Level 6: Cyan
    QColor(255, 255, 255) // Level 7+: White
};

QColor get_lod_color(uint32_t lod_level)
{
  if (lod_level < LOD_COLORS.size())
  {
    return LOD_COLORS[lod_level];
  }

  return LOD_COLORS.back();
}

} // namespace

CdlodViewWidget::CdlodViewWidget(QWidget *parent) : QWidget(parent)
{
  setMinimumSize(400, 400);
  setMouseTracking(true);

  // default camera position centered above patch
  context_.camera_pos = oros::terrain::Vec3{0.0f, 0.0f, camera_z_};
  context_.morph_ratio = 0.66f;

  regenerate_noise();
}

oros::terrain::Vec3 CdlodViewWidget::get_camera_pos() const noexcept
{
  return context_.camera_pos;
}

size_t CdlodViewWidget::get_instance_count() const noexcept
{
  return instances_.size();
}

void CdlodViewWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (event->buttons() & Qt::LeftButton)
  {
    update_camera_from_widget_coords(event->position());
  }
}

void CdlodViewWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    update_camera_from_widget_coords(event->position());
  }
}

void CdlodViewWidget::paintEvent(QPaintEvent * /*event*/)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const int w = width();
  const int h = height();
  const int side = std::min(w, h);
  const int offset_x = (w - side) / 2;
  const int offset_y = (h - side) / 2;

  const QRect patch_rect(offset_x, offset_y, side, side);

  // fill background
  painter.fillRect(rect(), QColor(24, 24, 28));

  // draw heightmap texture
  if (!background_image_.isNull())
  {
    painter.drawImage(patch_rect, background_image_);
  }

  // draw quadtree nodes
  for (const auto &inst : instances_)
  {
    const float node_px_x = offset_x + inst.offset_u * side;
    const float node_px_y = offset_y + inst.offset_v * side;
    const float node_px_w = inst.scale_u * side;
    const float node_px_h = inst.scale_v * side;

    const QRectF node_rect(node_px_x, node_px_y, node_px_w, node_px_h);

    const QColor color = get_lod_color(inst.lod_level);

    // semi-transparent fill
    QColor fill_color = color;
    fill_color.setAlpha(35);
    painter.fillRect(node_rect, fill_color);

    // colored border
    QPen pen(color, 1.5f);
    painter.setPen(pen);
    painter.drawRect(node_rect);

    // draw LOD level text if node is large enough
    if (node_px_w > 28.0f && node_px_h > 20.0f)
    {
      painter.setPen(color);
      QFont font = painter.font();
      font.setPointSize(9);
      font.setBold(true);
      painter.setFont(font);
      painter.drawText(node_rect.adjusted(4, 3, 0, 0),
                       Qt::AlignTop | Qt::AlignLeft,
                       QString("L%1").arg(inst.lod_level));
    }
  }

  // draw outer border
  painter.setPen(QPen(QColor(180, 180, 190), 2.0f));
  painter.drawRect(patch_rect);

  // draw camera position marker
  const float norm_cam_u = (context_.camera_pos.x / world_size_) + 0.5f;
  const float norm_cam_v = (context_.camera_pos.y / world_size_) + 0.5f;

  const float cam_px_x = offset_x + norm_cam_u * side;
  const float cam_px_y = offset_y + norm_cam_v * side;

  // camera crosshair
  QPen cam_pen(QColor(255, 255, 255), 2.0f);
  painter.setPen(cam_pen);

  painter.drawLine(QPointF(cam_px_x - 10.0f, cam_px_y),
                   QPointF(cam_px_x + 10.0f, cam_px_y));
  painter.drawLine(QPointF(cam_px_x, cam_px_y - 10.0f),
                   QPointF(cam_px_x, cam_px_y + 10.0f));

  painter.setBrush(QColor(255, 50, 50));
  painter.drawEllipse(QPointF(cam_px_x, cam_px_y), 4.0f, 4.0f);

  // draw info overlay text
  painter.setPen(QColor(240, 240, 240));
  QFont info_font = painter.font();
  info_font.setPointSize(10);
  info_font.setBold(false);
  painter.setFont(info_font);

  const QString info_str = QString(
                               "Camera: (%1, %2, %3) | Quads: %4 | Max LOD: %5")
                               .arg(context_.camera_pos.x, 0, 'f', 1)
                               .arg(context_.camera_pos.y, 0, 'f', 1)
                               .arg(context_.camera_pos.z, 0, 'f', 1)
                               .arg(instances_.size())
                               .arg(quadtree_.get_max_lod_levels());

  painter.drawText(15, 25, info_str);
}

void CdlodViewWidget::recompute_lod()
{
  if (!patch_)
  {
    return;
  }

  // configure LOD distances scaled by factor
  // e.g. base distances [80, 160, 320, 640, 1280, 2560]
  std::vector<float> lod_distances;
  float              base_dist = 75.0f * lod_dist_scale_;

  for (uint32_t i = 0; i < quadtree_.get_max_lod_levels(); ++i)
  {
    lod_distances.push_back(base_dist);
    base_dist *= 2.0f;
  }

  context_.lod_distances = lod_distances;
  instances_ = quadtree_.select_lod(*patch_, context_);

  emit selection_updated(instances_.size());
  update();
}

void CdlodViewWidget::regenerate_noise()
{
  FastNoiseLite noise;
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFrequency(noise_frequency_);
  noise.SetFractalType(FastNoiseLite::FractalType_FBm);
  noise.SetFractalOctaves(4);

  std::vector<float> elevations(heightmap_res_ * heightmap_res_);

  const float max_elevation = 150.0f;

  for (size_t y = 0; y < heightmap_res_; ++y)
  {
    for (size_t x = 0; x < heightmap_res_; ++x)
    {
      const float nx = static_cast<float>(x);
      const float ny = static_cast<float>(y);

      // FastNoiseLite returns values in [-1, 1]
      const float n_val = noise.GetNoise(nx, ny);
      const float normalized = (n_val + 1.0f) * 0.5f;

      elevations[y * heightmap_res_ + x] = normalized * max_elevation;
    }
  }

  heightmap_data_ = std::make_shared<oros::terrain::HeightmapData>(
      heightmap_res_,
      heightmap_res_,
      elevations);

  patch_ = std::make_unique<oros::terrain::HeightmapPatch>(
      "cdlod_test_patch",
      heightmap_data_,
      oros::terrain::Vec3{0.0f, 0.0f, 0.0f},
      world_size_,
      world_size_,
      0.0f);

  render_background_image();
  recompute_lod();
}

void CdlodViewWidget::render_background_image()
{
  if (!heightmap_data_)
  {
    return;
  }

  const size_t w = heightmap_data_->get_width();
  const size_t h = heightmap_data_->get_height();

  background_image_ = QImage(static_cast<int>(w),
                             static_cast<int>(h),
                             QImage::Format_RGB32);

  const auto [min_z, max_z] = heightmap_data_->get_elevation_range();
  const float range = (max_z - min_z > 0.001f) ? (max_z - min_z) : 1.0f;

  for (size_t y = 0; y < h; ++y)
  {
    auto *line = reinterpret_cast<QRgb *>(background_image_.scanLine(y));

    for (size_t x = 0; x < w; ++x)
    {
      const float val = heightmap_data_->get_height_at(x, y);
      const float norm = std::clamp((val - min_z) / range, 0.0f, 1.0f);

      // Dark earthy colormap (dark slate blue to green to sandy ochre)
      const int r = static_cast<int>(norm * 180.0f + 25.0f);
      const int g = static_cast<int>(norm * 200.0f + 35.0f);
      const int b = static_cast<int>(norm * 130.0f + 45.0f);

      line[x] = qRgb(r, g, b);
    }
  }
}

void CdlodViewWidget::set_camera_z(float z)
{
  camera_z_ = z;
  context_.camera_pos.z = camera_z_;

  recompute_lod();
  emit camera_moved(context_.camera_pos.x,
                    context_.camera_pos.y,
                    context_.camera_pos.z);
}

void CdlodViewWidget::set_lod_distance_scale(float scale)
{
  lod_dist_scale_ = scale;
  recompute_lod();
}

void CdlodViewWidget::set_max_lod_levels(uint32_t levels)
{
  quadtree_.set_max_lod_levels(levels);
  recompute_lod();
}

void CdlodViewWidget::set_noise_frequency(float freq)
{
  noise_frequency_ = freq;
  regenerate_noise();
}

void CdlodViewWidget::update_camera_from_widget_coords(const QPointF &pos)
{
  const int w = width();
  const int h = height();
  const int side = std::min(w, h);
  const int offset_x = (w - side) / 2;
  const int offset_y = (h - side) / 2;

  // convert pixel pos to normalized patch UV (unclamped to allow reference
  // point outside bounding box)
  const float u = static_cast<float>(pos.x() - offset_x) /
                  static_cast<float>(side);
  const float v = static_cast<float>(pos.y() - offset_y) /
                  static_cast<float>(side);

  // convert [u, v] to centered patch world coords [-world_size/2, world_size/2]
  context_.camera_pos.x = (u - 0.5f) * world_size_;
  context_.camera_pos.y = (v - 0.5f) * world_size_;

  recompute_lod();
  emit camera_moved(context_.camera_pos.x,
                    context_.camera_pos.y,
                    context_.camera_pos.z);
}

} // namespace oros::ui
