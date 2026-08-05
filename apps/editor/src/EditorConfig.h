#pragma once

#include <QString>

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

namespace core
{
	class Settings;
}

/**
 * Everything the editor reads out of `config.json`, in one place and with one default each.
 *
 * Split by what it describes. **Machine**: which device to create and how large its pools are --
 * true of the box, not of the work. **Workspace**: what to open and what to light the previews with.
 *
 * Every default lives in this struct's member initialisers, so what an absent key gives you can be
 * read without also reading the parser.
 */
struct EditorConfig
{
	// --- Machine ----------------------------------------------------------------------------

	bgl::GraphicsOptions graphics;

	/**
	 * The editor's one Scene. Every viewport -- the Level Editor, the Material Editor's model
	 * preview -- draws it through a SceneView of its own, so geometry, textures and materials are
	 * pooled here once and these budgets must cover all of them together.
	 *
	 * Stated here rather than taken from `bgl::SceneDesc`, whose every default is 1: that is the
	 * right floor for a library that cannot know what it will hold, and unusable for an editor that
	 * opens a project on launch.
	 */
	bgl::SceneDesc scene = {
		.initialGeom                 = 256,
		.initialMeshlets             = 32768,
		.initialIndices              = 2000000,
		.initialSubmeshes            = 512,
		.initialVertexBufferByteSize = 33554432,
		.initialPbrMaterials         = 256,
		.initialLoosePbrMaterials    = 256,
	};

	// --- Workspace --------------------------------------------------------------------------

	/**
	 * A project to open on launch, so working on one does not mean reopening it every run. May name
	 * an absolute path, and may begin with `~`: the file is machine-local.
	 */
	std::string startupProject;

	/** What a viewport lights itself with. Shared shape, configured per window. */
	struct Environment
	{
		std::string environmentMap;

		// What the paths inside that `.benv` resolve against. Configured rather than derived from
		// the file: an environment is not always two levels under the root it belongs to.
		std::filesystem::path dataRoot;

		// Absent means the exposure the `.benv` carries, which is the one derived from its maps.
		std::optional<float> exposureOverride;
	};

	struct LevelEditor
	{
		uint32_t initialInstances = 1000;

		// Per viewport, not graphics-wide: it sizes what this window's render target allocates.
		bool temporalAA = true;
	} levelEditor;

	struct MaterialEditor
	{
		uint32_t    initialPreviewInstances = 16;
		bool        temporalAA              = true;
		Environment environment;
	} materialEditor;

	struct Thumbnails
	{
		uint32_t initialInstances = 256;
		uint32_t dimension        = 256;

		// No temporalAA: the thumbnail cache renders too few frames to converge, so it is never
		// offered the choice.
		Environment environment;
	} thumbnails;

	/**
	 * Reads `settings`, warning about anything it will not use rather than failing.
	 *
	 * A value that cannot be honoured -- a pool of zero, a thumbnail zero pixels across -- falls back
	 * to the default and says so.
	 */
	[[nodiscard]] static EditorConfig
	Parse(const core::Settings& settings);
};
