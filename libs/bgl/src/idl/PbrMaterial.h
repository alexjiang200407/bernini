// THIS IS A FILE GENERATED FROM PbrMaterial.slang. DO NOT EDIT MANUALLY
#pragma once
#include "TextureHandle.h"

namespace bgl::idl
{
	struct PbrMaterial
	{
		glm::vec4 baseColorFactor;
		TextureHandle baseColorTexture;
		TextureHandle normalTexture;
		TextureHandle ormTexture;
		float metallicFactor;
		float roughnessFactor;
		float alphaCutoff;
		uint32_t _wgslPad[3];
	};

	static_assert(sizeof(PbrMaterial) == 64);
	static_assert(offsetof(PbrMaterial, baseColorFactor) == 0);
	static_assert(offsetof(PbrMaterial, baseColorTexture) == 16);
	static_assert(offsetof(PbrMaterial, normalTexture) == 24);
	static_assert(offsetof(PbrMaterial, ormTexture) == 32);
	static_assert(offsetof(PbrMaterial, metallicFactor) == 40);
	static_assert(offsetof(PbrMaterial, roughnessFactor) == 44);
	static_assert(offsetof(PbrMaterial, alphaCutoff) == 48);

}
