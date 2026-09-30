#pragma once
#include <bgl/Viewport.h>
#include <bgpu/types/Viewport.h>

namespace bgl
{
	/** The contract's spelling of an RHI viewport, for a helper the contract defines over its own. */
	[[nodiscard]] inline Viewport
	ToContractViewport(const bgpu::Viewport& viewport) noexcept
	{
		return Viewport(
			viewport.minX,
			viewport.maxX,
			viewport.minY,
			viewport.maxY,
			viewport.minZ,
			viewport.maxZ);
	}
}
