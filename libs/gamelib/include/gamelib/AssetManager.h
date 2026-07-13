#pragma once
#include <assetlib_structs/BMaterial.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <core/str/str.h>

namespace game
{
	/**
	 * Owns the lifetime of everything loaded from disk into a `bgl::IScene`: textures, materials,
	 * geometry, and the instances placed from it.
	 */
	class AssetManager
	{
	public:
		/** The three maps a baked material samples. */
		enum class TextureSlot : uint32_t
		{
			kBaseColor,
			kNormal,
			kOrm,
		};

		/**
		 * @param view     The view instances are placed in; its scene is what assets are uploaded to.
		 *                 Both are *held*, not borrowed: the destructor hands every asset back to the
		 *                 scene, so the manager keeps the scene alive rather than trusting the caller to
		 *                 have declared it in an order that outlives us.
		 * @param dataRoot The project's Data directory; every path handed to this manager is relative
		 *                 to it. A standalone baked model directory is its own data root.
		 *
		 * @throws bgl::SceneError if `view` is null.
		 */
		AssetManager(bgl::SceneViewHandle view, std::filesystem::path dataRoot);

		/** Releases everything still held, in dependency order. */
		~AssetManager();

		AssetManager(const AssetManager&) = delete;
		AssetManager&
		operator=(const AssetManager&) = delete;

		/** The Data directory this manager resolves against. */
		[[nodiscard]] const std::filesystem::path&
		DataRoot() const noexcept
		{
			return m_DataRoot;
		}

		// --- Acquire: load, or share what is already loaded. Each call takes a reference. --------

		/**
		 * Uploads the `.ktx2` at `relPath`, or shares the upload from a previous call. An empty path
		 * yields an invalid handle, which the scene reads as "absent" and replaces with its default.
		 *
		 * @throws std::runtime_error if the file cannot be read or decoded.
		 */
		bgl::TextureAssetHandle
		AcquireTexture(std::string_view relPath);

		/**
		 * Creates the scene material the `.bmaterial` at `relPath` describes, or shares the one from a
		 * previous call, acquiring a reference to every texture it names.
		 *
		 * @throws std::runtime_error if the file cannot be read, or the scene cannot allocate.
		 */
		bgl::MaterialHandle
		AcquireMaterial(std::string_view relPath);

		/**
		 * Uploads mesh `meshIndex` of the `.bmesh` at `relPath`, or shares the geometry from a previous
		 * call, acquiring a reference to each material its submeshes name (and thus to their textures).
		 *
		 * @throws std::runtime_error if the file cannot be read, or `meshIndex` is out of range.
		 */
		bgl::GeomHandle
		AcquireMesh(std::string_view relPath, uint32_t meshIndex = 0);

		// --- Procedural geometry: no file, so nothing to key on and nothing to share. -----------
		// Refcounted like any other geom: released, it deletes and drops its material's reference.

		bgl::GeomHandle
		CreateCube(bgl::MaterialHandle material = {});

		bgl::GeomHandle
		CreateSphere(
			uint32_t            xSegments,
			uint32_t            ySegments,
			float               radius,
			bgl::MaterialHandle material = {});

		/**
		 * Places `geom` in the view at `transform`. The instance holds a reference on the geometry, so
		 * geometry cannot be deleted while it is still being drawn.
		 *
		 * @throws bgl::SceneError if the geom is not one this manager owns, or has expired.
		 */
		bgl::MeshInstanceHandle
		CreateInstance(bgl::GeomHandle geom, const glm::mat4& transform);

		/** Destroys the instance and drops its reference on its geometry. */
		void
		DestroyInstance(bgl::MeshInstanceHandle instance);

		// --- Release: drop one reference. At zero the asset is destroyed, and its own references
		//     are released in turn. ------------------------------------------------------------

		void
		ReleaseGeom(bgl::GeomHandle geom);

		void
		ReleaseMaterial(bgl::MaterialHandle material);

		void
		ReleaseTexture(bgl::TextureAssetHandle texture);

		// --- Swapping ---------------------------------------------------------------------------

		/**
		 * Rebinds one submesh of `geom` to the material at `materialRelPath`, acquiring the new
		 * material and releasing the one the submesh held. Affects every instance of that geometry --
		 * a geom's submeshes are shared by all of them.
		 */
		void
		SetSubmeshMaterial(
			bgl::GeomHandle  geom,
			uint32_t         submeshIndex,
			std::string_view materialRelPath);

		/**
		 * Overrides one submesh of ONE instance with the material at `materialRelPath`, leaving the
		 * geom's default -- and every other instance of it -- alone. A cosmetic skin.
		 *
		 * Acquires the new material and releases the override this instance held, so the override is a
		 * reference like any other: the material cannot be destroyed while an instance still wears it.
		 * That is what makes bgl's raw-slot binding safe (see ISceneView::SetSubmeshMaterialOverride).
		 *
		 * @throws bgl::SceneError if the instance is not one this manager owns, or the submesh index
		 *         is out of range.
		 */
		void
		SetInstanceSubmeshMaterial(
			bgl::MeshInstanceHandle instance,
			uint32_t                submeshIndex,
			std::string_view        materialRelPath);

