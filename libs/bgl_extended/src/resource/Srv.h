#pragma once
#include "types/Format.h"
#include "types/TextureDimension.h"
#include "uniforms/DescriptorHandle.h"
#include <cstdint>
#include <string>

namespace bgl
{
	class Srv;

	struct SrvDesc
	{
		Format           format    = Format::UNKNOWN;
		TextureDimension dimension = TextureDimension::kTexture2D;
		uint32_t         mipLevels = 1;
		uint32_t         arraySize = 1;
		std::string      debugName = "";
	};

	struct SrvHandle
	{
		uint32_t idx        = 0xFFFFFFFF;
		uint32_t generation = 0;

		// The two ways a shader reaches this view, which are the same number only on D3D12.
		//
		// bindlessIndex is what belongs in a constant buffer: a descriptor-heap index on D3D12, the
		// texture's pool slot on Metal, where the dispatch rewrite looks the resource up by it.
		// descriptor is what belongs in GPU memory, read without the encoder in the loop: the same
		// heap index on D3D12, the native MTLResourceID on Metal.
		uint32_t         bindlessIndex = 0xFFFFFFFF;
		DescriptorHandle descriptor    = {};

		[[nodiscard]] bool
		IsNull() const
		{
			return idx == 0xFFFFFFFF;
		}

		operator bool() const noexcept { return !IsNull(); }
	};
}
