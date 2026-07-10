#pragma once
#include <assetlib_structs/BMaterial.h>
#include <bgl/IScene.h>
#include <core/str/str.h>

namespace game
{
	/**
	 * Loads on-disk assets into a `bgl::IScene`.
	 */
	class AssetFactory
	{
	public:
		/**
		 * @param scene    The scene assets are uploaded into. Must outlive the factory.
		 * @param dataRoot The project's Data directory; every path handed to this factory is relative
		 *        to it. A standalone baked model directory is its own data root.
		 */
		AssetFactory(bgl::IScene& scene, std::filesystem::path dataRoot);

		AssetFactory(const AssetFactory&) = delete;
		AssetFactory&
		operator=(const AssetFactory&) = delete;

		/** The Data directory this factory resolves against. */
		[[nodiscard]] const std::filesystem::path&
		DataRoot() const noexcept
		{
			return m_DataRoot;
		}

		/**
		 * Uploads the `.ktx2` at `relPath`, or returns the handle from a previous call. An empty path
		 * yields an invalid handle, which the scene reads as "absent" and replaces with its default.
		 *
		 * @throws std::runtime_error if the file cannot be read or decoded.
		 */
		bgl::TextureAssetHandle
		LoadTexture(const std::string& relPath);

		/**
		 * Loads the `.bmaterial` at `relPath` and creates the scene material it describes, or returns
		 * the handle from a previous call.
		 *
		 * @throws std::runtime_error if the file cannot be read, or the scene cannot allocate.
		 */
		bgl::MaterialHandle
		LoadMaterial(const std::string& relPath);

		/**
		 * Creates the scene material a `BMaterial` describes, honouring its `mode`:
		 *
		 * - `kBaked` samples the optimized baseColor / normal / orm triplet -- three texture reads.
		 * - `kLoose` samples the per-channel `routes` directly, with no bake -- up to nine.
		 *
		 * This is the only place that branch lives, so a material renders the same however it is loaded.
		 * Not cached: the caller holds no path to key on. Prefer LoadMaterial where there is one.
		 */
		bgl::MaterialHandle
		CreateMaterial(const assetlib::BMaterial& material);

	private:
		bgl::IScene&          m_Scene;
		std::filesystem::path m_DataRoot;

		// Keyed on the data-root-relative path. Transparent hashing, so a lookup by string_view or a
		// literal does not have to build a std::string first.
		core::str::unordered_str_map<bgl::TextureAssetHandle> m_Textures;
		core::str::unordered_str_map<bgl::MaterialHandle>     m_Materials;
	};
}
