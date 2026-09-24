#pragma once
#include <cstdint>

namespace bgl
{
	/** Thread groups one DispatchMesh can launch along an axis, on every backend. */
	constexpr uint32_t c_MaxDispatchMeshGroups = 65535;
}
