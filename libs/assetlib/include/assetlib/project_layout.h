#pragma once

namespace assetlib
{
	/**
	 * Where each kind of asset lives, relative to a project's data root.
	 *
	 * One table, because the layout is what a reference means: a `.bmesh` naming
	 * `Derived/BakedTextures/skin.ktx2` and a bake writing that file have to agree, and they are
	 * written in different places. Every default in this library that names a directory reads it
	 * from here.
	 *
	 * Deliberately free of AssetStore, so a header that only needs a directory name does not pull
	 * the mount in behind it.
	 */

	/**
	 * The data root's two halves, and the rule the whole layout rests on: a file under `Authored/`
	 * is one a person decided, and losing it loses work; a file under `Derived/` is one a bake or an
	 * import computed, and a stale or missing one is regenerated rather than repaired. The same
	 * split `CacheEntryCodecFor` draws over the codecs, projected onto the disk -- so a project's
	 * commit rule is a directory rather than a list of extensions.
	 *
	 * `Derived/Sky` and `Derived/EnvLighting` are the exception a project's `.gitignore` has to
	 * carry: they are cache entries whose bake reads a `.hdr` no project copies in, so nothing puts
	 * an absent one back. See [Asset Containers](docs/asset_containers.md).
	 */
	inline constexpr auto c_AuthoredDirectoryName = "Authored";
	inline constexpr auto c_DerivedDirectoryName  = "Derived";

	// The imported .glb sources and their .bimport documents.
	inline constexpr auto c_MeshSourcesDirectoryName  = "Authored/Meshes";
	inline constexpr auto c_MaterialsDirectoryName    = "Authored/Materials";
	inline constexpr auto c_EnvironmentsDirectoryName = "Authored/Environments";
	inline constexpr auto c_LevelsDirectoryName       = "Authored/Levels";

	// The UI runtime's documents and stylesheets, and the fonts they name. Authored: a person wrote
	// each one, and no bake puts one back.
	inline constexpr auto c_UiDirectoryName    = "Authored/UI";
	inline constexpr auto c_FontsDirectoryName = "Authored/Fonts";

	// One rig's authored half, mirroring `Derived/Skeletons` name for name: an avatar is found from
	// its `.bskel` by swapping the half and the extension, so the two directories are one layout.
	inline constexpr auto c_AvatarsDirectoryName = "Authored/Skeletons";

	inline constexpr auto c_MeshesDirectoryName     = "Derived/Meshes";
	inline constexpr auto c_SkeletonsDirectoryName  = "Derived/Skeletons";
	inline constexpr auto c_AnimationsDirectoryName = "Derived/Animations";

	// Two texture directories, split by which side of a bake a file is on: what a bake reads -- the
	// images a mesh import unpacks and the float cubes an environment import projects -- and the
	// compressed maps it writes. Only the second is what a prune sweeps and what ships.
	inline constexpr auto c_SourceTexturesDirectoryName = "Derived/SourceTextures";
	inline constexpr auto c_BakedTexturesDirectoryName  = "Derived/BakedTextures";

	// One per environment container, because the three have different lifetimes: a sky is re-authored
	// in seconds, the lighting convolved from it takes minutes, and the `.benv` naming the pair is a
	// few bytes that outlives both.
	inline constexpr auto c_EnvLightingDirectoryName = "Derived/EnvLighting";
	inline constexpr auto c_SkyDirectoryName         = "Derived/Sky";

	/**
	 * Every category Project::Create scaffolds and Project::IsRequiredDirectory protects -- anything
	 * that needs to know the layout reads it here rather than restating it and drifting.
	 *
	 * The halves themselves are not listed: they are the parents of everything here, and
	 * IsRequiredDirectory reads them from the two constants above.
	 */
	inline constexpr std::array<std::string_view, 14> c_RequiredDirectories = { {
		c_MeshSourcesDirectoryName,
		c_MaterialsDirectoryName,
		c_EnvironmentsDirectoryName,
		c_LevelsDirectoryName,
		c_UiDirectoryName,
		c_FontsDirectoryName,
		c_AvatarsDirectoryName,
		c_MeshesDirectoryName,
		c_SkeletonsDirectoryName,
		c_AnimationsDirectoryName,
		c_SourceTexturesDirectoryName,
		c_BakedTexturesDirectoryName,
		c_EnvLightingDirectoryName,
		c_SkyDirectoryName,
	} };

	/** Whether a file is one a person decided or one a bake computed -- and so which half of the
	 * data root holds it. */
	enum class AssetOrigin
	{
		kAuthored,
		kDerived
	};

	/**
	 * The origin `key`'s location says it has, or nullopt when it says neither -- a key at the data
	 * root, or one naming a half itself rather than something inside it.
	 *
	 * `key` is normalized first, so `Derived/../Authored/Materials/x.bmaterial` is authored.
	 */
	[[nodiscard]] std::optional<AssetOrigin>
	originOf(std::string_view key) noexcept;

	/**
	 * @throws std::runtime_error unless `key` is in the half `origin` belongs in, which is what
	 *         makes the layout an invariant rather than a convention. `what` names the container in
	 *         the message.
	 */
	void
	requireOrigin(std::string_view key, AssetOrigin origin, std::string_view what);
}
