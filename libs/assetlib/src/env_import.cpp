
#include <assetlib/AssetStore.h>
#include <assetlib/envmap.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include <spdlog/spdlog.h>

#include "fs_util.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_HdrExtension = ".hdr";

		// The suffix decides how the source is read, case-insensitively: what a file is named has
		// nothing to do with the case someone typed it in.
		bool
		isHdr(const std::filesystem::path& path)
		{
			std::string ext = path.extension().string();
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return ext == c_HdrExtension;
		}

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

		/** Writes `image` as an uncompressed float `.ktx2`, recording it if it is a new file. */
		void
		writeSource(
			const std::filesystem::path& dataRoot,
			CreatedFiles&                created,
			const std::string&           relative,
			const ImageData&             image)
		{
			created.WillWrite(relative);
			writeKTX2(image, dataRoot / relative, false, Ktx2Compression::kNone);
		}
	}

	std::vector<std::string>
	AssetStore::EnvironmentImportTargets(const EnvImportDesc& desc) const
	{
		auto out = std::vector<std::string>();

		if (desc.sky)
		{
			out.push_back(assetRef(desc.sourceDir, desc.name, "_sky.ktx2"));
			out.push_back(assetRef(desc.skyDir, desc.name, ".bsky"));
		}

		if (desc.lighting)
		{
			out.push_back(assetRef(desc.sourceDir, desc.name, "_prefilter.ktx2"));
			out.push_back(assetRef(desc.sourceDir, desc.name, "_irradiance.ktx2"));
			out.push_back(assetRef(desc.lightingDir, desc.name, ".benvl"));
		}

		if (desc.environment && (desc.sky || desc.lighting))
			out.push_back(assetRef(desc.environmentDir, desc.name, ".benv"));

		return out;
	}

	EnvImportResult
	AssetStore::ImportEnvironment(const EnvImportDesc& desc, const CancelToken& cancel) const
	{
		if (!desc.sky && !desc.lighting && !desc.environment)
			throw std::runtime_error("assetlib::importEnvironment: nothing was selected to write");

		// A `.benv` composes what the other two produce, so on its own it would name nothing.
		if (desc.environment && !desc.sky && !desc.lighting)
			throw std::runtime_error(
				"assetlib::importEnvironment: an environment composes a sky or a lighting, so one "
				"of "
				"them has to be written with it");

		if (!std::filesystem::is_directory(GetDataRoot()))
			throw std::runtime_error(
				"assetlib::importEnvironment: the data root '" + GetDataRoot().string() +
				"' is not a directory");

		if (desc.name.empty())
			throw std::runtime_error("assetlib::importEnvironment: the asset name is empty");

		// The float intermediates are written straight to the host by writeKTX2, which makes no
		// directory; the three containers go through the store, which makes its own.
		createDirectories(GetDataRoot() / desc.sourceDir);

		auto created = CreatedFiles(GetDataRoot());
		auto result  = EnvImportResult();

		throwIfCancelled(cancel);

		// Projected at the skybox's size, which is the largest of the three: the prefilter and the
		// irradiance convolve it down anyway, so starting them from the finer cube costs only the
		// projection.
		const auto faceSize = (std::max)(desc.skyFaceSize, desc.prefilterFaceSize);
		ImageData  source   = isHdr(desc.source) ?
		                          equirectToCube(loadRadianceHdr(desc.source), faceSize) :
		                          loadKTX2(desc.source);

		// A shipped map is RGB9E5, and that is the only form left when a route's float source has
		// gone. Re-convolving one costs a generation of quantization, so it is a recovery path and
		// not the one to reach for when the source is still there.
		if (source.vkFormat == VkFormat::E5B9G9R9_UFLOAT_PACK32)
		{
			spdlog::warn(
				"'{}' is RGB9E5; unpacking it to float. Re-convolving a baked map quantizes twice "
				"-- prefer the source it was baked from",
				desc.source.string());
			source = unpackRgb9e5(source);
		}

		if (desc.sky)
		{
			throwIfCancelled(cancel);

			// A chain, never a single blurred mip: the backdrop's defocus is presentation, so it
			// belongs on the `.benv` document where a viewer can change it. The lighting still
			// reads `source`.
			//
			// Clamped rather than refused: a sky too small for the requested chain is a small sky,
			// not a bad request, and the levels it can carry are still the ones a viewer would ask
			// for.
			const auto maxMips =
				static_cast<uint32_t>(std::bit_width(std::max(desc.skyFaceSize, 1u)));
			const uint32_t skyMips = std::clamp(desc.skyMips, 1u, maxMips);

			const ImageData chain = skyChain(source, desc.skyFaceSize, skyMips, 256, desc.threads);

			const std::string ref = assetRef(desc.sourceDir, desc.name, "_sky.ktx2");
			writeSource(GetDataRoot(), created, ref, chain);

			auto bsky       = BSky();
			bsky.name       = desc.name;
			bsky.sky.source = ref;

			throwIfCancelled(cancel);
			BakeSky(bsky);

			result.sky = assetRef(desc.skyDir, desc.name, ".bsky");
			created.WillWrite(result.sky);
			Save(bsky, result.sky);
		}

		if (desc.lighting)
		{
			throwIfCancelled(cancel);
			const ImageData irradiance = irradianceSh(source, desc.irradianceFaceSize);

			auto prefilterDesc      = PrefilterDesc();
			prefilterDesc.faceSize  = desc.prefilterFaceSize;
			prefilterDesc.mipLevels = desc.prefilterMips;
			prefilterDesc.samples   = desc.prefilterSamples;
			prefilterDesc.threads   = desc.threads;

			throwIfCancelled(cancel);
			const ImageData prefilter = prefilterRadiance(source, prefilterDesc);

			const std::string prefilterRef = assetRef(desc.sourceDir, desc.name, "_prefilter.ktx2");
			const std::string irradianceRef =
				assetRef(desc.sourceDir, desc.name, "_irradiance.ktx2");
			writeSource(GetDataRoot(), created, prefilterRef, prefilter);
			writeSource(GetDataRoot(), created, irradianceRef, irradiance);

			auto lighting              = BEnvLighting();
			lighting.name              = desc.name;
			lighting.prefilter.source  = prefilterRef;
			lighting.irradiance.source = irradianceRef;

			throwIfCancelled(cancel);
			BakeEnvLighting(lighting);

			result.lighting = assetRef(desc.lightingDir, desc.name, ".benvl");
			created.WillWrite(result.lighting);
			Save(lighting, result.lighting);

			result.exposure = lighting.exposure;
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

		result.written = created.Created();
		created.Commit();

		return result;
	}
}
