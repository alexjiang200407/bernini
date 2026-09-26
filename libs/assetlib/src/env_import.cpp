
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
#include "ref_paths.h"
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/SourceStamp.h>

namespace assetlib
{
	namespace
	{

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

		/** The container one part writes. */
		std::string
		partOutput(const EnvImportDesc& desc, EnvironmentPart part)
		{
			return part == EnvironmentPart::kSky ? assetRef(desc.skyDir, desc.name, ".bsky") :
			                                       assetRef(desc.lightingDir, desc.name, ".benvl");
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
					document.outputs.push_back(partOutput(desc, part));
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

	}

	std::vector<std::string>
	AssetStore::EnvironmentImportTargets(const EnvImportDesc& desc) const
	{
		const std::string sourceKey = importedSourceKey(desc);
		auto              out       = std::vector<std::string>{ sourceKey };

		for (const EnvironmentPart part : { EnvironmentPart::kSky, EnvironmentPart::kLighting })
			if (writes(desc, part))
				out.push_back(partOutput(desc, part));

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
		if (extension != c_EnvSourceHdrExtension && extension != c_TextureExtension)
		{
			core::throw_runtime_error(
				"AssetStore::ImportEnvironment: '{}' is neither an equirectangular '{}' nor a cube "
				"'{}'",
				desc.source.string(),
				c_EnvSourceHdrExtension,
				c_TextureExtension);
		}

		// Up front, because the convolutions take minutes and Save would not refuse a misplaced
		// `.benvl` until they were spent.
		if (desc.sky)
			requireOrigin(desc.skyDir.generic_string(), AssetOrigin::kDerived, "bsky");
		if (desc.lighting)
			requireOrigin(desc.lightingDir.generic_string(), AssetOrigin::kDerived, "benvl");
		if (desc.environment)
			requireOrigin(desc.environmentDir.generic_string(), AssetOrigin::kAuthored, "benv");

		const std::string sourceKey   = importedSourceKey(desc);
		const std::string documentKey = importDocumentKeyFor(sourceKey);
		if (!isUnder(normalizeRef(sourceKey), c_EnvSourcesDirectoryName))
		{
			core::throw_runtime_error(
				"AssetStore::ImportEnvironment: '{}': an imported environment source lives under "
				"'{}', which is where a re-import looks for it",
				sourceKey,
				c_EnvSourcesDirectoryName);
		}

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
			if (!writes(desc, part) && existing && claims(*existing, part) &&
			    stampOf(desc.source) != existing->envSourceStamp)
			{
				core::throw_runtime_error(
					"AssetStore::ImportEnvironment: '{}' is not the file '{}' was imported from, "
					"so "
					"keeping its {} would describe a different image; import both parts",
					desc.source.string(),
					sourceKey,
					part == EnvironmentPart::kSky ? "sky" : "lighting");
			}
		}

		auto created = CreatedFiles(GetDataRoot());
		auto result  = EnvImportResult();

		throwIfCancelled(cancel);

		created.WillWrite(sourceKey);

		// Stamped as it is copied, and before the cook reads it: a copy rewritten while the parts
		// convolve then reads as stale afterwards rather than as the file those pixels came from.
		const SourceStamp           copiedStamp = CopyImportedSource(desc.source, sourceKey).stamp;
		const std::filesystem::path copied      = ResolveWritePath(sourceKey);
		result.source                           = sourceKey;

		auto       input       = EnvironmentInput(copied);
		const auto beforeWrite = [&created](const std::string& key) { created.WillWrite(key); };

		if (desc.sky)
		{
			result.sky = partOutput(desc, EnvironmentPart::kSky);
			produceSky(
				*this,
				input,
				desc.parameters,
				desc.threads,
				desc.name,
				sourceKey,
				copiedStamp,
				result.sky,
				beforeWrite,
				{},
				cancel);
		}

		if (desc.lighting)
		{
			result.lighting = partOutput(desc, EnvironmentPart::kLighting);
			result.exposure = produceLighting(
				*this,
				input,
				desc.parameters,
				desc.threads,
				desc.name,
				sourceKey,
				copiedStamp,
				result.lighting,
				beforeWrite,
				{},
				cancel);
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
