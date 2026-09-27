#include "AssetStore/import_index.h"  // IWYU pragma: keep -- completes AssetStore's private ImportIndex
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/project_layout.h>
#include <core/err/util.h>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace assetlib
{
	std::optional<ResolvedImport>
	AssetStore::FindImportForOutput(std::string_view outputKey) const
	{
		const auto output = normalizePath(outputKey);
		requireInsideDataRoot("FindImportForOutput", output);
		const std::lock_guard lock(m_ImportIndex->mutex);
		const auto            readOwner = [&]() -> std::optional<ResolvedImport> {
			const auto found = m_ImportIndex->documents.find(output);
			if (found == m_ImportIndex->documents.end() || !Exists(found->second))
				return {};
			auto document = Load<ImportDocument>(found->second);
			if (std::ranges::find(document.outputs, output) == document.outputs.end())
				return {};
			return ResolvedImport{ found->second, output, std::move(document) };
		};
		if (m_ImportIndex->initialized)
			if (auto found = readOwner())
				return found;

		auto documents = core::str::unordered_str_map<std::string>();
		for (const auto category : { c_MeshSourcesDirectoryName, c_EnvSourcesDirectoryName })
			for (const auto& key : GetFiles().Enumerate(category))
			{
				if (!key.ends_with(".bimport"))
					continue;
				const auto document = Load<ImportDocument>(key);
				for (const auto& claimed : document.outputs)
				{
					const auto normalized = normalizePath(claimed);
					requireInsideDataRoot("import output", normalized);
					const auto [found, inserted] = documents.emplace(normalized, key);
					if (!inserted && found->second != key)
						core::throw_runtime_error(
							"Import output '{}' is claimed by '{}' and '{}'",
							normalized,
							found->second,
							key);
				}
			}
		m_ImportIndex->documents   = std::move(documents);
		m_ImportIndex->initialized = true;
		return readOwner();
	}

	ResolvedImport
	AssetStore::ResolveImport(std::string_view sourceKey, AssetType kind) const
	{
		const auto source = normalizePath(sourceKey);
		requireInsideDataRoot("ResolveImport", source);
		auto result        = ResolvedImport();
		result.documentKey = importDocumentKeyFor(source);
		result.document    = Load<ImportDocument>(result.documentKey);
		if (normalizePath(result.document.source) != source)
			core::throw_runtime_error(
				"ResolveImport: '{}' does not own source '{}'",
				result.documentKey,
				source);
		const auto expected = importOutputKey(result.document.identity, kind);
		for (const auto& output : result.document.outputs)
		{
			if (assetTypeFromExtension(output) != kind)
				continue;
			if (!result.outputKey.empty())
				core::throw_runtime_error(
					"ResolveImport: '{}' has ambiguous outputs",
					result.documentKey);
			if (output != expected)
				core::throw_runtime_error(
					"ResolveImport: '{}' has incorrectly named output '{}'",
					result.documentKey,
					output);
			result.outputKey = output;
		}
		if (result.outputKey.empty())
			core::throw_runtime_error(
				"ResolveImport: '{}' has no requested output",
				result.documentKey);
		return result;
	}
}
