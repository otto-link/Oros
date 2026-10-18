/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#include <cmath>

#include "oros/terrain/cdlod_quadtree.hpp"

namespace oros::terrain
{

CDLODQuadTree::CDLODQuadTree(uint32_t max_lod_levels)
    : max_lod_levels_(max_lod_levels)
{
}

uint32_t CDLODQuadTree::get_max_lod_levels() const noexcept
{
  return max_lod_levels_;
}

void CDLODQuadTree::set_max_lod_levels(uint32_t levels) noexcept
{
  max_lod_levels_ = levels;
}

std::vector<CDLODInstance> CDLODQuadTree::select_lod(
    const HeightmapPatch        &patch,
    const CDLODSelectionContext &context) const
{
  std::vector<CDLODInstance> instances;
  instances.reserve(128);

  const auto heightmap_data = patch.get_heightmap_data();
  float      min_z = 0.0f;
  float      max_z = 0.0f;

  if (heightmap_data)
  {
    const auto range = heightmap_data->get_elevation_range();
    min_z = range.first;
    max_z = range.second;
  }

  // Root node starts at coarsest LOD
  const uint32_t  root_lod = (max_lod_levels_ > 0) ? (max_lod_levels_ - 1) : 0;
  const CDLODNode root_node{.u0 = 0.0f,
                            .v0 = 0.0f,
                            .size_u = 1.0f,
                            .size_v = 1.0f,
                            .lod_level = root_lod,
                            .min_z = min_z,
                            .max_z = max_z};

  select_node(patch, context, root_node, instances);

  return instances;
}

void CDLODQuadTree::select_node(const HeightmapPatch        &patch,
                                const CDLODSelectionContext &context,
                                const CDLODNode             &node,
                                std::vector<CDLODInstance> &out_instances) const
{
  // Calculate center of node in local patch space
  const float center_u = node.u0 + node.size_u * 0.5f;
  const float center_v = node.v0 + node.size_v * 0.5f;
  const float mid_z = (node.min_z + node.max_z) * 0.5f;

  // Transform center of node to world space
  const Vec3 node_world_center = patch.local_to_world(center_u,
                                                      center_v,
                                                      mid_z);

  // Distance from camera to node center
  const float dx = context.camera_pos.x - node_world_center.x;
  const float dy = context.camera_pos.y - node_world_center.y;
  const float dz = context.camera_pos.z - node_world_center.z;
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

  // Determine LOD visibility threshold
  const size_t lod_idx = std::min<size_t>(node.lod_level,
                                          context.lod_distances.size() - 1);
  const float  lod_dist = context.lod_distances.empty()
                              ? 100.0f
                              : context.lod_distances[lod_idx];

  // If node can be subdivided into a finer LOD and is closer than threshold
  const bool should_subdivide = (node.lod_level > 0) && (distance < lod_dist);

  if (should_subdivide)
  {
    const float    half_u = node.size_u * 0.5f;
    const float    half_v = node.size_v * 0.5f;
    const uint32_t child_lod = node.lod_level - 1;
    const auto     hdata = patch.get_heightmap_data();

    for (int sy = 0; sy < 2; ++sy)
    {
      for (int sx = 0; sx < 2; ++sx)
      {
        const float cu0 = node.u0 + sx * half_u;
        const float cv0 = node.v0 + sy * half_v;

        float c_min_z = node.min_z;
        float c_max_z = node.max_z;

        if (hdata)
        {
          const auto r = hdata->get_elevation_range_in_rect(cu0,
                                                            cv0,
                                                            cu0 + half_u,
                                                            cv0 + half_v);
          c_min_z = r.first;
          c_max_z = r.second;
        }

        const CDLODNode child_node{.u0 = cu0,
                                   .v0 = cv0,
                                   .size_u = half_u,
                                   .size_v = half_v,
                                   .lod_level = child_lod,
                                   .min_z = c_min_z,
                                   .max_z = c_max_z};

        select_node(patch, context, child_node, out_instances);
      }
    }
  }
  else
  {
    // Node is rendered at current LOD level
    // Calculate morph transition parameters
    const float morph_end = lod_dist;
    const float morph_range = lod_dist * context.morph_ratio;
    const float morph_start = std::max(0.0f, morph_end - morph_range);

    out_instances.push_back(CDLODInstance{
        .offset_u = node.u0,
        .offset_v = node.v0,
        .scale_u = node.size_u,
        .scale_v = node.size_v,
        .morph_start = morph_start,
        .morph_range = (morph_range > 0.0001f) ? morph_range : 1.0f,
        .lod_level = node.lod_level});
  }
}

} // namespace oros::terrain
