#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <core/str/str.h>
#include <gamelib/ClipInfo.h>
#include <gamelib/PreparedMesh.h>

namespace game
{
	/**
	 * Per-manager loading options, fixed at construction: a path maps to one shared material, so an
	 * option that varied per call would make what everyone shares depend on who asked first.
	 */
	struct AssetManagerOptions
	{
		/**
		 * Create every hashed-alpha material as the blend material its coverage converges to.
		 *
		 * For a consumer that renders one frame per image -- the thumbnail cache -- hashed coverage
		 * is speckle without accumulation, and blend is its converged truth. See docs/taa.md.
		 */
		bool hashedAsBlend = false;
	};

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
		 * @param scene    The scene assets are uploaded to. *Held*, not borrowed: the destructor hands
		 *                 every asset back to it, so the manager keeps it alive rather than trusting the
		 *                 caller to have declared it in an order that outlives us. Textures, materials and
		 *                 geometry belong to the scene and are shared across every view drawn from it, so
		 *                 one manager serves them all -- CreateInstance names the view each instance goes
		 *                 in, and holds it for as long as the instance lives there.
		 * @param dataRoot The project's Data directory; every path handed to this manager is relative
		 *                 to it. A standalone baked model directory is its own data root.
		 * @param options  See AssetManagerOptions.
		 *
		 * @throws bgl::SceneError if `scene` is null.
		 */
		AssetManager(
			bgl::SceneRef         scene,
			std::filesystem::path dataRoot,
			AssetManagerOptions   options = {});

		/**
		 * The mounted form: assets resolve through `store`, which may be a directory, a `.bpak`, or a
		 * loose overlay over one. The path-taking constructor above is this one over a loose store.
		 *
		 * @throws bgl::SceneError if `scene` is null.
		 */
		AssetManager(
			bgl::SceneRef        scene,
			assetlib::AssetStore store,
			AssetManagerOptions  options = {});

		/** Releases everything still held, in dependency order. */
		~AssetManager();

		AssetManager(const AssetManager&) = delete;
		AssetManager&
		operator=(const AssetManager&) = delete;

		/** The Data directory this manager writes to. */
		[[nodiscard]] const std::filesystem::path&
		DataRoot() const noexcept
		{
			return m_Store.GetDataRoot();
		}

		/** Where it reads: a directory, an archive, or a loose overlay over one. */
		[[nodiscard]] const assetlib::AssetStore&
		GetStore() const noexcept
		{
			return m_Store;
		}

		// --- Acquire: load, or share what is already loaded. Each call takes a reference. --------

		/**
		 * Uploads the `.ktx2` at `relPath`, or shares the upload from a previous call. An empty path
		 * yields an invalid handle, which the scene reads as "absent" and replaces with its default.
		 *
		 * @param prefetch Optional decoded images to upload instead of reading the file. When
		 *        supplied it is authoritative: a path it does not carry (and no previous call
		 *        uploaded) yields an invalid handle rather than a read of the file. Null means
		 *        decode here.
		 * @throws std::runtime_error if the file cannot be read or decoded (null `prefetch` only).
		 */
		bgl::TextureAssetHandle
		AcquireTexture(std::string_view relPath, TexturePrefetch* prefetch = nullptr);

		/**
		 * Creates the scene material the `.bmaterial` at `relPath` describes, or shares the one from a
		 * previous call, acquiring a reference to every texture it names.
		 *
		 * @param prefetch Optional decoded images for the textures it names -- the way to keep their
		 *        decode off the render thread. MaterialTextures() says what to put in one, and
		 *        AcquireTexture's rule applies per texture: absent from a supplied prefetch means
		 *        the default map, not a disk read.
		 * @throws std::runtime_error if the file cannot be read, or the scene cannot allocate.
		 */
		bgl::MaterialHandle
		AcquireMaterial(std::string_view relPath, TexturePrefetch* prefetch = nullptr);

		/**
		 * Uploads mesh `meshIndex` of the `.bmesh` at `relPath`, or shares the geometry from a previous
		 * call, acquiring a reference to each material its submeshes name (and thus to their textures).
		 *
		 * @throws std::runtime_error if the file cannot be read, or `meshIndex` is out of range.
		 */
		bgl::GeomHandle
		AcquireMesh(std::string_view relPath, uint32_t meshIndex = 0);

