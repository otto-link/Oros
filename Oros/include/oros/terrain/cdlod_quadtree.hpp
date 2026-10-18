#pragma once

#include <cstdint>
#include <vector>

#include "oros/terrain/heightmap_patch.hpp"

namespace oros::terrain
{

// --- CDLOD instance information sent to the renderer
struct CDLODInstance
{
  float    offset_u{0.0f}; // [0, 1] UV sub-region offset
  float    offset_v{0.0f};
  float    scale_u{1.0f}; // [0, 1] UV sub-region size
  float    scale_v{1.0f};
  float    morph_start{0.0f}; // Distance where morph begins
  float    morph_range{1.0f}; // Distance range for morph transition
  uint32_t lod_level{0};      // 0 = finest, increases towards coarsest
};

// --- Selection context for LOD traversal
struct CDLODSelectionContext
{
  Vec3               camera_pos{};
  std::vector<float> lod_distances{}; // e.g. [50, 100, 200, 400, 800...]
  float morph_ratio{0.66f};           // Portion of LOD band used for morphing
};

// --- Quadtree node internal structure
struct CDLODNode
{
  float    u0{0.0f};
  float    v0{0.0f};
  float    size_u{1.0f};
  float    size_v{1.0f};
  uint32_t lod_level{0};
  float    min_z{0.0f};
  float    max_z{0.0f};
};

// --- CDLODQuadTree class
// Manages hierarchical continuous distance-dependent LOD selection
// and culling for a single HeightmapPatch.

class CDLODQuadTree
{
public:
  CDLODQuadTree() = default;

  explicit CDLODQuadTree(uint32_t max_lod_levels);

  [[nodiscard]] uint32_t get_max_lod_levels() const noexcept;

  void set_max_lod_levels(uint32_t levels) noexcept;

  // Performs fast LOD selection given a patch and camera context
  [[nodiscard]] std::vector<CDLODInstance> select_lod(
      const HeightmapPatch        &patch,
      const CDLODSelectionContext &context) const;

private:
  void select_node(const HeightmapPatch        &patch,
                   const CDLODSelectionContext &context,
                   const CDLODNode             &node,
                   std::vector<CDLODInstance>  &out_instances) const;

  uint32_t max_lod_levels_{6};
};

} // namespace oros::terrain
