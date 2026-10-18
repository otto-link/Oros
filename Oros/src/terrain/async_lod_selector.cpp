#include "oros/terrain/async_lod_selector.hpp"

namespace oros::terrain
{

std::future<std::vector<CDLODInstance>> AsyncLODSelector::request_lod_async(
    const HeightmapPatch        &patch,
    const CDLODSelectionContext &context,
    const CDLODQuadTree         &quadtree)
{
  return std::async(std::launch::async,
                    [patch, context, quadtree]()
                    { return quadtree.select_lod(patch, context); });
}

std::vector<CDLODInstance> AsyncLODSelector::compute_lod_sync(
    const HeightmapPatch        &patch,
    const CDLODSelectionContext &context,
    const CDLODQuadTree         &quadtree)
{
  return quadtree.select_lod(patch, context);
}

} // namespace oros::terrain
