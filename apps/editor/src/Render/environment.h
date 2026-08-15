#pragma once

#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace editor
{
	/** The texture assets an apply bound, so a caller replacing one can release what it displaced. */
	struct AppliedEnvironment
	{
		bgl::TextureAssetHandle irradiance;
		bgl::TextureAssetHandle prefilter;
		bgl::TextureAssetHandle skybox;
	};

	/** ApplyEnvironment's value parameters, as the block a window's desc embeds or aliases. */
	struct EnvironmentApplyDesc
	{
		std::string environmentMap;

		// What the paths inside that `.benv` resolve against. Configured rather than derived from
		// the file: an environment is not always two levels under the root it belongs to.
		// TODO: resolve through feat/archive's IFileSystem mount once it lands -- a raw data root
		// cannot see into a .bpak, and the archive branch already moves the staleness predicates
		// onto that seam.
		std::filesystem::path dataRoot;

		// Absent means the exposure the `.benv` carries, which is the value derived from those
		// maps. Set it only to overrule that deliberately.
		std::optional<float> exposureOverride;

		// A preview wants the eye on its subject, and a defocused backdrop reads as depth of field
		// where a sharp one competes for attention -- so these viewports overrule the `.bsky`'s
		// own presentation by default. A sky baked as a single mip cannot honour it and stays as
		// it is.
		std::optional<uint32_t> skyMipLevelOverride = 3;
	};

	/**
	 * Puts a `.benv`'s image-based lighting onto a view: the IBL pair, the skybox, and the exposure.
	 *
	 * Shared by the material preview and the thumbnail cache so the two cannot light the same asset
	 * differently -- a thumbnail that disagrees with the preview it was generated from is a bug that
	 * only shows up side by side.
	 *
	 * Degrades rather than throws: a missing or unreadable environment warns and binds nothing,
	 * leaving whatever the view was already lit by in place. That is deliberately survivable, because
	 * an editor that will not open is worse than one that draws dark -- but it is also why a broken
	 * path is quiet, so check the log if a viewport is black.
	 *
	 * Must be called on the render thread, like everything else that touches a scene or a view.
	 *
	 * @param benvPath The `.benv`; nothing is applied when empty.
	 * @param dataRoot What the paths inside the `.benv` chain are relative to. Passed rather than
	 *        derived from `benvPath`: an environment is not always two levels under the root -- a
	 *        subfolder, or a file dropped from anywhere -- and guessing lands on the wrong root
	 *        without saying so.
	 * @param exposureOverride Overrules the exposure the environment's lighting derived.
	 * @param skyMipLevelOverride Overrules how defocused the `.bsky` says its backdrop is drawn.
	 *        How much depth of field a backdrop wants is a property of the viewport and not of the
	 *        environment -- a material preview wants the eye on the material where a level viewport
	 *        wants the world. Clamped by the sampler to the mips the cube actually has, so asking a
	 *        single-mip sky to defocus is a no-op rather than an error.
	 * @param who Prefix for warnings, naming the caller.
	 * @return What was bound. Applying twice over one view leaks the first set's slots unless the
	 *         caller releases them -- pass both to ReplaceEnvironment.
	 */
	[[nodiscard]] AppliedEnvironment
	ApplyEnvironment(
		bgl::IScene*                 scene,
		bgl::ISceneView*             view,
		const std::string&           benvPath,
		const std::filesystem::path& dataRoot,
		std::optional<float>         exposureOverride,
		std::optional<uint32_t>      skyMipLevelOverride,
		const char*                  who);

	/**
	 * Hands back the maps an apply displaced, and only those: a map the apply did not rebind is one
	 * the view still samples every frame, so it is kept rather than released.
	 *
	 * Releasing the whole of `previous` after a failed or partial apply leaves the view naming
	 * retired slots. D3D12 survives that -- the shader indexes the heap and reads a stale
	 * descriptor -- but Metal resolves each handle to an MTLResourceID at dispatch, so the next
	 * frame aborts.
	 *
	 * Must be called on the render thread.
	 *
	 * @return What the view now names: `applied` wherever it bound something, `previous` elsewhere.
	 * @throws bgl::SceneError if a displaced handle was already deleted.
	 */
	[[nodiscard]] AppliedEnvironment
	ReplaceEnvironment(
		bgl::IScene*              scene,
		const AppliedEnvironment& previous,
		const AppliedEnvironment& applied);
}
