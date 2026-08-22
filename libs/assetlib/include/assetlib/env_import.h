#pragma once
#include <assetlib/cancel.h>
#include <assetlib/project_layout.h>

namespace assetlib
{
	/**
	 * One HDRI, imported into a project as the environment family.
	 *
	 * The three outputs are separable because they are separately useful: a sky can be re-authored
	 * without paying for the lighting's convolution, and an existing `.bsky` can be given a `.benvl`
	 * later. Every path in the result is relative to `dataRoot`.
	 */
	struct EnvImportDesc
	{
		std::filesystem::path dataRoot;  // the project's Data directory
		std::filesystem::path source;    // an equirectangular `.hdr`, or a cube map `.ktx2`

		// Names every file the import writes: `Sky/<name>.bsky`, `textures_src/<name>_sky.ktx2`, ...
		std::string name = "env";

		/**
		 * Where each part lands, relative to the data root. Defaulted to the project's categories, so
		 * a caller that does not care writes the layout `Project::Create` scaffolds.
		 *
		 * A caller that does care -- the import dialog offers a folder per part -- names a
		 * subdirectory *inside* the category rather than replacing it, so the categories stay the
		 * layout every other reference is written against.
		 */
		std::filesystem::path skyDir         = c_SkyDirectoryName;
		std::filesystem::path lightingDir    = c_EnvLightingDirectoryName;
		std::filesystem::path environmentDir = c_EnvironmentsDirectoryName;
		std::filesystem::path sourceDir      = c_TexturesSrcDirectoryName;

		bool sky         = true;  // write the `.bsky`
		bool lighting    = true;  // write the `.benvl` -- the prefilter/irradiance pair
		bool environment = true;  // write the `.benv` composing whichever of the two were written

		uint32_t skyFaceSize = 512;

		// Levels in the sky's defocus chain -- see skyChain. The backdrop is always baked sharp at
		// mip 0; how defocused it is drawn is `skyMipLevel`, which a viewer may overrule.
		uint32_t skyMips = 6;

		// Which level the written `.benv` document presents. 0 is the sharp projection.
		uint32_t skyMipLevel = 0;

		uint32_t prefilterFaceSize  = 256;
		uint32_t prefilterMips      = 7;  // must match the shader's MAX_REFLECTION_LOD + 1
		uint32_t prefilterSamples   = 128;
		uint32_t irradianceFaceSize = 128;

		uint32_t threads = 0;  // 0 means hardware concurrency
	};

	/** What an import wrote, so a caller can name it, open it, or report it. */
	struct EnvImportResult
	{
		std::string sky;  // empty when that output was not requested
		std::string lighting;
		std::string environment;

		/**
		 * Every file this call brought into being, data-root relative -- not the ones it overwrote,
		 * and not the baked maps (see importEnvironment).
		 */
		std::vector<std::string> written;

		float exposure = 1.0f;  // what the lighting derived, or 1 when none was written
	};

	/**
	 * Imports `desc.source` into `desc.dataRoot` as a `.bsky`, a `.benvl` and the `.benv` composing
	 * them, writing the float intermediates into `textures_src/` as the routed sources and baking each
	 * into `Textures/`.
	 *
	 * **Rolls back on failure.** A cancelled or failed import removes the files it created, so a
	 * half-written environment is never left behind. It removes only what it *created*: a file that was
	 * already there is one this import overwrote rather than made, and destroying it would take the
	 * previous import's work with it.
	 *
	 * **Baked maps are deliberately not rolled back.** They are content-addressed and shared, so the
	 * map this import wrote may be the same file another environment already names -- and deleting one
	 * would take it out from under that. An orphan left by a failed import is exactly what
	 * `findUnusedBakedTextures` sweeps, which is the mechanism that already owns this question.
	 *
	 * @param cancel Polled between the projection, each convolution and each bake -- the four steps
	 *        worth interrupting. A cancelled import rolls back like a failed one.
	 * @throws std::runtime_error if nothing is selected, if `dataRoot` is not a directory, or if the
	 *         source cannot be read.
	 * @throws Cancelled if `cancel` is signalled.
	 */
	[[nodiscard]] EnvImportResult
	importEnvironment(const EnvImportDesc& desc, const CancelToken& cancel = {});

	/**
	 * Every file `desc` would write, data-root relative, without writing any of them.
	 *
	 * For a caller that must decide *before* importing whether it would land on something already
	 * there -- the editor refuses rather than overwrites, and cannot ask that question by trying it.
	 * Naming the files here rather than in the caller is what keeps the two from disagreeing about
	 * where an import goes.
	 *
	 * The baked maps are not included: they are content-addressed, so a collision with one is two
	 * imports agreeing on content rather than one destroying the other.
	 */
	[[nodiscard]] std::vector<std::string>
	environmentImportTargets(const EnvImportDesc& desc);
}
