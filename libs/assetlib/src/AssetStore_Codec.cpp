#include <assetlib/AssetStore.h>

#include <assetlib/env_bake.h>
#include <assetlib/material_bake.h>
#include <assetlib/project_layout.h>

#include "fs_util.h"

namespace assetlib
{
	std::vector<std::byte>
	AssetStore::ReadBytes(std::string_view path) const
	{
		return m_Files->Read(path);
	}

	void
	AssetStore::WriteBytes(
		std::string_view           path,
		std::span<const std::byte> bytes,
		std::string_view           what) const
	{
		writeFileBytes(ResolveWritePath(path), bytes, what);
	}

	void
	AssetStore::BakeMaterial(BMaterial& material, const CancelToken& cancel) const
	{
		BakeMaterial(material, c_TexturesDirectoryName, cancel);
	}

	void
	AssetStore::BakeMaterial(
		BMaterial&         material,
		std::string_view   textureDir,
		const CancelToken& cancel) const
	{
		bakeMaterial(material, { .dataRoot = m_DataRoot, .textureDir = textureDir }, cancel);
	}

	void
	AssetStore::BakeSky(BSky& sky, const CancelToken& cancel) const
	{
		BakeSky(sky, c_TexturesDirectoryName, cancel);
	}

	void
	AssetStore::BakeSky(BSky& sky, std::string_view textureDir, const CancelToken& cancel) const
	{
		bakeSky(sky, { .dataRoot = m_DataRoot, .textureDir = textureDir }, cancel);
	}

	void
	AssetStore::BakeEnvLighting(BEnvLighting& lighting, const CancelToken& cancel) const
	{
		BakeEnvLighting(lighting, c_TexturesDirectoryName, cancel);
	}

	void
	AssetStore::BakeEnvLighting(
		BEnvLighting&      lighting,
		std::string_view   textureDir,
		const CancelToken& cancel) const
	{
		bakeEnvLighting(lighting, { .dataRoot = m_DataRoot, .textureDir = textureDir }, cancel);
	}
}
