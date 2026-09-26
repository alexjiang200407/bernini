#include "ImportHost.h"
#include <assetlib/AssetKindRegistry.h>
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/asset_refs.h>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// Only this executable defines these fakes. They prove client wiring and ownership, not naming,
// randomness, codecs, validation or source-free production IO; those need implementation tests.
namespace assetlib
{
	AssetStore::AssetStore(
		std::filesystem::path                          dataRoot,
		std::shared_ptr<const core::file::IFileSystem> files,
		std::shared_ptr<const AssetKindRegistry>       registry) :
		m_DataRoot(std::move(dataRoot)), m_Registry(std::move(registry)), m_Files(std::move(files))
	{}

	ResolvedImport
	AssetStore::ResolveImport(std::string_view sourceKey, AssetType kind) const
	{
		const auto& host = dynamic_cast<const test::ImportHost&>(GetFiles());
		if (!host.failure.empty())
			core::throw_runtime_error("{}", host.failure);
		const auto found = host.imports.find({ std::string(sourceKey), kind });
		if (found == host.imports.end())
			core::throw_runtime_error("contract: no such output");
		return found->second;
	}

	RegenMesh
	AssetStore::LoadRegenMesh(std::string_view key) const
	{
		const auto& host = dynamic_cast<const test::ImportHost&>(GetFiles());
		if (!host.failure.empty())
			core::throw_runtime_error("{}", host.failure);
		const auto found = host.meshes.find(std::string(key));
		if (found == host.meshes.end())
			core::throw_runtime_error("contract: missing cooked mesh");
		return found->second;
	}

	ImportIdentity
	makeImportIdentity(std::string_view sourceKey)
	{
		if (sourceKey != "Authored/Meshes/street.glb")
			core::throw_runtime_error("contract: no identity fixture");
		return { 0x3f9a1c7e0b24d5a6ull, "street.glb" };
	}

	std::string
	importOutputKey(const ImportIdentity& identity, AssetType kind)
	{
		if (identity != ImportIdentity{ 0x3f9a1c7e0b24d5a6ull, "street.glb" })
			core::throw_runtime_error("contract: no naming fixture");
		if (kind == AssetType::kMesh)
			return "Derived/Meshes/street.glb-3f9a1c7e0b24d5a6.bmesh";
		core::throw_runtime_error("contract: no output fixture for kind");
	}

	std::string
	importTextureDirectory(const ImportIdentity& identity)
	{
		if (identity != ImportIdentity{ 0x3f9a1c7e0b24d5a6ull, "street.glb" })
			core::throw_runtime_error("contract: no texture-directory fixture");
		return "Derived/SourceTextures/street.glb-3f9a1c7e0b24d5a6";
	}
}
