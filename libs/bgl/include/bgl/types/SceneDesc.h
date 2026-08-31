#pragma once

namespace bgl
{
	/**
	 * How big the scene's GPU arenas start, not how big they may get. Each one grows on demand,
	 * bounded only by device memory, so these are a hint that trades startup residency against the
	 * number of growth events during a load. Sizing them near the steady state avoids both.
	 *
	 * Every field is advisory. A renderer that keeps no arena of a given kind ignores that field
	 * rather than failing on it.
	 */
	struct SceneDesc
	{
		uint32_t initialGeom                 = 1;
		uint32_t initialMeshlets             = 1;
		uint32_t initialIndices              = 1;
		uint32_t initialSubmeshes            = 1;
		uint32_t initialVertexBufferByteSize = 1;
		uint32_t initialPbrMaterials         = 1;
		uint32_t initialLoosePbrMaterials    = 1;
	};
}
