#include <memory>

#include <QApplication>
#include <gtest/gtest.h>
#include <oros/oros.hpp>
#include <oros/ui/viewport_widget.hpp>

// --- ViewportWidget unit tests

TEST(OrosBasicTest, VersionCheck)
{
  EXPECT_FALSE(oros::get_version().empty());
  EXPECT_EQ(oros::get_version(), "0.1.0");
}

TEST(ViewportWidgetTest, WidgetCreationAndColor)
{
  // ensure offscreen platform for headless test execution
  qputenv("QT_QPA_PLATFORM", "offscreen");

  int           argc = 0;
  char         *argv[] = {nullptr};
  QApplication *app = qobject_cast<QApplication *>(
      QCoreApplication::instance());
  std::unique_ptr<QApplication> app_holder;

  if (!app)
  {
    app_holder = std::make_unique<QApplication>(argc, argv);
    app = app_holder.get();
  }

  oros::ui::ViewportWidget widget;
  EXPECT_EQ(widget.get_background_color(), QColor(30, 30, 35));

  QColor new_color(50, 70, 90);
  widget.set_background_color(new_color);
  EXPECT_EQ(widget.get_background_color(), new_color);
}

TEST(ViewportWidgetTest, WidgetResize)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");

  int           argc = 0;
  char         *argv[] = {nullptr};
  QApplication *app = qobject_cast<QApplication *>(
      QCoreApplication::instance());
  std::unique_ptr<QApplication> app_holder;

  if (!app)
  {
    app_holder = std::make_unique<QApplication>(argc, argv);
    app = app_holder.get();
  }

  oros::ui::ViewportWidget widget;
  widget.resize(640, 480);
  EXPECT_EQ(widget.width(), 640);
  EXPECT_EQ(widget.height(), 480);
}
