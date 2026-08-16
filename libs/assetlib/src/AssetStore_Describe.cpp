#include <assetlib/AssetStore.h>

#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BVat.h>

#include "mounted_io.h"

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
	AssetStore::Describe(const BVat& vat) const
	{
		return describe(vat, m_Files.get());
	}
}
