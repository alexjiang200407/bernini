
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/container_info.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/envmap.h>
#include <assetlib/import_document.h>

#include <assetlib_structs/BEnv.h>

#include <cctype>
#include <core/err/util.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "env_parts.h"
#include "env_produce.h"
#include "fs_util.h"
#include "ref_paths.h"
#include <assetlib/cancel.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/SourceStamp.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_HdrExtension = ".hdr";
		constexpr std::string_view c_KtxExtension = ".ktx2";

		/**
		 * The files an import has brought into being, and the undo for them.
		 *
		 * Only files that were absent when the import reached them: one already on disk is one this
		 * import overwrote rather than made, and removing it on a failure would destroy whatever wrote
		 * it first. Nothing is removed once Commit has run.
		 */
		class CreatedFiles
		{
		public:
			explicit CreatedFiles(std::filesystem::path dataRoot) : m_DataRoot(std::move(dataRoot))
			{}

			~CreatedFiles()
			{
				if (m_Committed)
					return;

				// A rollback runs while an exception is in flight, so it cannot throw: a file that
				// will not go leaves the import failed either way, and the prune sweeps an orphan.
				for (const std::string& relative : m_Created)
				{
					std::error_code ec;
					std::filesystem::remove(m_DataRoot / relative, ec);
				}
			}

			CreatedFiles(const CreatedFiles&) = delete;
			CreatedFiles&
			operator=(const CreatedFiles&) = delete;

			/** Call immediately before writing `relative`, so "was it already there" is the truth. */
			void
			WillWrite(const std::string& relative)
			{
				if (!std::filesystem::exists(m_DataRoot / relative))
					m_Created.push_back(relative);
			}

			void
			Commit() noexcept
			{
				m_Committed = true;
			}

			[[nodiscard]] const std::vector<std::string>&
			Created() const noexcept
			{
				return m_Created;
			}

		private:
			std::filesystem::path    m_DataRoot;
			std::vector<std::string> m_Created;
			bool                     m_Committed = false;
		};

		std::string
		assetRef(const std::filesystem::path& dir, const std::string& name, const char* suffix)
		{
			return (dir / (name + suffix)).generic_string();
		}

		/** `<importedSourceDir>/<name><the source's own extension>`. */
		std::string
		importedSourceKey(const EnvImportDesc& desc)
		{
			return assetRef(
				desc.importedSourceDir,
				desc.name,
				extensionOf(desc.source.generic_string()).c_str());
		}

		/** The files one part writes, in the order it writes them. */
		std::vector<std::string>
		partOutputs(const EnvImportDesc& desc, EnvironmentPart part)
		{
			if (part == EnvironmentPart::kSky)
				return { assetRef(desc.sourceDir, desc.name, c_SkySourceSuffix.data()),
					     assetRef(desc.skyDir, desc.name, ".bsky") };

			return { assetRef(desc.sourceDir, desc.name, c_PrefilterSourceSuffix.data()),
				     assetRef(desc.sourceDir, desc.name, c_IrradianceSourceSuffix.data()),
				     assetRef(desc.lightingDir, desc.name, ".benvl") };
		}

		bool
		writes(const EnvImportDesc& desc, EnvironmentPart part)
		{
			return part == EnvironmentPart::kSky ? desc.sky : desc.lighting;
		}

		bool
		claims(const ImportDocument& document, EnvironmentPart part)
		{
			return std::ranges::any_of(document.outputs, [part](const std::string& output) {
				return isPartOutput(output, part);
			});
		}

		/**
		 * The document an import leaves: this run's parts as written, and whatever part it did not
		 * write carried over from the document already there -- its claim, its parameters and the
		 * hash they were written with -- so re-authoring a sky never forgets the lighting.
		 */
		ImportDocument
		importedDocument(
			const EnvImportDesc&                 desc,
			const std::optional<ImportDocument>& existing,
			const std::string&                   sourceKey)
		{
			ImportDocument document;
			if (existing)
			{
				document.extraJson           = existing->extraJson;
				document.extraParametersJson = existing->extraParametersJson;
			}

			document.source             = sourceKey;
			document.environment        = desc.parameters;
			document.envSourceBakeToken = c_EnvSourceBakeToken;

			for (const EnvironmentPart part : { EnvironmentPart::kSky, EnvironmentPart::kLighting })
			{
				uint64_t& hash = part == EnvironmentPart::kSky ? document.envSkyParametersHash :
				                                                 document.envLightingParametersHash;
				if (writes(desc, part))
				{
					hash = partParametersHashOf(desc.parameters, part);
					std::ranges::copy(
						partOutputs(desc, part),
						std::back_inserter(document.outputs));
					continue;
				}

				if (!existing || !existing->environment)
					continue;

				auto&       now = *document.environment;
				const auto& was = *existing->environment;
				if (part == EnvironmentPart::kSky)
				{
					now.skyFaceSize = was.skyFaceSize;
					now.skyMips     = was.skyMips;
					hash            = existing->envSkyParametersHash;
				}
				else
				{
					now.prefilterFaceSize  = was.prefilterFaceSize;
					now.prefilterMips      = was.prefilterMips;
					now.prefilterSamples   = was.prefilterSamples;
					now.irradianceFaceSize = was.irradianceFaceSize;
					hash                   = existing->envLightingParametersHash;
				}
				std::ranges::copy_if(
					existing->outputs,
					std::back_inserter(document.outputs),
					[part](const std::string& output) { return isPartOutput(output, part); });
			}

			std::ranges::sort(document.outputs);
			return document;
		}

		/**
		 * Copies the incoming file into the project. A copy onto itself is a re-import from the
		 * source already there -- the recovery path -- and copies nothing.
		 */
		void
		copySource(const std::filesystem::path& from, const std::filesystem::path& to)
		{
			std::error_code ec;
			if (std::filesystem::exists(to, ec) && std::filesystem::equivalent(from, to, ec))
				return;

			createDirectories(to.parent_path());
			std::filesystem::copy_file(
				from,
				to,
				std::filesystem::copy_options::overwrite_existing,
				ec);
			core::throw_runtime_error_if(
				static_cast<bool>(ec),
				"AssetStore::ImportEnvironment: cannot copy '{}' to '{}': {}",
				from.string(),
				to.string(),
				ec.message());
		}
	}

	std::vector<std::string>
	AssetStore::EnvironmentImportTargets(const EnvImportDesc& desc) const
	{
		const std::string sourceKey = importedSourceKey(desc);
		auto              out       = std::vector<std::string>{ sourceKey };

		for (const EnvironmentPart part : { EnvironmentPart::kSky, EnvironmentPart::kLighting })
			if (writes(desc, part))
				std::ranges::copy(partOutputs(desc, part), std::back_inserter(out));

		if (desc.environment && (desc.sky || desc.lighting))
			out.push_back(assetRef(desc.environmentDir, desc.name, ".benv"));

		out.push_back(importDocumentKeyFor(sourceKey));
		return out;
	}

	EnvImportResult
	AssetStore::ImportEnvironment(const EnvImportDesc& desc, const CancelToken& cancel) const
	{
		if (!desc.sky && !desc.lighting && !desc.environment)
			throw std::runtime_error(
				"AssetStore::ImportEnvironment: nothing was selected to write");

		// A `.benv` composes what the other two produce, so on its own it would name nothing.
		if (desc.environment && !desc.sky && !desc.lighting)
			throw std::runtime_error(
				"AssetStore::ImportEnvironment: an environment composes a sky or a lighting, so "
				"one of them has to be written with it");

		if (!std::filesystem::is_directory(GetDataRoot()))
			throw std::runtime_error(
				"AssetStore::ImportEnvironment: the data root '" + GetDataRoot().string() +
				"' is not a directory");

		if (desc.name.empty())
			throw std::runtime_error("AssetStore::ImportEnvironment: the asset name is empty");

		const std::string extension = extensionOf(desc.source.generic_string());
		core::throw_runtime_error_if(
			extension != c_HdrExtension && extension != c_KtxExtension,
			"AssetStore::ImportEnvironment: '{}' is neither an equirectangular '{}' nor a cube "
			"'{}'",
			desc.source.string(),
			c_HdrExtension,
			c_KtxExtension);

		// Up front, because the convolutions take minutes and Save would not refuse a misplaced
		// `.benvl` until they were spent. The float intermediates never reach Save at all.
		requireOrigin(
			desc.sourceDir.generic_string(),
			AssetOrigin::kDerived,
			"environment sources");
		if (desc.sky)
			requireOrigin(desc.skyDir.generic_string(), AssetOrigin::kDerived, "bsky");
		if (desc.lighting)
			requireOrigin(desc.lightingDir.generic_string(), AssetOrigin::kDerived, "benvl");
		if (desc.environment)
			requireOrigin(desc.environmentDir.generic_string(), AssetOrigin::kAuthored, "benv");

		const std::string sourceKey   = importedSourceKey(desc);
		const std::string documentKey = importDocumentKeyFor(sourceKey);
		core::throw_runtime_error_if(
			!isUnder(normalizeRef(sourceKey), c_EnvSourcesDirectoryName),
			"AssetStore::ImportEnvironment: '{}': an imported environment source lives under "
			"'{}', which is where a re-import looks for it",
			sourceKey,
			c_EnvSourcesDirectoryName);

		// One that will not parse claims nothing; the import writes a fresh document rather than
		// refusing over a file it is about to replace.
		auto existing = std::optional<ImportDocument>();
		try
		{
			if (std::filesystem::exists(GetDataRoot() / documentKey))
				existing = loadImportDocument(GetDataRoot() / documentKey);
		}
		catch (const std::exception&)
		{}

		for (const EnvironmentPart part : { EnvironmentPart::kSky, EnvironmentPart::kLighting })
		{
			core::throw_runtime_error_if(
				!writes(desc, part) && existing && claims(*existing, part) &&
					stampOf(desc.source) != existing->envSourceStamp,
				"AssetStore::ImportEnvironment: '{}' is not the file '{}' was imported from, so "
				"keeping its {} would describe a different image; import both parts",
				desc.source.string(),
				sourceKey,
				part == EnvironmentPart::kSky ? "sky" : "lighting");
		}

		// The float intermediates are written straight to the host by writeKTX2, which makes no
		// directory; the three containers go through the store, which makes its own.
		createDirectories(GetDataRoot() / desc.sourceDir);

		auto created = CreatedFiles(GetDataRoot());
		auto result  = EnvImportResult();

		throwIfCancelled(cancel);

		const std::filesystem::path copied = GetDataRoot() / sourceKey;
		created.WillWrite(sourceKey);
		copySource(desc.source, copied);
		result.source = sourceKey;

		// Stamped before the cook reads it, so a copy rewritten while the parts convolve reads as
		// stale afterwards rather than as the file those pixels came from.
		const SourceStamp copiedStamp = stampOf(copied);

		auto       input       = EnvironmentInput(copied);
		const auto beforeWrite = [&created](const std::string& key) { created.WillWrite(key); };

		if (desc.sky)
		{
			const std::vector<std::string> keys = partOutputs(desc, EnvironmentPart::kSky);
			produceSky(
				*this,
				input,
				desc.parameters,
				desc.threads,
				desc.name,
				SkyTargets{ .source = { keys[0] }, .container = { keys[1] } },
				beforeWrite,
				{},
				cancel);
			result.sky = keys[1];
		}

		if (desc.lighting)
		{
			const std::vector<std::string> keys = partOutputs(desc, EnvironmentPart::kLighting);
			result.exposure                     = *produceLighting(
				*this,
				input,
				desc.parameters,
				desc.threads,
				desc.name,
				LightingTargets{ .prefilter  = { keys[0] },
			                     .irradiance = { keys[1] },
			                     .container  = { keys[2] } },
				beforeWrite,
				{},
				cancel);
			result.lighting = keys[2];
		}

		if (desc.environment)
		{
			auto env     = BEnv();
			env.name     = desc.name;
			env.sky      = result.sky;
			env.lighting = result.lighting;

			// As requested, not clamped: the document records the person's ask, and resolution
			// clamps it against the mips the baked map actually has -- so a later, larger re-bake
			// serves the original request instead of a value shrunk to fit an older map.
			env.skyMipLevel = desc.skyMipLevel;

			result.environment = assetRef(desc.environmentDir, desc.name, ".benv");
			created.WillWrite(result.environment);
			Save(env, result.environment);
		}

		// Last: the document then cannot claim a file that was not written.
		ImportDocument document = importedDocument(desc, existing, sourceKey);
		document.envSourceStamp = copiedStamp;
		result.document         = documentKey;
		created.WillWrite(documentKey);
		Save(document, documentKey);

		result.written = created.Created();
		created.Commit();

		return result;
	}
}
