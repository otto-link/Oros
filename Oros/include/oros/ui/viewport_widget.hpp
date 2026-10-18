#pragma once

#include <QColor>
#include <QPoint>
#include <QWidget>

namespace oros::ui
{

// --- ViewportWidget class

class ViewportWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ViewportWidget(QWidget *parent = nullptr);
  ~ViewportWidget() override = default;

  [[nodiscard]] const QColor &get_background_color() const;
  void                        set_background_color(const QColor &color);

  [[nodiscard]] float get_fps() const;
  [[nodiscard]] float get_frame_time_ms() const;

signals:
  void fps_updated(float fps, float frame_time_ms);

protected:
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  QColor background_color_{30, 30, 35};
  float  current_fps_{0.0f};
  float  current_frame_time_ms_{0.0f};
  bool   is_dragging_{false};
  QPoint last_mouse_pos_{};
};

} // namespace oros::ui
