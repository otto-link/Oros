/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include "cdlod_view_widget.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <oros/logger.hpp>
#include <oros/oros.hpp>

// --- Entry point

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  oros::Logger::log()->info("Starting CDLOD Visual Test (version {})...",
                            oros::get_version());

  QMainWindow main_window;
  main_window.setWindowTitle("Oros - CDLOD Quadtree Visual Test");
  main_window.resize(900, 750);

  auto *central_widget = new QWidget(&main_window);
  auto *root_layout = new QHBoxLayout(central_widget);

  // CDLOD view widget
  auto *cdlod_view = new oros::ui::CdlodViewWidget(central_widget);
  root_layout->addWidget(cdlod_view, 1);

  // Control side-panel
  auto *panel = new QWidget(central_widget);
  panel->setFixedWidth(260);
  auto *panel_layout = new QVBoxLayout(panel);

  panel_layout->addWidget(new QLabel("<h3>CDLOD Debugger</h3>", panel));

  // Instructions
  auto *instruction_lbl = new QLabel("Click or drag on the terrain map to move "
                                     "the camera (reference point) in "
                                     "(x, y).",
                                     panel);
  instruction_lbl->setWordWrap(true);
  panel_layout->addWidget(instruction_lbl);

  // Camera coordinates label
  auto *coords_lbl = new QLabel("Camera: (0.0, 0.0, 100.0)", panel);
  panel_layout->addWidget(coords_lbl);

  // Instances count label
  auto *instances_lbl = new QLabel("Active Quads: 0", panel);
  panel_layout->addWidget(instances_lbl);

  panel_layout->addSpacing(10);

  // Camera altitude slider & spinbox
  panel_layout->addWidget(new QLabel("Camera Height (Z):", panel));
  auto *alt_slider = new QSlider(Qt::Horizontal, panel);
  alt_slider->setRange(10, 1000);
  alt_slider->setValue(100);

  auto *alt_spin = new QDoubleSpinBox(panel);
  alt_spin->setRange(10.0, 2000.0);
  alt_spin->setValue(100.0);

  auto *alt_layout = new QHBoxLayout();
  alt_layout->addWidget(alt_slider);
  alt_layout->addWidget(alt_spin);
  panel_layout->addLayout(alt_layout);

  QObject::connect(alt_slider,
                   &QSlider::valueChanged,
                   [alt_spin, cdlod_view](int val)
                   {
                     alt_spin->setValue(static_cast<double>(val));
                     cdlod_view->set_camera_z(static_cast<float>(val));
                   });

  QObject::connect(alt_spin,
                   &QDoubleSpinBox::valueChanged,
                   [alt_slider, cdlod_view](double val)
                   {
                     alt_slider->setValue(static_cast<int>(val));
                     cdlod_view->set_camera_z(static_cast<float>(val));
                   });

  panel_layout->addSpacing(10);

  // Max LOD Levels
  panel_layout->addWidget(new QLabel("Max LOD Levels:", panel));
  auto *lod_spin = new QSpinBox(panel);
  lod_spin->setRange(1, 8);
  lod_spin->setValue(6);
  panel_layout->addWidget(lod_spin);

  QObject::connect(
      lod_spin,
      &QSpinBox::valueChanged,
      [cdlod_view](int val)
      { cdlod_view->set_max_lod_levels(static_cast<uint32_t>(val)); });

  panel_layout->addSpacing(10);

  // LOD Distance Scale
  panel_layout->addWidget(new QLabel("LOD Distance Scale:", panel));
  auto *scale_spin = new QDoubleSpinBox(panel);
  scale_spin->setRange(0.2, 5.0);
  scale_spin->setSingleStep(0.1);
  scale_spin->setValue(1.0);
  panel_layout->addWidget(scale_spin);

  QObject::connect(
      scale_spin,
      &QDoubleSpinBox::valueChanged,
      [cdlod_view](double val)
      { cdlod_view->set_lod_distance_scale(static_cast<float>(val)); });

  panel_layout->addSpacing(10);

  // Noise frequency
  panel_layout->addWidget(new QLabel("Noise Frequency:", panel));
  auto *noise_spin = new QDoubleSpinBox(panel);
  noise_spin->setRange(0.001, 0.1);
  noise_spin->setDecimals(4);
  noise_spin->setSingleStep(0.005);
  noise_spin->setValue(0.015);
  panel_layout->addWidget(noise_spin);

  QObject::connect(
      noise_spin,
      &QDoubleSpinBox::valueChanged,
      [cdlod_view](double val)
      { cdlod_view->set_noise_frequency(static_cast<float>(val)); });

  // Regenerate noise button
  auto *regen_btn = new QPushButton("Regenerate Noise", panel);
  panel_layout->addWidget(regen_btn);

  QObject::connect(regen_btn,
                   &QPushButton::clicked,
                   [cdlod_view]() { cdlod_view->regenerate_noise(); });

  // Connect updates to labels
  QObject::connect(cdlod_view,
                   &oros::ui::CdlodViewWidget::camera_moved,
                   [coords_lbl](float x, float y, float z)
                   {
                     coords_lbl->setText(QString("Camera: (%1, %2, %3)")
                                             .arg(x, 0, 'f', 1)
                                             .arg(y, 0, 'f', 1)
                                             .arg(z, 0, 'f', 1));
                   });

  QObject::connect(
      cdlod_view,
      &oros::ui::CdlodViewWidget::selection_updated,
      [instances_lbl](size_t count)
      { instances_lbl->setText(QString("Active Quads: %1").arg(count)); });

  panel_layout->addStretch();
  root_layout->addWidget(panel);

  main_window.setCentralWidget(central_widget);
  main_window.show();

  return app.exec();
}
