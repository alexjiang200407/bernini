#include <assetlib/AssetStore.h>

#include <assetlib/asset_import.h>
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

	std::vector<std::string>
	AssetStore::GetStaleImportedTextureSources() const
	{
		if (IsReadOnly())
			return {};

		auto stale = std::vector<std::string>();
		for (const std::string& key : GetFiles().Enumerate(c_MeshesSrcDirectoryName))
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
				stale.push_back(importedSourceKeyFor(key));
		}

		std::ranges::sort(stale);
		return stale;
	}

	TextureRefresh
	AssetStore::RefreshImportedTextures(
		std::string_view         sourceKey,
		const TextureProgressFn& onProgress,
		const CancelToken&       cancel) const
	{
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

		TextureRefresh refresh{ document.textureDir, {}, {} };

		const std::vector<std::string> before = texturesDirectlyIn(GetFiles(), document.textureDir);

		// The glTF parser reads a file, and the copied source is only on the loose layer.
		const imp::BMeshImport imported = loadFromGltf(
			ResolveWritePath(sourceKey),
			{ .cancel = cancel, .sampleRate = document.sampleRate });

		WriteTextures(imported, document.textureDir, onProgress, cancel);

		for (const std::string& name : importedTextureFileNames(imported))
			refresh.written.push_back(document.textureDir + "/" + name);
		std::ranges::sort(refresh.written);

		std::ranges::set_difference(
			before,
			refresh.written,
			std::back_inserter(refresh.superseded));

		// Last, so a refresh that threw or was cancelled is still reported stale.
		ImportDocument advanced = document;
		advanced.textureStamp   = StampOf(sourceKey);
		Save(advanced, documentKey);

		return refresh;
	}
}
