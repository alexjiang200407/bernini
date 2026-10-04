#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/project_layout.h>
#include <core/err/util.h>
#include <cstdint>
#include <format>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace assetlib
{
	namespace
	{
		void
		requireLabel(std::string_view label)
		{
			if (label.empty() || label == "." || label == ".." ||
			    label.find_first_of("/\\:") != std::string_view::npos || label.contains('\0'))
				core::throw_runtime_error("import identity: '{}' is not a filename", label);
		}

		std::string
		identityName(const ImportIdentity& identity)
		{
			requireLabel(identity.label);
			if (identity.id == 0)
				core::throw_runtime_error("import identity: zero id requires migration");
			return std::format("{}-{:016x}", identity.label, identity.id);
		}
	}

	ImportIdentity
	makeImportIdentity(std::string_view sourceKey)
	{
		const auto source = normalizePath(sourceKey);
		requireInsideDataRoot("import identity", source);
		const auto label = std::string_view(source).substr(source.find_last_of('/') + 1);
		requireLabel(label);
		std::random_device                      entropy;
		std::uniform_int_distribution<uint64_t> ids(1, std::numeric_limits<uint64_t>::max());
		return { ids(entropy), std::string(label) };
	}

	std::string
	importOutputKey(const ImportIdentity& identity, AssetType kind)
	{
		const auto name = identityName(identity);
		switch (kind)
		{
		case AssetType::kMesh:
			return std::format("{}/{}.bmesh", c_MeshesDirectoryName, name);
		case AssetType::kSkeleton:
			return std::format("{}/{}.bskel", c_SkeletonsDirectoryName, name);
		case AssetType::kAnimation:
			return std::format("{}/{}.banim", c_AnimationsDirectoryName, name);
		case AssetType::kSky:
			return std::format("{}/{}.bsky", c_SkyDirectoryName, name);
		case AssetType::kEnvLighting:
			return std::format("{}/{}.benvl", c_EnvLightingDirectoryName, name);
		case AssetType::kMaterial:
		case AssetType::kTexture:
		case AssetType::kEnvironment:
		case AssetType::kImportDocument:
		case AssetType::kUiDocument:
		case AssetType::kUiStyle:
		case AssetType::kFont:
		case AssetType::kAvatar:
		case AssetType::kBlend:
		case AssetType::kGrass:
		case AssetType::kToonShadingRig:
		case AssetType::kCount:
			break;
		}
		core::throw_runtime_error(
			"import identity: unsupported output kind {}",
			std::to_underlying(kind));
	}

	std::string
	importTextureDirectory(const ImportIdentity& identity)
	{
		return std::format("{}/{}", c_SourceTexturesDirectoryName, identityName(identity));
	}
}
