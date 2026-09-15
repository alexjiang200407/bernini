#pragma once

#include <bgl/MeshInstanceFlag.h>
#include <core/containers/enum_set.h>
#include <cstdint>

namespace bgl
{
	/** A placement's flags word, as ISceneView::SetMeshInstanceFlags takes it. Empty is the default. */
	using MeshInstanceFlags = core::enum_set<MeshInstanceFlag, uint32_t>;
}