		/**
		 * The commit half of the acquire above: uploads what PrepareMesh already read, cooked and
		 * decoded, consuming it. Nothing here reads a file or flattens a mesh, so a caller on the
		 * render thread pays only the uploads.
		 *
		 * A geom already live under the same key is shared, exactly as the path-taking overload
		 * shares it, and `prepared` is dropped unused -- the cache cannot be consulted from the
		 * thread that prepared it.
		 *
		 * @throws std::runtime_error if `prepared` was made for another tier or has already been
		 *         spent; bgl::SceneError for anything AddStaticMeshGeom refuses.
		 */
		bgl::GeomHandle
		AcquireMesh(PreparedMesh prepared);

		/** An acquired VAT mesh: the geom to instance, and the clips its instances can play. */
		struct VatMesh
		{
			bgl::GeomHandle       geom;
			std::vector<ClipInfo> clips;
		};

		/**
		 * An acquired skinned mesh. Deliberately the same shape as VatMesh, down to the clip type: the
		 * two tiers describe a clip identically, and a caller that shows a clip list should not have to
		 * know which one it acquired.
		 */
		struct SkinnedMesh
		{
			bgl::GeomHandle       geom;
			std::vector<ClipInfo> clips;
		};

		/**
		 * Uploads mesh `meshIndex` of the `.bmesh` at `relPath` as VAT geometry -- pose fetched
		 * from the rig's baked texture pair instead of skinned -- or shares it from a previous
		 * call, acquiring its materials like AcquireMesh does.
		 *
		 * The pair's `.bvat` (assetlib::vatPathFor: beside the mesh, one file per clip set) is a
		 * derived build product -- a bake is seconds of CPU skinning, and the file is never
		 * committed. Missing, or out of date against the stamps of the three inputs it was baked
		 * from, it is re-baked from `relPath` + `animationsRelPath` and written to the store's
		 * writable layer, which may be an overlay that did not hold it before (see EnsureVatBaked,
		 * which owns the rule and can run the bake off the render thread).
		 *
		 * **Unless the store has nowhere to write at all.** A shipped mount is an archive `pack`
		 * baked every `.bvat` into, so what it carries is used without asking whether it is stale;
		 * one it does not carry cannot be made, and throws.
		 *
		 * While the geom is live, every acquire must name the `.banim` it was first acquired
		 * with: a shared acquire returns the cached clip table without reading the container, so
		 * switching clip sets means releasing the geom to zero first -- the eviction is what lets
		 * the freshness check see the new request.
		 *
		 * @throws std::runtime_error if an input cannot be read or the bake refuses it, or if the
		 *         geom is live with clips from a different `.banim` than the one named, or
		 *         bgl::SceneError if a submesh's material does not resolve to kPBR -- the VAT
		 *         pipeline has no unlit or loose variant, so such a mesh cannot be acquired as VAT.
		 *         A failed acquire owns nothing.
		 */
		VatMesh
		AcquireVatMesh(
			std::string_view relPath,
			std::string_view animationsRelPath,
			uint32_t         meshIndex = 0);

		/**
		 * The commit half of the acquire above, on AcquireMesh(PreparedMesh)'s terms: PrepareVatMesh
		 * has already made the bake fresh and decoded the texture pair, so all that is left here is
		 * the upload.
		 *
		 * @throws std::runtime_error if `prepared` was made for another tier or has already been
		 *         spent, or if the geom is live with clips from a different `.banim`;
		 *         bgl::SceneError for anything AddVatMeshGeom refuses.
		 */
		VatMesh
		AcquireVatMesh(PreparedMesh prepared);

