#include <assetlib/AssetStore.h>
#include <assetlib/avatar.h>

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <string>

#include "asset_describe.h"

namespace assetlib
{
	std::string
	AssetStore::Describe(const BMaterial& material) const
	{
		return describe(material, m_Files.get());
	}

	std::string
	AssetStore::Describe(const BSky& sky) const
	{
		return describe(sky, m_Files.get());
	}

	std::string
	AssetStore::Describe(const BEnvLighting& lighting) const
	{
		return describe(lighting, m_Files.get());
	}

	std::string
	AssetStore::Describe(const BEnv& env) const
	{
		return describe(env, m_Files.get());
	}

	std::string
	AssetStore::Describe(const BMesh& mesh, bool verbose) const
	{
		return describe(mesh, verbose);
	}

	std::string
	AssetStore::Describe(const Skeleton& skeleton) const
	{
		return describe(skeleton);
	}

	std::string
	AssetStore::Describe(const AnimationSet& animations, const Skeleton* skeleton) const
	{
		return describe(animations, skeleton);
	}

	std::string
	AssetStore::Describe(const Avatar& avatar, const Skeleton* skeleton) const
	{
		return describe(avatar, skeleton);
	}
}
