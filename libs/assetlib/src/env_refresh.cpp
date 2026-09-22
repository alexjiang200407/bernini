#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/import_document.h>
#include <assetlib/progress.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

#include "env_parts.h"
#include "env_produce.h"
#include "progress_report.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		struct StaleParts
		{
			bool sky      = false;
			bool lighting = false;

			[[nodiscard]] bool
			Any() const noexcept
			{
				return sky || lighting;
			}
		};

		uint64_t&
		writtenHash(ImportDocument& document, EnvironmentPart part)
		{
			return part == EnvironmentPart::kSky ? document.envSkyParametersHash :
			                                       document.envLightingParametersHash;
		}

		uint64_t
		writtenHash(const ImportDocument& document, EnvironmentPart part)
		{
			return part == EnvironmentPart::kSky ? document.envSkyParametersHash :
			                                       document.envLightingParametersHash;
		}

		/**
		 * Which of the parts `document` claims no longer match it: all of them when the copied
		 * source or the bake's revision moved, and otherwise each one whose parameters were
		 * edited since it was written. An absent source stales nothing, the rule the geometry
		 * cache keys follow.
		 *
		 * @param stamp The source as it stands, taken by the caller so the one that decides this is
		 *        the one a refresh records.
		 */
		StaleParts
		staleParts(const SourceStamp& stamp, const ImportDocument& document)
		{
			if (!document.environment || stamp == SourceStamp())
				return {};

			const bool sourceMoved = stamp != document.envSourceStamp ||
			                         document.envSourceBakeToken != c_EnvSourceBakeToken;

			const auto stale = [&](EnvironmentPart part) {
				const bool claimed =
					std::ranges::any_of(document.outputs, [part](const std::string& output) {
						return isPartOutput(output, part);
					});
				return claimed &&
				       (sourceMoved || partParametersHashOf(*document.environment, part) !=
				                           writtenHash(document, part));
			};

			return { .sky      = stale(EnvironmentPart::kSky),
				     .lighting = stale(EnvironmentPart::kLighting) };
		}
	}

	std::vector<std::string>
	AssetStore::GetStaleEnvironmentSources() const
	{
		ZoneScopedN("assetlib scan stale environments");

		if (IsReadOnly())
			return {};

		auto stale = std::vector<std::string>();
		for (const std::string& key : GetFiles().Enumerate(c_EnvSourcesDirectoryName))
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
					"'{}' cannot be read, so whether its environment is stale is unknowable: {}",
					key,
					e.what());
			}

			const std::string sourceKey = importedSourceKeyFor(key, document);
			if (staleParts(StampOf(sourceKey), document).Any())
				stale.push_back(sourceKey);
		}

		std::ranges::sort(stale);
		return stale;
	}

	std::vector<std::string>
	AssetStore::RefreshEnvironmentSource(
		std::string_view    sourceKey,
		const ProgressSink& onProgress,
		const CancelToken&  cancel) const
	{
		ZoneScopedN("assetlib refresh environment");
		ZoneTextF("%.*s", static_cast<int>(sourceKey.size()), sourceKey.data());

		core::throw_runtime_error_if(
			IsReadOnly(),
			"'{}': this project has nowhere to write, so its environment cannot be re-cooked",
			sourceKey);

		core::throw_runtime_error_if(
			!Exists(sourceKey),
			"'{}' is not in this project, so there is nothing to re-cook from",
			sourceKey);

		const std::string documentKey = importDocumentKeyFor(sourceKey);
		core::throw_runtime_error_if(
			!Exists(documentKey),
			"'{}': the import document beside it is gone, so how its environment was made is "
			"unknowable; re-import the source",
			sourceKey);

		// Taken once, before the cook: a source rewritten while a part convolves must read as stale
		// again afterwards, never as the file those pixels were made from.
		const SourceStamp    stamp    = StampOf(sourceKey);
		const ImportDocument document = loadImportDocument(GetFiles(), documentKey);
		const StaleParts     stale    = staleParts(stamp, document);
		if (!stale.Any())
			return {};

		auto wanted = std::vector<std::string>();
		for (const std::string& output : document.outputs)
			if ((stale.sky && isPartOutput(output, EnvironmentPart::kSky)) ||
			    (stale.lighting && isPartOutput(output, EnvironmentPart::kLighting)))
				wanted.push_back(output);

		auto written = std::vector<std::string>();
		auto step    = size_t(0);
		produceEnvironmentOutputs(
			*this,
			std::string(sourceKey),
			document,
			wanted,
			[&](const std::string& key) {
				reportStep(onProgress, ProgressPhase::kRegenerating, key, step++, wanted.size());
			},
			[&](const std::string& key) { written.push_back(key); },
			cancel);

		// Last, so a refresh that threw or was cancelled is still reported stale. The stamp and the
		// revision only move when they had moved, and then every claimed part was just re-cooked.
		ImportDocument advanced     = document;
		advanced.envSourceStamp     = stamp;
		advanced.envSourceBakeToken = c_EnvSourceBakeToken;
		for (const auto& [part, refreshed] :
		     { std::pair{ EnvironmentPart::kSky, stale.sky },
		       std::pair{ EnvironmentPart::kLighting, stale.lighting } })
			if (refreshed)
				writtenHash(advanced, part) = partParametersHashOf(*advanced.environment, part);
		Save(advanced, documentKey);

		std::ranges::sort(written);
		return written;
	}
}