		/**
		 * Uploads mesh `meshIndex` of the `.bmesh` at `relPath` as skinned geometry, or shares it from
		 * a previous call, acquiring its materials like AcquireMesh does. See
		 * [Skinned Meshes](docs/skinning.md).
		 *
		 * `animationsRelPath` names the clip set; the skeleton is the one that set was cooked against,
		 * so a caller never names it. A live geom must be re-acquired with the same `.banim`.
		 *
		 * `posedBounds` is the box the geom culls by. When it is not given, the `.banim`'s baked box
		 * is read (assetlib::findPosedBounds), and only a pairing the cook never measured falls back
		 * to measuring here (assetlib::posedBounds) -- which skins every vertex at every frame,
		 * seconds on a dense rig, so a caller that cannot block the thread it acquires on passes its
		 * own. It is not validated against the rig: an under-sized box culls the mesh early, which
		 * is the caller's mistake to make.
		 *
		 * A **shared** acquire ignores it entirely -- not even to validate it. The sphere belongs to
		 * the geom, and the geom already exists, so nothing this call passes can change it. Release
		 * the geom to zero to re-bound it.
		 *
		 * @throws std::runtime_error if an input cannot be read, `meshIndex` is out of range, the clip
		 *         set no longer matches the skeleton it names, or the geom is live with clips from a
		 *         different `.banim`; bgl::SceneError for anything AddSkinnedMeshGeom refuses. A
		 *         failed acquire owns nothing.
		 */
		SkinnedMesh
		AcquireSkinnedMesh(
			std::string_view                       relPath,
			std::string_view                       animationsRelPath,
			uint32_t                               meshIndex   = 0,
			const std::optional<assetlib::Bounds>& posedBounds = std::nullopt);

		/**
		 * The commit half of the acquire above, on AcquireMesh(PreparedMesh)'s terms: PrepareSkinnedMesh
		 * has already read the rig, checked its signature and settled the posed box, so all that is
		 * left here is the upload.
		 *
		 * @throws std::runtime_error if `prepared` was made for another tier or has already been
		 *         spent, or if the geom is live with clips from a different `.banim`;
		 *         bgl::SceneError for anything AddSkinnedMeshGeom refuses.
		 */
		SkinnedMesh
		AcquireSkinnedMesh(PreparedMesh prepared);

		/**
		 * A `.benv` followed to its uploads: one texture reference per baked map it references, plus
		 * the parameters that travel with them. A value, not a resource -- the three references are
		 * what ReleaseEnvironment gives back.
		 *
		 * Pieces the `.benv` does not reference come back as invalid handles. Loading half an
		 * environment is the caller's decision to make and not an error here -- but the scene does
		 * **not** read an invalid handle as "absent": `SetEnvironmentMap` and `SetSkyBox` both throw
		 * on one. Ask before binding, which is what HasLighting and HasSky are for.
		 */
		struct Environment
		{
			bgl::TextureAssetHandle irradiance;
			bgl::TextureAssetHandle prefilter;
			bgl::TextureAssetHandle skybox;

			float    exposure     = 1.0f;
			uint32_t skyMipLevel  = 0;
			float    skyRotationY = 0.0f;

			/**
			 * Both or neither: the two are the diffuse and specular convolutions of one radiance, so a
			 * view holding one of them would light the scene from half an environment.
			 */
			[[nodiscard]] bool
			HasLighting() const noexcept
			{
				return !irradiance.textureSlot.is_null() && !prefilter.textureSlot.is_null();
			}

			[[nodiscard]] bool
			HasSky() const noexcept
			{
				return !skybox.textureSlot.is_null();
			}
		};

		/**
		 * Loads the `.benv` at `relPath` and acquires one texture reference per map its chain names.
		 * The maps are keyed by their own paths, so two environments composing the same sky share its
		 * upload -- which is the point of the reference container.
		 *
		 * Baked or source per route, by `assetlib::envMapToDraw` -- the rule a material's baked-vs-
		 * loose branch follows.
		 *
		 * @throws std::runtime_error if a referenced file is missing or malformed, or a route has
		 *         neither a baked map nor a source on disk.
		 */
		Environment
		AcquireEnvironment(std::string_view relPath);

		/** Releases the three texture references an AcquireEnvironment took. */
		void
		ReleaseEnvironment(const Environment& environment);

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
		 * Places `geom` in `view` at `transform`. The instance holds a reference on the geometry, so
		 * geometry cannot be deleted while it is still being drawn, and holds `view` too, so the view it
		 * lives in outlives it. `view` must draw this manager's scene.
		 *
		 * @throws bgl::SceneError if `view` is null, or the geom is not one this manager owns or has
		 *         expired.
		 */
		bgl::MeshInstanceHandle
		CreateInstance(bgl::SceneViewRef view, bgl::GeomHandle geom, const glm::mat4& transform);

