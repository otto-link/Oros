/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <oros/ui/viewport_widget.hpp>

namespace oros::ui
{

// --- Construction

ViewportWidget::ViewportWidget(QWidget *parent) : QWidget(parent)
{
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
}

// --- Properties

const QColor &ViewportWidget::get_background_color() const
{
  return background_color_;
}

void ViewportWidget::set_background_color(const QColor &color)
{
  background_color_ = color;
  update();
}

float ViewportWidget::get_fps() const
{
  return current_fps_;
}

float ViewportWidget::get_frame_time_ms() const
{
  return current_frame_time_ms_;
}

// --- Protected Qt events

void ViewportWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (is_dragging_)
  {
    QPoint delta = event->pos() - last_mouse_pos_;
    last_mouse_pos_ = event->pos();

    // placeholder hook for camera rotation or panning
    Q_UNUSED(delta);
    update();
  }

  QWidget::mouseMoveEvent(event);
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
  {
    is_dragging_ = true;
    last_mouse_pos_ = event->pos();
  }

  QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
  {
    is_dragging_ = false;
  }

  QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);

  QPainter painter(this);
  painter.fillRect(rect(), background_color_);

  // draw temporary 3D placeholder grid / text
  painter.setPen(QColor(120, 120, 130));
  painter.drawText(rect(), Qt::AlignCenter, "Oros 3D Viewport");
}

void ViewportWidget::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
  // placeholder hook for camera zoom
  update();

  QWidget::wheelEvent(event);
}

} // namespace oros::ui
