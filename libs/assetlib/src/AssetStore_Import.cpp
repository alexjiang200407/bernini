#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <core/err/util.h>
#include <string>
#include <string_view>

namespace assetlib
{
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
