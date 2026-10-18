/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

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