		/**
		 * The VAT counterpart of CreateInstance: places a geom AcquireVatMesh returned, spawned on
		 * `desc`'s clip, phase and rate. The same references are taken and the same
		 * DestroyInstance releases them.
		 *
		 * @throws bgl::SceneError if `view` is null, the geom is not this manager's or has
		 *         expired, or `desc.clip` is out of the geom's clip table.
		 */
		bgl::MeshInstanceHandle
		CreateVatInstance(
			bgl::SceneViewRef           view,
			bgl::GeomHandle             geom,
			const glm::mat4&            transform,
			const bgl::VatInstanceDesc& desc);

		/**
		 * The skinned counterpart of CreateInstance: places a geom AcquireSkinnedMesh returned, spawned
		 * on `desc`'s clip, phase and rate. The same references are taken and the same DestroyInstance
		 * releases them.
		 *
		 * @throws bgl::SceneError if `view` is null, the geom is not this manager's or has expired, or
		 *         `desc.clip` is out of the geom's clip table.
		 */
		bgl::MeshInstanceHandle
		CreateSkinnedInstance(
			bgl::SceneViewRef               view,
			bgl::GeomHandle                 geom,
			const glm::mat4&                transform,
			const bgl::SkinnedInstanceDesc& desc);

		/**
		 * Destroys `instance` in `view` and drops its reference on its geometry. `view` is the one it was
		 * created in: an instance's slot index is unique only within its view, so the manager needs both
		 * to find it.
		 */
		void
		DestroyInstance(bgl::SceneViewRef view, bgl::MeshInstanceHandle instance);

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
		 * @throws bgl::SceneError if the instance is not one this manager owns in `view`, or the submesh
		 *         index is out of range.
		 */
		void
		SetInstanceSubmeshMaterial(
			bgl::SceneViewRef       view,
			bgl::MeshInstanceHandle instance,
			uint32_t                submeshIndex,
			std::string_view        materialRelPath);

		/** Drops the override; the submesh returns to the geom's default and the material is released. */
		void
		ClearInstanceSubmeshMaterial(
			bgl::SceneViewRef       view,
			bgl::MeshInstanceHandle instance,
			uint32_t                submeshIndex);

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

			// Whether it draws from its routes rather than its baked triplet, decided once when the
			// material was created. A scene material's type is fixed for the life of its handle, so
			// re-measuring the disk later could not act on a different answer anyway.
			bool loose = false;

