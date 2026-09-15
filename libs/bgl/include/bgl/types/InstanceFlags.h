#pragma once

#include <bgl/InstanceFlag.h>
#include <core/containers/enum_set.h>
#include <cstdint>

namespace bgl
{
	/** A placement's flags word, as ISceneView::SetInstanceFlags takes it. Empty is the default. */
	using InstanceFlags = core::enum_set<InstanceFlag, uint32_t>;
}
