// THIS IS A FILE GENERATED FROM PbrMaterial.slang. DO NOT EDIT MANUALLY
#pragma once
#include "TextureHandle.h"

namespace bgl::idl
{
	struct PbrMaterial
	{
		TextureHandle baseColorTexture;
		TextureHandle normalTexture;
		TextureHandle ormTexture;
		glm::vec4 baseColorFactor;
		float metallicFactor;
		float roughnessFactor;
	};

	static_assert(sizeof(PbrMaterial) == 36);
	static_assert(offsetof(PbrMaterial, baseColorTexture) == 0);
	static_assert(offsetof(PbrMaterial, normalTexture) == 4);
	static_assert(offsetof(PbrMaterial, ormTexture) == 8);
	static_assert(offsetof(PbrMaterial, baseColorFactor) == 12);
	static_assert(offsetof(PbrMaterial, metallicFactor) == 28);
	static_assert(offsetof(PbrMaterial, roughnessFactor) == 32);

}
