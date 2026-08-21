#pragma once

namespace assetlib
{
	/**
	 * Where each kind of asset lives, relative to a project's data root.
	 *
	 * One table, because the layout is what a reference means: a `.bmesh` naming
	 * `Textures/skin.ktx2` and a bake writing that file have to agree, and they are written in
	 * different places. Every default in this library that names a directory reads it from here.
	 *
	 * Deliberately free of AssetStore, so a header that only needs a directory name does not pull
	 * the mount in behind it.
	 */

	inline constexpr auto c_MeshesDirectoryName      = "Meshes";
	inline constexpr auto c_TexturesDirectoryName    = "Textures";
	inline constexpr auto c_TexturesSrcDirectoryName = "textures_src";
	// The imported .glb sources and their .bimport documents.
	inline constexpr auto c_MeshesSrcDirectoryName = "meshes_src";
	inline constexpr auto c_MaterialsDirectoryName = "Materials";
	inline constexpr auto c_LevelsDirectoryName    = "Levels";

	// One per environment container, because the three have different lifetimes: a sky is re-authored
	// in seconds, the lighting convolved from it takes minutes, and the `.benv` naming the pair is a
	// few bytes that outlives both.
	inline constexpr auto c_EnvironmentsDirectoryName = "Environments";
	inline constexpr auto c_EnvLightingDirectoryName  = "EnvLighting";
	inline constexpr auto c_SkyDirectoryName          = "Sky";

	// Split for the same reason, and a sharper one: a rig outlives its clips. Re-cooking a clip set
	// leaves the skeleton untouched, and re-authoring a rest pose does not invalidate a clip -- which
	// is what skeletonSignature exists to check, and only means anything if the two can move apart.
	inline constexpr auto c_SkeletonsDirectoryName  = "Skeletons";
	inline constexpr auto c_AnimationsDirectoryName = "Animations";

	/**
	 * Every category Project::Create scaffolds and Project::IsRequiredDirectory protects -- anything
	 * that needs to know the layout reads it here rather than restating it and drifting.
	 */
	inline constexpr std::array<std::string_view, 11> c_RequiredDirectories = { {
		c_MeshesDirectoryName,
		c_TexturesDirectoryName,
		c_TexturesSrcDirectoryName,
		c_MeshesSrcDirectoryName,
		c_MaterialsDirectoryName,
		c_LevelsDirectoryName,
		c_EnvironmentsDirectoryName,
		c_EnvLightingDirectoryName,
		c_SkyDirectoryName,
		c_SkeletonsDirectoryName,
		c_AnimationsDirectoryName,
	} };
}
