/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oros::terrain
{

// --- HeightmapData class
// Manages raster elevation values and a multi-level min/max bounding pyramid
// for efficient hierarchical queries and culling.

class HeightmapData
{
public:
  HeightmapData() = default;

  HeightmapData(size_t width, size_t height, float initial_height = 0.0f);

  HeightmapData(size_t width, size_t height, std::vector<float> data);

  [[nodiscard]] size_t get_width() const noexcept;

  [[nodiscard]] size_t get_height() const noexcept;

  [[nodiscard]] const std::vector<float> &get_data() const noexcept;

  [[nodiscard]] float get_height_at(size_t x, size_t y) const;

  void set_height_at(size_t x, size_t y, float value);

  // Updates a sub-rectangle of elevations and incrementally refreshes min/max
  // bounds
  void update_region(size_t       start_x,
                     size_t       start_y,
                     size_t       region_width,
                     size_t       region_height,
                     const float *data);

  // Retrieves global min and max elevation bounds
  [[nodiscard]] std::pair<float, float> get_elevation_range() const noexcept;

  // Retrieves min and max elevation bounds for a normalized sub-region [u0, v0]
  // to [u1, v1] in [0, 1]
  [[nodiscard]] std::pair<float, float> get_elevation_range_in_rect(
      float u0,
      float v0,
      float u1,
      float v1) const noexcept;

private:
  struct MinMaxNode
  {
    float min_z{std::numeric_limits<float>::infinity()};
    float max_z{-std::numeric_limits<float>::infinity()};
  };

  void build_pyramid();

  void update_pyramid_region(size_t start_x,
                             size_t start_y,
                             size_t region_width,
                             size_t region_height);

  size_t             width_{0};
  size_t             height_{0};
  std::vector<float> data_{};

  // Mip pyramid of min/max bounds: mip 0 is 1/2 resolution, up to 1x1 root
  std::vector<std::vector<MinMaxNode>> min_max_pyramid_{};
  std::vector<size_t>                  pyramid_widths_{};
  std::vector<size_t>                  pyramid_heights_{};
  float                                global_min_{0.0f};
  float                                global_max_{0.0f};
};

} // namespace oros::terrain
