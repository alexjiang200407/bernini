#pragma once
#include <core/glm.h>

namespace assetlib
{
	/**
	 * The modular, on-disk material: the file counterpart of imp::BMaterialImport. Where the import
	 * form indexes directly into imp::BMeshImport::textures, this references its textures by file path
	 * (empty string when absent), so textures live as standalone assets that a BMesh can share and
	 * re-link. This is the in-memory form of a `.bmaterial` file.
	 */
	struct BMaterial
	{
		std::string baseColorTexture;  // path to the base-color texture file (empty when absent)
		std::string normalTexture;     // path to the normal texture file (empty when absent)
		std::string ormTexture;        // path to the occlusion/roughness/metallic texture file
		glm::vec4   baseColorFactor = glm::vec4(1.0f);
		float       metallicFactor  = 1.0f;
		float       roughnessFactor = 1.0f;
		std::string name;
	};
}