			uint32_t refCount = 0;
		};

		struct GeomRecord
		{
			std::string     key;  // empty for procedural geometry
			bgl::GeomHandle handle;

			// One per submesh: the material that submesh is bound to, and holds a reference to.
			std::vector<bgl::MaterialHandle> submeshMaterials;

			// VAT only: the embedded texture pair this geom fetches from (each holding a
			// reference), the clip table a shared acquire hands back without re-reading the
			// container, and the normalized .banim path those clips came from -- what a shared
			// acquire is checked against.
			std::vector<bgl::TextureAssetHandle> vatTextures;
			std::vector<ClipInfo>                vatClips;
			std::string                          vatAnimations;

			// Skinned only, and the same bargain as vatClips/vatAnimations above: the clip table a
			// shared acquire hands back without re-reading the container, and the normalized .banim
			// path it came from. No textures -- a skinned pose is computed, not fetched.
			std::vector<ClipInfo> skinnedClips;
			std::string           skinnedAnimations;

			uint32_t refCount = 0;
		};

		struct InstanceRecord
		{
			bgl::MeshInstanceHandle handle;

			// The view this instance was placed in, and is deleted from. Held, so a view cannot be
			// destroyed while the manager still has an instance to hand back to it.
			bgl::SceneViewRef view;

			uint32_t geomSlot = 0;

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

		// An instance's slot index is unique only within its view -- each view numbers its own from 0 --
		// so a scene-wide manager keys an instance by its view together with its index. The pointer is an
		// identity only; InstanceRecord::view is the reference that keeps the view alive.
		struct InstanceKey
		{
			const bgl::ISceneView* view  = nullptr;
			uint32_t               index = 0;

			[[nodiscard]] bool
			operator==(const InstanceKey&) const noexcept = default;
		};

		struct InstanceKeyHash
		{
			[[nodiscard]] size_t
			operator()(const InstanceKey& key) const noexcept
			{
				return std::hash<const void*>{}(key.view) * 31 + key.index;
			}
		};

		// Creates the scene material a BMaterial describes, acquiring a reference to every texture it
		// names. A current bake samples the optimized triplet (three reads); a stale one samples the
		// authoring routes directly (up to nine). The only place that branch lives, so a material
		// renders the same however it was loaded.
		// `loose` is assetlib::drawsLoose's verdict when it has already been taken -- nullopt
		// measures it here. A Prepare* passes its own so this touches no disk.
		bgl::MaterialHandle
		CreateMaterial(
			const assetlib::BMaterial& material,
			std::string                key,
			TexturePrefetch*           prefetch = nullptr,
			std::optional<bool>        loose    = std::nullopt);

		// AcquireMaterial over a material a Prepare* already read and measured, so nothing here
		// touches the disk. An empty `relPath` yields an invalid handle, as the path-taking one does.
		bgl::MaterialHandle
		AcquireMaterial(const PreparedMaterial& material, TexturePrefetch* prefetch);

		// The materials one prepared mesh's submeshes bind to: `materials` parallel to the source
		// `.bmesh`'s list, `submeshMaterials` one per submesh. Every handle taken is appended to
		// `acquired`, so a caller that throws afterwards can hand exactly those back.
		void
		AcquirePreparedMaterials(
			PreparedMesh&                     prepared,
			std::vector<bgl::MaterialHandle>& materials,
			std::vector<bgl::MaterialHandle>& submeshMaterials,
			std::vector<bgl::MaterialHandle>& acquired);

		// One more reference on the geom already live under `key`, or an invalid handle when none
		// is: the cache hit every acquire opens with.
		[[nodiscard]] bgl::GeomHandle
		ShareGeom(std::string_view key);

		// Files a freshly built record under `key` and hands its handle back: the tail every acquire
		// closes with.
		bgl::GeomHandle
		FileGeom(GeomRecord record, std::string key);

		// Drops one reference to a geom by its slot, destroying it at zero. Shared by ReleaseGeom and
		// DestroyInstance, which are the two things that hold geometry references.
		void
		DropGeomRef(uint32_t geomSlot);

		// Takes a reference on an already-resolved material, for the procedural geometry that is handed
		// one rather than acquiring it by path. A material this manager does not own -- created straight
		// on the scene -- is not counted, and releasing it is a no-op, which is the same bargain.
		void
		AddMaterialRef(bgl::MaterialHandle material);

		// Uploads an already-decoded image as a refcounted texture record, or shares it by `key` --
		// how a texture embedded in a container (no file of its own) joins the same identity scheme
		// as one loaded from a path.
		bgl::TextureAssetHandle
		AddEmbeddedTexture(std::string key, assetlib::ImageData image);

		// The record-keeping tail CreateInstance and CreateVatInstance share: takes the geom
		// reference and files the InstanceRecord.
		void
		RegisterInstance(
			bgl::SceneViewRef       view,
			uint32_t                geomSlot,
			bgl::MeshInstanceHandle instance);

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

		bgl::SceneRef        m_Scene;
		assetlib::AssetStore m_Store;
		AssetManagerOptions  m_Options;

		core::str::unordered_str_map<uint32_t> m_TextureByPath;
		core::str::unordered_str_map<uint64_t> m_MaterialByPath;
		core::str::unordered_str_map<uint32_t> m_GeomByPath;

		std::unordered_map<uint32_t, TextureRecord>  m_Textures;
		std::unordered_map<uint64_t, MaterialRecord> m_Materials;
		std::unordered_map<uint32_t, GeomRecord>     m_Geoms;

		std::unordered_map<InstanceKey, InstanceRecord, InstanceKeyHash> m_Instances;
	};
}
