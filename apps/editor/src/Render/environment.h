#pragma once

#include <QStringList>
#include <bgl/TextureAssetHandle.h>

#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <qcontainerfwd.h>
#include <string>

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

	/**
	 * A viewport's environment: the one it was configured with, and the one bound over it.
	 *
	 * A drop replaces what is bound without touching what was configured, which is what lets a
	 * viewport giving up what it was showing get back to the `.benv` config.json named.
	 */
	struct EnvironmentBinding
	{
		EnvironmentApplyDesc configured;

		// The `.benv` bound now, and the maps it took: the path says whether a drop displaced the
		// configured one, the handles are what the next bind releases.
		std::string        boundPath;
		AppliedEnvironment bound;
	};

	/**
	 * What a view lit through `binding` holds open: the `.benv` bound to it, and nothing at all when
	 * it is lit by nothing.
	 *
	 * The `.bsky`, the `.benvl` and the maps beneath them are not named here. Those are held by
	 * assetlib's reference graph, which is where that rule lives -- a `.benv` is the one end of the
	 * chain nothing on disk references, so it is the one end a panel has to answer for.
	 */
	[[nodiscard]] QStringList
	GetHeldOpenEnvironment(const EnvironmentBinding& binding);

	/**
	 * The `.benv` a reset has to bind to undo a drop; nothing when there is nothing to undo.
	 *
	 * Nothing also for a viewport configured without an environment, which cannot undo one: an
	 * empty apply binds nothing and so displaces nothing, leaving the drop lit either way. That is
	 * deliberate -- the only other reading of "clear" there is an unlit preview, and black is worse
	 * than somebody else's backdrop.
	 */
	[[nodiscard]] std::optional<std::string>
	GetEnvironmentToRestore(const EnvironmentBinding& binding);

	/**
	 * Lights `view` from `benvPath` and records it in `binding`, handing back what it displaced.
	 *
	 * The pair that a viewport owning an environment wants instead of ApplyEnvironment: applying
	 * without releasing keeps every predecessor's three cube maps uploaded for the life of the
	 * window, and the scene's texture slots are bounded.
	 *
	 * Must be called on the render thread.
	 *
	 * @param dataRoot What the paths inside the `.benv` resolve against -- the open project's for a
	 *        drop, and `binding.configured`'s for the one config.json named, which is relative to
	 *        whatever that named.
	 * @throws bgl::SceneError if a displaced handle was already deleted.
	 */
	void
	BindEnvironment(
		bgl::IScene*                 scene,
		bgl::ISceneView*             view,
		EnvironmentBinding&          binding,
		const std::string&           benvPath,
		const std::filesystem::path& dataRoot,
		const char*                  who);
}
