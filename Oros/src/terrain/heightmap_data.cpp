/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <cmath>

#include "oros/terrain/heightmap_data.hpp"

namespace oros::terrain
{

HeightmapData::HeightmapData(size_t width, size_t height, float initial_height)
    : width_(width), height_(height), data_(width * height, initial_height)
{
  build_pyramid();
}

HeightmapData::HeightmapData(size_t             width,
                             size_t             height,
                             std::vector<float> data)
    : width_(width), height_(height), data_(std::move(data))
{
  if (data_.size() != width_ * height_)
  {
    throw std::invalid_argument(
        "HeightmapData size does not match width * height");
  }

  build_pyramid();
}

size_t HeightmapData::get_width() const noexcept
{
  return width_;
}

size_t HeightmapData::get_height() const noexcept
{
  return height_;
}

const std::vector<float> &HeightmapData::get_data() const noexcept
{
  return data_;
}

float HeightmapData::get_height_at(size_t x, size_t y) const
{
  if (x >= width_ || y >= height_)
  {
    throw std::out_of_range("Heightmap coordinate out of bounds");
  }

  return data_[y * width_ + x];
}

void HeightmapData::set_height_at(size_t x, size_t y, float value)
{
  update_region(x, y, 1, 1, &value);
}

void HeightmapData::update_region(size_t       start_x,
                                  size_t       start_y,
                                  size_t       region_width,
                                  size_t       region_height,
                                  const float *data)
{
  if (start_x + region_width > width_ || start_y + region_height > height_)
  {
    throw std::out_of_range("Update region out of bounds");
  }

  // Update raw elevation grid
  for (size_t row = 0; row < region_height; ++row)
  {
    const size_t dst_offset = (start_y + row) * width_ + start_x;
    const size_t src_offset = row * region_width;

    std::copy_n(data + src_offset, region_width, data_.begin() + dst_offset);
  }

  // Incrementally update min/max pyramid
  update_pyramid_region(start_x, start_y, region_width, region_height);
}

std::pair<float, float> HeightmapData::get_elevation_range() const noexcept
{
  return {global_min_, global_max_};
}

std::pair<float, float> HeightmapData::get_elevation_range_in_rect(
    float u0,
    float v0,
    float u1,
    float v1) const noexcept
{
  if (width_ == 0 || height_ == 0)
  {
    return {0.0f, 0.0f};
  }

  const float min_u = std::clamp(std::min(u0, u1), 0.0f, 1.0f);
  const float max_u = std::clamp(std::max(u0, u1), 0.0f, 1.0f);
  const float min_v = std::clamp(std::min(v0, v1), 0.0f, 1.0f);
  const float max_v = std::clamp(std::max(v0, v1), 0.0f, 1.0f);

  const size_t x0 = static_cast<size_t>(min_u * static_cast<float>(width_ - 1));
  const size_t x1 = static_cast<size_t>(
      std::ceil(max_u * static_cast<float>(width_ - 1)));
  const size_t y0 = static_cast<size_t>(min_v *
                                        static_cast<float>(height_ - 1));
  const size_t y1 = static_cast<size_t>(
      std::ceil(max_v * static_cast<float>(height_ - 1)));

  float min_val = std::numeric_limits<float>::infinity();
  float max_val = -std::numeric_limits<float>::infinity();

  for (size_t y = y0; y <= y1 && y < height_; ++y)
  {
    const size_t row_start = y * width_;
    for (size_t x = x0; x <= x1 && x < width_; ++x)
    {
      const float val = data_[row_start + x];
      min_val = std::min(min_val, val);
      max_val = std::max(max_val, val);
    }
  }

  if (min_val > max_val)
  {
    return {global_min_, global_max_};
  }

  return {min_val, max_val};
}

void HeightmapData::build_pyramid()
{
  min_max_pyramid_.clear();
  pyramid_widths_.clear();
  pyramid_heights_.clear();

  if (data_.empty())
  {
    global_min_ = 0.0f;
    global_max_ = 0.0f;
    return;
  }

  // Compute global bounds
  const auto [min_it, max_it] = std::minmax_element(data_.begin(), data_.end());
  global_min_ = *min_it;
  global_max_ = *max_it;

  // Build pyramid levels (each level is half resolution)
  size_t curr_w = std::max<size_t>(1, (width_ + 1) / 2);
  size_t curr_h = std::max<size_t>(1, (height_ + 1) / 2);

  // Level 0
  std::vector<MinMaxNode> level0(curr_w * curr_h);
  for (size_t py = 0; py < curr_h; ++py)
  {
    for (size_t px = 0; px < curr_w; ++px)
    {
      float min_z = std::numeric_limits<float>::infinity();
      float max_z = -std::numeric_limits<float>::infinity();

      for (size_t dy = 0; dy < 2; ++dy)
      {
        const size_t sy = py * 2 + dy;
        if (sy >= height_)
        {
          continue;
        }

        for (size_t dx = 0; dx < 2; ++dx)
        {
          const size_t sx = px * 2 + dx;
          if (sx >= width_)
          {
            continue;
          }

          const float val = data_[sy * width_ + sx];
          min_z = std::min(min_z, val);
          max_z = std::max(max_z, val);
        }
      }

      level0[py * curr_w + px] = {min_z, max_z};
    }
  }

  min_max_pyramid_.push_back(std::move(level0));
  pyramid_widths_.push_back(curr_w);
  pyramid_heights_.push_back(curr_h);

  // Higher levels
  while (curr_w > 1 || curr_h > 1)
  {
    const size_t prev_w = curr_w;
    const size_t prev_h = curr_h;

    curr_w = std::max<size_t>(1, (prev_w + 1) / 2);
    curr_h = std::max<size_t>(1, (prev_h + 1) / 2);

    const auto             &prev_level = min_max_pyramid_.back();
    std::vector<MinMaxNode> next_level(curr_w * curr_h);

    for (size_t py = 0; py < curr_h; ++py)
    {
      for (size_t px = 0; px < curr_w; ++px)
      {
        float min_z = std::numeric_limits<float>::infinity();
        float max_z = -std::numeric_limits<float>::infinity();

        for (size_t dy = 0; dy < 2; ++dy)
        {
          const size_t sy = py * 2 + dy;
          if (sy >= prev_h)
          {
            continue;
          }

          for (size_t dx = 0; dx < 2; ++dx)
          {
            const size_t sx = px * 2 + dx;
            if (sx >= prev_w)
            {
              continue;
            }

            const auto &node = prev_level[sy * prev_w + sx];
            min_z = std::min(min_z, node.min_z);
            max_z = std::max(max_z, node.max_z);
          }
        }

        next_level[py * curr_w + px] = {min_z, max_z};
      }
    }

    min_max_pyramid_.push_back(std::move(next_level));
    pyramid_widths_.push_back(curr_w);
    pyramid_heights_.push_back(curr_h);
  }
}

void HeightmapData::update_pyramid_region(size_t start_x,
                                          size_t start_y,
                                          size_t region_width,
                                          size_t region_height)
{
  if (min_max_pyramid_.empty())
  {
    build_pyramid();
    return;
  }

  // Update base level 0
  size_t cur_x0 = start_x / 2;
  size_t cur_y0 = start_y / 2;
  size_t cur_x1 = std::min(pyramid_widths_[0] - 1,
                           (start_x + region_width + 1) / 2);
  size_t cur_y1 = std::min(pyramid_heights_[0] - 1,
                           (start_y + region_height + 1) / 2);

  auto        &level0 = min_max_pyramid_[0];
  const size_t w0 = pyramid_widths_[0];

  for (size_t py = cur_y0; py <= cur_y1; ++py)
  {
    for (size_t px = cur_x0; px <= cur_x1; ++px)
    {
      float min_z = std::numeric_limits<float>::infinity();
      float max_z = -std::numeric_limits<float>::infinity();

      for (size_t dy = 0; dy < 2; ++dy)
      {
        const size_t sy = py * 2 + dy;
        if (sy >= height_)
        {
          continue;
        }

        for (size_t dx = 0; dx < 2; ++dx)
        {
          const size_t sx = px * 2 + dx;
          if (sx >= width_)
          {
            continue;
          }

          const float val = data_[sy * width_ + sx];
          min_z = std::min(min_z, val);
          max_z = std::max(max_z, val);
        }
      }

      level0[py * w0 + px] = {min_z, max_z};
    }
  }

  // Propagate to higher levels
  for (size_t level = 1; level < min_max_pyramid_.size(); ++level)
  {
    const size_t prev_w = pyramid_widths_[level - 1];
    const size_t prev_h = pyramid_heights_[level - 1];
    const size_t cur_w = pyramid_widths_[level];

    cur_x0 = cur_x0 / 2;
    cur_y0 = cur_y0 / 2;
    cur_x1 = std::min(cur_w - 1, (cur_x1 + 1) / 2);
    cur_y1 = std::min(pyramid_heights_[level] - 1, (cur_y1 + 1) / 2);

    const auto &prev_level = min_max_pyramid_[level - 1];
    auto       &curr_level = min_max_pyramid_[level];

    for (size_t py = cur_y0; py <= cur_y1; ++py)
    {
      for (size_t px = cur_x0; px <= cur_x1; ++px)
      {
        float min_z = std::numeric_limits<float>::infinity();
        float max_z = -std::numeric_limits<float>::infinity();

        for (size_t dy = 0; dy < 2; ++dy)
        {
          const size_t sy = py * 2 + dy;
          if (sy >= prev_h)
          {
            continue;
          }

          for (size_t dx = 0; dx < 2; ++dx)
          {
            const size_t sx = px * 2 + dx;
            if (sx >= prev_w)
            {
              continue;
            }

            const auto &node = prev_level[sy * prev_w + sx];
            min_z = std::min(min_z, node.min_z);
            max_z = std::max(max_z, node.max_z);
          }
        }

        curr_level[py * cur_w + px] = {min_z, max_z};
      }
    }
  }

  // Root level gives the global min and max
  const auto &root = min_max_pyramid_.back()[0];
  global_min_ = root.min_z;
  global_max_ = root.max_z;
}

} // namespace oros::terrain