		/** Drops the override; the submesh returns to the geom's default and the material is released. */
		void
		ClearInstanceSubmeshMaterial(bgl::MeshInstanceHandle instance, uint32_t submeshIndex);

		/**
		 * Swaps one map of a *baked* material, acquiring the new texture and releasing the old.
		 *
		 * The material is rewritten in place, so its handle stays valid and every submesh bound to it
		 * follows the change with no rebinding. Note the material is shared by path: this changes it
		 * for everything using it, which is the point.
		 *
		 * @throws bgl::SceneError if the material is not a baked one this manager owns.
		 */
		void
		SetMaterialTexture(
			bgl::MaterialHandle material,
			TextureSlot         slot,
			std::string_view    relPath);

		/**
		 * The loose counterpart of SetMaterialTexture: swaps the source of one of the nine authoring
		 * channels (see `assetlib::c_LooseChannelCount`).
		 *
		 * @throws bgl::SceneError if the material is not a loose one this manager owns, or `channel`
		 *         is out of range.
		 */
		void
		SetMaterialRoute(
			bgl::MaterialHandle material,
			uint32_t            channel,
			std::string_view    relPath,
			uint16_t            sourceChannel);

		// 0 if not owned
		[[nodiscard]] uint32_t
		TextureRefCount(bgl::TextureAssetHandle texture) const noexcept;

		// 0 if not owned
		[[nodiscard]] uint32_t
		MaterialRefCount(bgl::MaterialHandle material) const noexcept;

		// 0 if not owned
		[[nodiscard]] uint32_t
		GeomRefCount(bgl::GeomHandle geom) const noexcept;

	private:
		struct TextureRecord
		{
			std::string             key;
			bgl::TextureAssetHandle handle;
			uint32_t                refCount = 0;
		};

		struct MaterialRecord
		{
			std::string                          key;  // empty when not loaded from a path
			bgl::MaterialHandle                  handle;
			assetlib::BMaterial                  source;
			std::vector<bgl::TextureAssetHandle> textures;

			uint32_t refCount = 0;
		};

		struct GeomRecord
		{
			std::string     key;  // empty for procedural geometry
			bgl::GeomHandle handle;

			// One per submesh: the material that submesh is bound to, and holds a reference to.
			std::vector<bgl::MaterialHandle> submeshMaterials;

			uint32_t refCount = 0;
		};

		struct InstanceRecord
		{
			bgl::MeshInstanceHandle handle;
			uint32_t                geomSlot = 0;

			// Per submesh, the material this instance overrides its geom's default with. Invalid means
			// none. Each valid one holds a reference, released by ClearInstanceSubmeshMaterial or by
			// DestroyInstance -- the same edge as every other reference, just one level lower:
			// instance -> material, alongside instance -> geom -> material.
			std::vector<bgl::MaterialHandle> overrides;
		};

		// A baked and a loose material live in *different* buffers, so their slot indices collide --
		// both start at 0. The record key has to carry the type as well as the index.
		[[nodiscard]] static uint64_t
		MaterialKey(bgl::MaterialHandle material) noexcept
		{
			return (static_cast<uint64_t>(material.materialType) << 32) | material.handle.index;
		}

		// Creates the scene material a BMaterial describes, honouring its `mode`, and acquiring a
		// reference to every texture it names. kBaked samples the optimized triplet (three reads);
		// kLoose samples the authoring routes directly (up to nine). The only place that branch lives,
		// so a material renders the same however it was loaded.
		bgl::MaterialHandle
		CreateMaterial(const assetlib::BMaterial& material, std::string key);

		// Drops one reference to a geom by its slot, destroying it at zero. Shared by ReleaseGeom and
		// DestroyInstance, which are the two things that hold geometry references.
		void
		DropGeomRef(uint32_t geomSlot);

		// Takes a reference on an already-resolved material, for the procedural geometry that is handed
		// one rather than acquiring it by path. A material this manager does not own -- created straight
		// on the scene -- is not counted, and releasing it is a no-op, which is the same bargain.
		void
		AddMaterialRef(bgl::MaterialHandle material);

		// Rebuilds `record`'s scene material from its (just-edited) `source`, swapping the texture
		// references it holds to match. Used by both swap entry points.
		void
		RebuildMaterial(MaterialRecord& record);

		[[nodiscard]] bgl::PbrMaterialDesc
		BakedDesc(const MaterialRecord& record) const;

		[[nodiscard]] bgl::LoosePbrMaterialDesc
		LooseDesc(const MaterialRecord& record) const;

		// Destroys a geom and releases the materials it held. Assumes its refcount reached zero
		void
		DestroyGeom(GeomRecord& record);

		bgl::SceneViewHandle  m_View;
		bgl::SceneHandle      m_Scene;
		std::filesystem::path m_DataRoot;

		core::str::unordered_str_map<uint32_t> m_TextureByPath;
		core::str::unordered_str_map<uint64_t> m_MaterialByPath;
		core::str::unordered_str_map<uint32_t> m_GeomByPath;

		std::unordered_map<uint32_t, TextureRecord>  m_Textures;
		std::unordered_map<uint64_t, MaterialRecord> m_Materials;
		std::unordered_map<uint32_t, GeomRecord>     m_Geoms;
		std::unordered_map<uint32_t, InstanceRecord> m_Instances;
	};
}
