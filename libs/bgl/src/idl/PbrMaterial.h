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
		float alphaCutoff;
	};

	static_assert(sizeof(PbrMaterial) == 52);
	static_assert(offsetof(PbrMaterial, baseColorTexture) == 0);
	static_assert(offsetof(PbrMaterial, normalTexture) == 8);
	static_assert(offsetof(PbrMaterial, ormTexture) == 16);
	static_assert(offsetof(PbrMaterial, baseColorFactor) == 24);
	static_assert(offsetof(PbrMaterial, metallicFactor) == 40);
	static_assert(offsetof(PbrMaterial, roughnessFactor) == 44);
	static_assert(offsetof(PbrMaterial, alphaCutoff) == 48);

}
