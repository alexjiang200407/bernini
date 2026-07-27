// THIS IS A FILE GENERATED FROM LoosePbrMaterial.slang. DO NOT EDIT MANUALLY
#pragma once
#include "ChannelSource.h"

namespace bgl::idl
{
	enum class PbrChannel : uint32_t
	{
		kBaseColorR = 0,
		kBaseColorG = 1,
		kBaseColorB = 2,
		kBaseColorA = 3,
		kAo = 4,
		kRoughness = 5,
		kMetallic = 6,
		kNormalX = 7,
		kNormalY = 8,
	};

	static_assert(sizeof(PbrChannel) == 4);

	constexpr uint32_t cLooseChannelCount = 9;

	struct LoosePbrMaterial
	{
		glm::vec4 baseColorFactor;
		ChannelSource sources[9];
		float metallicFactor;
		float roughnessFactor;
		float alphaCutoff;
		uint32_t pad[1];
	};

	static_assert(sizeof(LoosePbrMaterial) == 176);
	static_assert(offsetof(LoosePbrMaterial, baseColorFactor) == 0);
	static_assert(offsetof(LoosePbrMaterial, sources) == 16);
	static_assert(offsetof(LoosePbrMaterial, metallicFactor) == 160);
	static_assert(offsetof(LoosePbrMaterial, roughnessFactor) == 164);
	static_assert(offsetof(LoosePbrMaterial, alphaCutoff) == 168);

}
