#include <assetlib/AssetStore.h>

#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/container_info.h>
#include <assetlib/import_document.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMeshImport.h>
#include <core/err/util.h>
#include <core/file/file.h>

#include "mounted_io.h"
#include "ref_paths.h"

#include <tracy/Tracy.hpp>

namespace assetlib
{
	namespace
	{
		/** The `.ktx2` files directly in `directory`, sorted. A nested folder is not this one. */
		std::vector<std::string>
		texturesDirectlyIn(const core::file::IFileSystem& files, std::string_view directory)
		{
			auto found = std::vector<std::string>();
			for (std::string& key : files.Enumerate(directory))
			{
				if (extensionOf(key) != c_TextureExtension)
					continue;
				if (key.find('/', directory.size() + 1) != std::string::npos)
					continue;
				found.push_back(std::move(key));
			}
			std::ranges::sort(found);
			return found;
		}
	}

	namespace
	{
		/**
		 * Whether `key` ends in the name the extract gave an unnamed image before it named them
		 * after their content: `tex<digits>.ktx2`, the image's position in the source.
		 *
		 * A migration sniff, and the only thing that can see a naming-rule change from outside --
		 * `textureStamp` answers for the source, not for the rule. Delete it once no project holds
		 * one; nothing else depends on it.
		 */
		bool
		isNumberedTextureName(std::string_view key)
		{
			const size_t     slash = key.rfind('/');
			std::string_view name  = slash == std::string_view::npos ? key : key.substr(slash + 1);

			if (!name.starts_with("tex") || !name.ends_with(c_TextureExtension))
				return false;

			name.remove_prefix(3);
			name.remove_suffix(c_TextureExtension.size());
			return !name.empty() &&
			       std::ranges::all_of(name, [](char c) { return c >= '0' && c <= '9'; });
		}

		/**
		 * Sorts `orphaned` into the files that merely moved -- byte-identical to exactly one of
		 * `written`, which is the same image under the name the current rule gives it -- and the
		 * ones nothing accounts for, which are returned. A move is followed through RenameAsset, so
		 * every material routed at the old key is rewritten onto the new one.
		 *
		 * Exactly one, deliberately: two identical files leave no way to say which the routes meant,
		 * and guessing is the failure the naming rule exists to prevent. Those are reported instead.
		 */
		std::vector<std::string>
		followMovedTextures(
			const AssetStore&            store,
			std::span<const std::string> written,
			std::span<const std::string> orphaned,
			std::vector<MovedTexture>&   moved)
		{
			auto unaccounted = std::vector<std::string>();
			if (orphaned.empty())
				return unaccounted;

			const auto bytesOf = [&](std::string_view key) {
				return core::file::read_file_bytes(store.ResolveWritePath(key).string());
			};

			// One walk answers every rename below, and a folder with nothing orphaned never pays it.
			const AssetRefGraph graph = AssetRefGraph::Scan(store);

			for (const std::string& stale : orphaned)
			{
				auto match = std::string();
				auto count = 0;
				try
				{
					const auto staleBytes = bytesOf(stale);
					for (const std::string& candidate : written)
						if (bytesOf(candidate) == staleBytes)
						{
							match = candidate;
							++count;
						}
				}
				catch (const std::exception&)
				{
					count = 0;
				}

				if (count != 1)
				{
					unaccounted.push_back(stale);
					continue;
				}

				RenamePlan plan;
				plan.subject.from                         = stale;
				plan.subject.to                           = match;
				plan.assetType                            = AssetType::kTexture;
				const std::span<const AssetRef> referrers = graph.ReferrersOf(stale);
				plan.referrers.assign(referrers.begin(), referrers.end());

				if (store.RenameAsset(plan).status == RenameStatus::kRenamed)
					moved.push_back({ stale, match });
				else
					unaccounted.push_back(stale);
			}

			std::ranges::sort(moved, {}, &MovedTexture::from);
			return unaccounted;
		}
	}

