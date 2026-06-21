#pragma once

namespace bgl
{
	enum class GeomType : uint8_t
	{
		kInvalid    = static_cast<uint8_t>(-1),
		kStaticMesh = 0,
		//kSkinnedMesh,
		kCount
	};
}
