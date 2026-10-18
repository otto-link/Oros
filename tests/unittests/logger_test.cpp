#include <gtest/gtest.h>
#include <oros/logger.hpp>

// --- Logger unit tests

TEST(LoggerTest, LoggerInitialization)
{
  auto &log = oros::Logger::log();
  ASSERT_NE(log, nullptr);

  // logging should work without throwing exceptions
  EXPECT_NO_THROW({
    log->trace("Trace message test");
    log->debug("Debug message test");
    log->info("Info message test");
    log->warn("Warning message test");
    log->error("Error message test");
  });
}
