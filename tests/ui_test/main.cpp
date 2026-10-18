#include <QApplication>
#include <oros/oros.hpp>
#include <oros/ui/viewport_widget.hpp>

// --- Entry point

int main(int argc, char *argv[])
{
  // initialize Qt application
  QApplication app(argc, argv);

  // create and show 3D viewport widget
  oros::ui::ViewportWidget widget;
  widget.setWindowTitle("Oros - 3D Viewport Example");
  widget.resize(800, 600);
  widget.show();

  return app.exec();
}
