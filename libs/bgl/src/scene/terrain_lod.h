#pragma once
#include <bgl/glm.h>
#include <bgl/idl/Constants.h>
#include <cstdint>

namespace bgl
{
	/**
	 * How a terrain is cut into levels of detail, and how a level is chosen, as the CPU computes
	 * it: the twin of the terrain stage's amplification shader (programs/forward/Terrain.slang),
	 * which the scene uses to lay out a terrain's node bounds and the tests use to check the stage's
	 * rule. Both read the same numbers off the terrain's record, so a change here is a change there.
	 *
	 * A node at level `l` is a square patch of `cTerrainPatchQuads << l` cells; level 0 is the
	 * samples' own resolution, and the coarsest level has one node across the longer axis.
	 */

	/** Cells one node of `level` spans along either axis. */
	[[nodiscard]] constexpr uint32_t
	TerrainNodeCells(const uint32_t level) noexcept
	{
		return idl::cTerrainPatchQuads << level;
	}

	/** Nodes of `level` across `samples` samples: the last may hang past the edge. */
	[[nodiscard]] constexpr uint32_t
	TerrainNodesAcross(const uint32_t samples, const uint32_t level) noexcept
	{
		const uint32_t cells = TerrainNodeCells(level);
		return (samples - 1 + cells - 1) / cells;
	}

	/** Levels a field of `samplesX` by `samplesZ` is cut into: until one node spans it. */
	[[nodiscard]] constexpr uint32_t
	TerrainLevels(const uint32_t samplesX, const uint32_t samplesZ) noexcept
	{
		uint32_t levels = 1;
		while (levels < idl::cTerrainMaxLevels && (TerrainNodesAcross(samplesX, levels - 1) > 1 ||
		                                           TerrainNodesAcross(samplesZ, levels - 1) > 1))
		{
			++levels;
		}
		return levels;
	}

	/** Nodes in every level up to `levels`, laid out level by level, each row-major. */
	[[nodiscard]] constexpr uint32_t
	TerrainNodeCount(
		const uint32_t samplesX,
		const uint32_t samplesZ,
		const uint32_t levels) noexcept
	{
		uint32_t count = 0;
		for (uint32_t level = 0; level < levels; ++level)
		{
			count += TerrainNodesAcross(samplesX, level) * TerrainNodesAcross(samplesZ, level);
		}
		return count;
	}

	/** One node, found from its place in the level-major layout. */
	struct TerrainNode
	{
		uint32_t level = 0;
		uint32_t x     = 0;
		uint32_t z     = 0;
	};

	/**
	 * The node at `index` of the level-major layout.
	 * @pre index < TerrainNodeCount(samplesX, samplesZ, levels).
	 */
	[[nodiscard]] constexpr TerrainNode
	TerrainNodeAt(
		const uint32_t samplesX,
		const uint32_t samplesZ,
		const uint32_t levels,
		uint32_t       index) noexcept
	{
		for (uint32_t level = 0; level < levels; ++level)
		{
			const uint32_t across  = TerrainNodesAcross(samplesX, level);
			const uint32_t inLevel = across * TerrainNodesAcross(samplesZ, level);
			if (index < inLevel)
			{
				return { .level = level, .x = index % across, .z = index / across };
			}
			index -= inLevel;
		}
		return { .level = levels, .x = 0, .z = 0 };
	}

	/**
	 * The distance from the camera at which a cell of `level` spans `pixelsPerCell` on screen:
	 * the range the level is drawn within. Floored at sqrt(2) times a node's width so two
	 * neighbours never differ by more than one level, which is what keeps the morph crack-free.
	 */
	[[nodiscard]] constexpr float
	TerrainLevelRange(
		const uint32_t level,
		const float    cellSize,
		const float    pixelsPerUnit,
		const float    pixelsPerCell) noexcept
	{
		const float cell  = cellSize * static_cast<float>(1u << level);
		const float range = cell * pixelsPerUnit / pixelsPerCell;
		const float floor = 1.4142136f * cellSize * static_cast<float>(TerrainNodeCells(level));
		return range > floor ? range : floor;
	}

	/**
	 * Whether a node draws at its own level: it is no nearer than the finer level's range, and
	 * its parent is nearer than its own level's range (or it is the coarsest). Every finest cell is
	 * then covered by exactly one node of the chain above it, with no traversal and no state.
	 *
	 * @param distance        The nearest distance from the camera to the node's bounds.
	 * @param parentDistance  The same for its parent; ignored on the coarsest level.
	 * @param range           The range of each level, TerrainLevelRange, `levels` long.
	 */
	[[nodiscard]] constexpr bool
	TerrainNodeDraws(
		const uint32_t level,
		const uint32_t levels,
		const float    distance,
		const float    parentDistance,
		const float*   range) noexcept
	{
		const bool coarseEnough = level == 0 || distance >= range[level - 1];
		const bool fineEnough   = level + 1 == levels || parentDistance < range[level];
		return coarseEnough && fineEnough;
	}
}
