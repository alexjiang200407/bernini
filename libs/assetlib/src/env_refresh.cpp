#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/import_document.h>
#include <assetlib/progress.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

#include "MountedFileReader.h"
#include "cache_io.h"
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

		bool
		outputNeedsRefresh(
			const AssetStore&  store,
			const std::string& output,
			EnvironmentPart    part,
			std::string_view   sourceKey)
		{
			if (!store.Exists(output))
				return false;

			const bool     sky = part == EnvironmentPart::kSky;
			const uint32_t magic =
				sky ? AssetCodec<BSky>::c_Magic : AssetCodec<BEnvLighting>::c_Magic;
			const uint64_t token =
				sky ? AssetCodec<BSky>::c_BakeToken : AssetCodec<BEnvLighting>::c_BakeToken;
			const std::string_view what = sky ? "bsky" : "benvl";
			try
			{
				MountedFileReader reader(store.GetFiles(), output, what);
				if (cache::peekKey(reader, magic, what).bakeToken != token)
					return true;
				if (sky)
				{
					auto value = store.Load<BSky>(output);
					if (!sourceKey.empty())
						value.sky.source = sourceKey;
					return store.IsSkyBakeStale(value);
				}
				auto value = store.Load<BEnvLighting>(output);
				if (!sourceKey.empty())
					value.prefilter.source = value.irradiance.source = sourceKey;
				return store.IsEnvLightingBakeStale(value);
			}
			catch (const std::exception&)
			{
				return true;
			}
		}

		/**
		 * Which of the parts `document` claims no longer match it: all of them when the copied
		 * source or the bake's revision moved, and otherwise each one whose parameters were
		 * edited since it was written or whose container is on disk at another revision. An
		 * absent source stales nothing, the rule the geometry cache keys follow.
		 *
		 * @param stamp The source as it stands, taken by the caller so the one that decides this is
		 *        the one a refresh records.
		 */
		StaleParts
		staleParts(
			const AssetStore&     store,
			const SourceStamp&    stamp,
			const ImportDocument& document)
		{
			if (!document.environment || stamp == SourceStamp())
				return {};

			const bool sourceMoved = stamp != document.envSourceStamp ||
			                         document.envSourceBakeToken != c_EnvSourceBakeToken;

			const auto stale = [&](EnvironmentPart part) {
				bool claimed  = false;
				bool unusable = false;
				for (const std::string& output : document.outputs)
				{
					if (!isPartOutput(output, part))
						continue;
					claimed  = true;
					unusable = unusable || outputNeedsRefresh(store, output, part, document.source);
				}
				return claimed && (sourceMoved || unusable ||
				                   partParametersHashOf(*document.environment, part) !=
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
			if (staleParts(*this, StampOf(sourceKey), document).Any())
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

		if (IsReadOnly())
		{
			core::throw_runtime_error(
				"'{}': this project has nowhere to write, so its environment cannot be re-cooked",
				sourceKey);
		}

		if (!Exists(sourceKey))
		{
			core::throw_runtime_error(
				"'{}' is not in this project, so there is nothing to re-cook from",
				sourceKey);
		}

		const std::string documentKey = importDocumentKeyFor(sourceKey);
		if (!Exists(documentKey))
		{
			core::throw_runtime_error(
				"'{}': the import document beside it is gone, so how its environment was made is "
				"unknowable; re-import the source",
				sourceKey);
		}

		// Taken once, before the cook: a source rewritten while a part convolves must read as stale
		// again afterwards, never as the file those pixels were made from.
		const SourceStamp    stamp    = StampOf(sourceKey);
		const ImportDocument document = loadImportDocument(GetFiles(), documentKey);
		const StaleParts     stale    = staleParts(*this, stamp, document);
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
