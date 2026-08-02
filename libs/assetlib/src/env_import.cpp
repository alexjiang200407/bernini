#include <assetlib/env_import.h>

#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/envmap_bake.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

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
		sourceRef(const std::string& name, const char* suffix)
		{
			return (std::filesystem::path("textures_src") / (name + suffix)).generic_string();
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

	EnvImportResult
	importEnvironment(const EnvImportDesc& desc, const CancelToken& cancel)
	{
		if (!desc.sky && !desc.lighting && !desc.environment)
			throw std::runtime_error("assetlib::importEnvironment: nothing was selected to write");

		// A `.benv` composes what the other two produce, so on its own it would name nothing.
		if (desc.environment && !desc.sky && !desc.lighting)
			throw std::runtime_error(
				"assetlib::importEnvironment: an environment composes a sky or a lighting, so one "
				"of "
				"them has to be written with it");

		if (!std::filesystem::is_directory(desc.dataRoot))
			throw std::runtime_error(
				"assetlib::importEnvironment: the data root '" + desc.dataRoot.string() +
				"' is not a directory");

		if (desc.name.empty())
			throw std::runtime_error("assetlib::importEnvironment: the asset name is empty");

		createDirectories(desc.dataRoot / "textures_src");
		if (desc.sky)
			createDirectories(desc.dataRoot / "Sky");
		if (desc.lighting)
			createDirectories(desc.dataRoot / "EnvLighting");
		if (desc.environment)
			createDirectories(desc.dataRoot / "Environments");

		auto created = CreatedFiles(desc.dataRoot);
		auto result  = EnvImportResult();

		throwIfCancelled(cancel);

		// Projected at the skybox's size, which is the largest of the three: the prefilter and the
		// irradiance convolve it down anyway, so starting them from the finer cube costs only the
		// projection.
		const auto      faceSize = (std::max)(desc.skyFaceSize, desc.prefilterFaceSize);
		const ImageData source   = isHdr(desc.source) ?
		                               equirectToCube(loadRadianceHdr(desc.source), faceSize) :
		                               loadKTX2(desc.source);

		if (desc.sky)
		{
			throwIfCancelled(cancel);

			// Blurred, the backdrop is its own convolution of the environment; sharp, it is the
			// projection itself. Either way the lighting below still reads `source`.
			const ImageData blurred =
				desc.skyBlur > 0.0f ?
					blurCube(source, desc.skyFaceSize, desc.skyBlur, 256, desc.threads) :
					ImageData();

			const std::string ref = sourceRef(desc.name, "_sky.ktx2");
			writeSource(desc.dataRoot, created, ref, desc.skyBlur > 0.0f ? blurred : source);

			auto bsky       = BSky();
			bsky.name       = desc.name;
			bsky.sky.source = ref;

			throwIfCancelled(cancel);
			bakeSky(bsky, { desc.dataRoot });

			result.sky = ("Sky/" + desc.name + ".bsky");
			created.WillWrite(result.sky);
			saveSky(bsky, desc.dataRoot / result.sky);
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

			const std::string prefilterRef  = sourceRef(desc.name, "_prefilter.ktx2");
			const std::string irradianceRef = sourceRef(desc.name, "_irradiance.ktx2");
			writeSource(desc.dataRoot, created, prefilterRef, prefilter);
			writeSource(desc.dataRoot, created, irradianceRef, irradiance);

			auto lighting              = BEnvLighting();
			lighting.name              = desc.name;
			lighting.prefilter.source  = prefilterRef;
			lighting.irradiance.source = irradianceRef;

			throwIfCancelled(cancel);
			bakeEnvLighting(lighting, { desc.dataRoot });

			result.lighting = ("EnvLighting/" + desc.name + ".benvl");
			created.WillWrite(result.lighting);
			saveEnvLighting(lighting, desc.dataRoot / result.lighting);

			result.exposure = lighting.exposure;
		}

		if (desc.environment)
		{
			auto env     = BEnv();
			env.name     = desc.name;
			env.sky      = result.sky;
			env.lighting = result.lighting;

			result.environment = ("Environments/" + desc.name + ".benv");
			created.WillWrite(result.environment);
			saveEnv(env, desc.dataRoot / result.environment);
		}

		result.written = created.Created();
		created.Commit();

		return result;
	}
}