	std::vector<std::string>
	AssetStore::GetStaleImportedTextureSources() const
	{
		ZoneScopedN("assetlib scan stale textures");

		if (IsReadOnly())
			return {};

		auto stale = std::vector<std::string>();
		for (const std::string& key : GetFiles().Enumerate(c_MeshSourcesDirectoryName))
		{
			if (extensionOf(key) != c_ImportDocumentExtension)
				continue;

			ImportDocument document;
			try
			{
				document = loadImportDocument(GetFiles(), key);
			}
			catch (const std::exception& e)
			{
				core::throw_runtime_error(
					"'{}' cannot be read, so whether its textures are stale is unknowable: {}",
					key,
					e.what());
			}

			if (document.textureDir.empty())
				continue;

			// An absent source cannot be compared, so it stales nothing -- the rule the geometry
			// cache keys follow, which keeps a project missing its sources usable.
			const SourceStamp stamp = StampOf(importedSourceKeyFor(key));
			if (stamp != SourceStamp() && stamp != document.textureStamp)
			{
				stale.push_back(importedSourceKeyFor(key));
				continue;
			}

			// The stamp answers whether the *source* moved, and nothing answers whether the naming
			// rule did. One rule has moved: an unnamed image used to be numbered. A folder still
			// holding one of those names is a folder the current extract would not write, so it is
			// stale whatever the stamp says.
			if (std::ranges::any_of(
					texturesDirectlyIn(GetFiles(), document.textureDir),
					[](std::string_view name) { return isNumberedTextureName(name); }))
				stale.push_back(importedSourceKeyFor(key));
		}

		std::ranges::sort(stale);
		return stale;
	}

	TextureRefresh
	AssetStore::RefreshImportedTextures(
		std::string_view    sourceKey,
		const ProgressSink& onProgress,
		const CancelToken&  cancel) const
	{
		ZoneScopedN("assetlib refresh textures");
		ZoneTextF("%.*s", static_cast<int>(sourceKey.size()), sourceKey.data());

		core::throw_runtime_error_if(
			IsReadOnly(),
			"'{}': this project has nowhere to write, so its textures cannot be re-extracted",
			sourceKey);

		core::throw_runtime_error_if(
			!Exists(sourceKey),
			"'{}' is not in this project, so there is nothing to re-extract from",
			sourceKey);

		const std::string documentKey = importDocumentKeyFor(sourceKey);
		core::throw_runtime_error_if(
			!Exists(documentKey),
			"'{}': the import document beside it is gone, so where its textures went is "
			"unknowable; re-import the source",
			sourceKey);

		const ImportDocument document = loadImportDocument(GetFiles(), documentKey);
		core::throw_runtime_error_if(
			document.textureDir.empty(),
			"'{}': its import document records no texture folder, so there is nowhere to "
			"re-extract into; re-import the source",
			sourceKey);

		TextureRefresh refresh{ document.textureDir, {}, {}, {} };

		const std::vector<std::string> before = texturesDirectlyIn(GetFiles(), document.textureDir);

		// The glTF parser reads a file, and the copied source is only on the loose layer.
		const imp::BMeshImport imported = loadFromGltf(
			ResolveWritePath(sourceKey),
			{ .cancel = cancel, .sampleRate = document.sampleRate });

		refresh.written = WriteTextures(imported, document.textureDir, onProgress, cancel);
		std::ranges::sort(refresh.written);

		auto orphaned = std::vector<std::string>();
		std::ranges::set_difference(before, refresh.written, std::back_inserter(orphaned));
		refresh.superseded = followMovedTextures(*this, refresh.written, orphaned, refresh.moved);

		// Last, so a refresh that threw or was cancelled is still reported stale.
		ImportDocument advanced = document;
		advanced.textureStamp   = StampOf(sourceKey);
		Save(advanced, documentKey);

		return refresh;
	}
}
