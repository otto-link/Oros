#pragma once

#include <future>
#include <memory>
#include <vector>

#include "oros/terrain/cdlod_quadtree.hpp"

namespace oros::terrain
{

// --- AsyncLODSelector
// Asynchronously computes CDLOD active node instances without stalling the
// render thread.

class AsyncLODSelector
{
public:
  AsyncLODSelector() = default;

  // Launches an asynchronous LOD calculation task
  [[nodiscard]] std::future<std::vector<CDLODInstance>> request_lod_async(
      const HeightmapPatch        &patch,
      const CDLODSelectionContext &context,
      const CDLODQuadTree         &quadtree);

  // Synchronously computes LOD instances immediately
  [[nodiscard]] std::vector<CDLODInstance> compute_lod_sync(
      const HeightmapPatch        &patch,
      const CDLODSelectionContext &context,
      const CDLODQuadTree         &quadtree);
};

} // namespace oros::terrain
