/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include "cdlod_gl_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <oros/logger.hpp>
#include <oros/oros.hpp>

// --- Entry point

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  oros::Logger::log()->info(
      "Starting CDLOD OpenGL Wireframe Test (version {})...",
      oros::get_version());

  oros::Logger::log()->info("Building the GUI...");

  QMainWindow main_window;
  main_window.setWindowTitle("Oros - CDLOD OpenGL Realtime Wireframe Test");
  main_window.resize(1100, 800);

  auto *central_widget = new QWidget(&main_window);
  auto *root_layout = new QHBoxLayout(central_widget);

  // OpenGL CDLOD widget
  auto *gl_widget = new oros::ui::CdlodGlWidget(central_widget);
  root_layout->addWidget(gl_widget, 1);

  // Control side-panel
  auto *panel = new QWidget(central_widget);
  panel->setFixedWidth(280);
  auto *panel_layout = new QVBoxLayout(panel);

  panel_layout->addWidget(new QLabel("<h3>OpenGL CDLOD</h3>", panel));

  // Controls Guide
  auto *guide_lbl = new QLabel("<b>Navigation:</b><br>"
                               "- Left Drag: Orbit camera<br>"
                               "- Right Drag: Pan terrain<br>"
                               "- Wheel: Zoom in / out",
                               panel);
  guide_lbl->setWordWrap(true);
  panel_layout->addWidget(guide_lbl);

  panel_layout->addSpacing(10);

  // Stats
  auto *coords_lbl = new QLabel("Camera: (0, 0, 0)", panel);
  panel_layout->addWidget(coords_lbl);

  auto *instances_lbl = new QLabel("Active Patches: 0", panel);
  panel_layout->addWidget(instances_lbl);

  panel_layout->addSpacing(10);

  // Shading Checkbox
  auto *shading_check = new QCheckBox("Terrain Shading (Normals + Light)",
                                      panel);
  shading_check->setChecked(true);
  panel_layout->addWidget(shading_check);

  QObject::connect(shading_check,
                   &QCheckBox::toggled,
                   gl_widget,
                   &oros::ui::CdlodGlWidget::set_shading_mode);

  // Wireframe Checkbox
  auto *wireframe_check = new QCheckBox("Wireframe Mode (glPolygonMode)",
                                        panel);
  wireframe_check->setChecked(true);
  panel_layout->addWidget(wireframe_check);

  QObject::connect(wireframe_check,
                   &QCheckBox::toggled,
                   gl_widget,
                   &oros::ui::CdlodGlWidget::set_wireframe_mode);

  // Geomorphing Checkbox
  auto *morph_check = new QCheckBox("Enable Geomorphing (No Seams)", panel);
  morph_check->setChecked(false);
  panel_layout->addWidget(morph_check);

  QObject::connect(morph_check,
                   &QCheckBox::toggled,
                   gl_widget,
                   &oros::ui::CdlodGlWidget::set_morphing_enabled);

  panel_layout->addSpacing(10);

  // Max LOD Levels
  panel_layout->addWidget(new QLabel("Max LOD Levels:", panel));
  auto *lod_spin = new QSpinBox(panel);
  lod_spin->setRange(1, 10);
  lod_spin->setValue(8);
  panel_layout->addWidget(lod_spin);

  QObject::connect(
      lod_spin,
      &QSpinBox::valueChanged,
      [gl_widget](int val)
      { gl_widget->set_max_lod_levels(static_cast<uint32_t>(val)); });

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
      [gl_widget](double val)
      { gl_widget->set_lod_distance_scale(static_cast<float>(val)); });

  panel_layout->addSpacing(10);

  // Noise Frequency
  panel_layout->addWidget(new QLabel("Terrain Roughness (Freq):", panel));
  auto *noise_spin = new QDoubleSpinBox(panel);
  noise_spin->setRange(0.1, 50.0);
  noise_spin->setDecimals(2);
  noise_spin->setSingleStep(0.5);
  noise_spin->setValue(2.0);
  panel_layout->addWidget(noise_spin);

  QObject::connect(
      noise_spin,
      &QDoubleSpinBox::valueChanged,
      [gl_widget](double val)
      { gl_widget->set_noise_frequency(static_cast<float>(val)); });

  // Regenerate Noise
  auto *regen_btn = new QPushButton("Regenerate Terrain", panel);
  panel_layout->addWidget(regen_btn);

  QObject::connect(regen_btn,
                   &QPushButton::clicked,
                   [gl_widget]() { gl_widget->regenerate_noise(); });

  // Updates from widget
  QObject::connect(gl_widget,
                   &oros::ui::CdlodGlWidget::camera_moved,
                   [coords_lbl](float x, float y, float z)
                   {
                     coords_lbl->setText(QString("Camera: (%1, %2, %3)")
                                             .arg(x, 0, 'f', 1)
                                             .arg(y, 0, 'f', 1)
                                             .arg(z, 0, 'f', 1));
                   });

  QObject::connect(
      gl_widget,
      &oros::ui::CdlodGlWidget::selection_updated,
      [instances_lbl](size_t count)
      { instances_lbl->setText(QString("Active Patches: %1").arg(count)); });

  panel_layout->addStretch();
  root_layout->addWidget(panel);

  main_window.setCentralWidget(central_widget);
  main_window.show();

  return app.exec();
}
