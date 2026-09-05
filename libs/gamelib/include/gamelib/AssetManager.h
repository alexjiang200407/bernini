#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <core/str/str.h>
#include <gamelib/BlendSpaceInfo.h>
#include <gamelib/ClipInfo.h>

namespace game
{
	/**
	 * The texture files `material` names, relative to the data root: the nine authoring routes when
	 * `loose`, otherwise the baked triplet. Unrouted slots come back as empty strings, so the result
	 * is positional.
	 *
	 * `loose` is `assetlib::drawsLoose` against the data root -- the caller passes the verdict in
	 * rather than it being taken here, so one material is measured against the disk once and every
	 * derived thing agrees with it.
	 *
	 * Public because decoding a texture is expensive and pure CPU, while uploading it is neither --
	 * it must happen on the render thread. A caller that wants the decode off that thread needs to
	 * know what to decode before it acquires anything. See TexturePrefetch.
	 */
	[[nodiscard]] std::vector<std::string>
	MaterialTextures(const assetlib::BMaterial& material, bool loose);

	/**
	 * Textures decoded ahead of time, keyed by the data-root-relative path they will be asked for.
	 *
	 * `assetlib::loadKTX2` transcodes a whole Basis mip chain and is the dominant cost of loading a
	 * material -- but it touches no GPU state, so it can run anywhere, unlike the upload that follows.
	 * Hand one of these to AcquireTexture / AcquireMaterial and a matching entry is consumed instead
	 * of the file being read, leaving only the upload on the render thread.
	 *
	 * Entries are moved out as they are used. A supplied prefetch is the whole truth: a path it does
	 * not carry resolves to the scene's default map (with a warning), never to a read of the file --
	 * so handing one in *is* the guarantee that the acquiring thread does no decode. A texture whose
	 * decode failed is simply left out, and was reported where it failed.
	 */
	using TexturePrefetch = core::str::unordered_str_map<assetlib::ImageData>;

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
		 * An acquired skinned mesh: the geom to instance, and the two kinds of thing its instances
		 * can play -- a clip, or a blend space the `.bblend` authored.
		 *
		 * Together they are the node table a playback slot indexes, clips first: `clips[i]` is node
		 * `i` and `spaces[i]` is node `clips.size() + i`. Two vectors rather than one of a tagged
		 * kind, because the two carry different things and are driven differently -- a clip is
		 * played, a space is played *and* steered by a parameter -- so a single table would leave
		 * half of every entry meaningless.
		 */
		struct SkinnedMesh
		{
			bgl::GeomHandle             geom;
			std::vector<ClipInfo>       clips;
			std::vector<BlendSpaceInfo> spaces;
		};

		/**
		 * Uploads mesh `meshIndex` of the `.bmesh` at `relPath` as skinned geometry, or shares it from
		 * a previous call, acquiring its materials like AcquireMesh does. See
		 * [Skinned Meshes](docs/skinning.md).
		 *
		 * `animationsRelPath` names the clip set; the skeleton is the one that set was cooked against,
		 * so a caller never names it. A live geom must be re-acquired with the same `.banim`.
		 *
		 * `blendRelPath` names a `.bblend` whose spaces become nodes after the clips, resolved
		 * against this clip set. Unlike `posedBounds` below, a **shared** acquire *is* checked
		 * against it -- a set is part of what the rig is, the way the `.banim` is, rather than a
		 * property of the geom.
		 *
		 * The rule is one-sided at both levels it is checked: naming a different set than the geom
		 * or its rig already has is refused, naming none accepts whatever they have, since a caller
		 * that asked for no spaces is not wrong to find some. A geom records the set its *rig*
		 * carries, so one that took a shared rig's spaces may name them later. Which handle to give
		 * back differs: release the geom to zero to re-acquire it against another set, and every
		 * geom on the rig to change the rig's.
		 *
		 * `posedBounds` is the box the geom culls by. When it is not given, the `.banim`'s baked box
		 * is read (assetlib::findPosedBounds), and only a pairing the cook never measured falls back
		 * to measuring here (assetlib::posedBounds) -- a pose walk rather than a vertex one, so it
		 * costs a fraction of a second even on a dense rig. It is not validated against the rig: an
		 * under-sized box culls the mesh early, which is the caller's mistake to make.
		 *
		 * A **shared** acquire ignores it entirely -- not even to validate it. The sphere belongs to
		 * the geom, and the geom already exists, so nothing this call passes can change it. Release
		 * the geom to zero to re-bound it.
		 *
		 * @throws std::runtime_error if an input cannot be read, `meshIndex` is out of range, the clip
		 *         set no longer matches the skeleton it names, the geom is live with clips from a
		 *         different `.banim`, the geom is live with a different blend set, the rig is live
		 *         with a different one, the blend set names another `.banim` than this one, or a
		 *         member names a clip the set does not hold;
		 *         bgl::SceneError for anything AddRig or AddSkinnedMeshGeom refuses -- a space of
		 *         fewer than two members, a member that does not loop, parameters that do not
		 *         strictly increase. A failed acquire owns nothing.
		 */
		SkinnedMesh
		AcquireSkinnedMesh(
			std::string_view                       relPath,
			std::string_view                       animationsRelPath,
			std::string_view                       blendRelPath = {},
			uint32_t                               meshIndex    = 0,
			const std::optional<assetlib::Bounds>& posedBounds  = std::nullopt);

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
		 * The skinned counterpart of CreateInstance: places a geom AcquireSkinnedMesh returned, spawned
		 * on `desc`'s clip, phase and rate and posed from its `source`. The same references are taken
		 * and the same DestroyInstance releases them.
		 *
		 * `PoseSource::kBoneAnimTable` reserves the rig's table on the first such instance -- see
		 * ISceneView::CreateSkinnedMeshInstance for what that costs.
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

		/**
		 * One uploaded rig, shared by every skinned geom cooked against the same clip set.
		 *
		 * Keyed on the normalized `.banim` path rather than the `.bskel`'s: a clip set names its own
		 * rig, so the two are one choice, and it is the path a skinned acquire already carries.
		 */
		struct RigRecord
		{
			bgl::RigHandle handle;
			uint32_t       refCount = 0;

			// The normalized `.bblend` this rig's spaces came from, empty when it was built with
			// none. What a later acquire naming a set is checked against -- the tables are the
			// rig's, and nothing attaches a set to one already uploaded.
			std::string blend;

			// The spaces as source-asset names, so a shared acquire answers without re-reading
			// either container.
			std::vector<BlendSpaceInfo> spaces;
		};

		struct GeomRecord
		{
			std::string     key;  // empty for procedural geometry
			bgl::GeomHandle handle;

			// One per submesh: the material that submesh is bound to, and holds a reference to.
			std::vector<bgl::MaterialHandle> submeshMaterials;

			// Skinned only: the clip table a shared acquire hands back without re-reading the
			// container, and the normalized .banim path it came from -- what a shared acquire is
			// checked against.
			std::vector<ClipInfo> skinnedClips;
			std::string           skinnedAnimations;

			// The spaces a shared acquire hands back, and the normalized `.bblend` they came
			// from -- empty when the rig was built with no set.
			std::vector<BlendSpaceInfo> skinnedSpaces;
			std::string                 skinnedBlend;

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

		// Every kind now shares one arena, so a byte offset already identifies a material on its
		// own. The type stays in the key regardless: a handle whose type disagrees with the record
		// at its offset is a caller error, and one that keys as a different record would hide it.
		[[nodiscard]] static uint64_t
		MaterialKey(bgl::MaterialHandle material) noexcept
		{
			return (static_cast<uint64_t>(material.materialType) << 32) | material.byteOffset;
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
		bgl::MaterialHandle
		CreateMaterial(
			const assetlib::BMaterial& material,
			std::string                key,
			TexturePrefetch*           prefetch = nullptr);

		// Defined in the .cpp: holding the containers by value here would need their definitions in
		// this header, which nothing else in it requires.
		struct ContainerReads;

		// The three containers a skinned acquire reads. Deserializing one is most of a second on a
		// dense rig, and a rig drawn as many meshes acquires once per mesh entry, so each is kept
		// beside the stamp it was read at and re-read only when that stamp moves.
		//
		// The cache never trusts itself: the editor authors through assetlib rather than through
		// this, so a write is invisible here and every read re-stamps.
		const assetlib::BMesh&
		ReadMesh(std::string_view path);

		const assetlib::Skeleton&
		ReadSkeleton(std::string_view path);

		const assetlib::AnimationSet&
		ReadAnimations(std::string_view path);

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

		// The record-keeping tail every CreateInstance door shares: takes the geom reference and
		// files the InstanceRecord.
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

		/**
		 * The rig for `animationsNorm`, uploaded on the first acquire and shared afterwards. One
		 * reference per skinned *geom*, not per geom reference: a geom takes one when it is built
		 * and gives it back in DestroyGeom.
		 *
		 * The upload is where a rig's feet are planted: the avatar its skeleton authors is found by
		 * convention, its legs resolved, each sole fitted to `mesh`, and the plant weights read off
		 * the `.banim` or measured when the ones there were made against something else. A rig
		 * with no avatar uploads exactly as before.
		 *
		 * `mesh` is the one in hand at that first acquire, and the soles are fitted to it alone: the
		 * legs are the rig's and go up once, so a unit assembled from slot meshes has its soles
		 * fitted to whichever mesh came first. A body and its boots disagree by the boot's thickness.
		 */
		/**
		 * A rig as a geom records it: the handle, and what the *rig* carries rather than what this
		 * call asked for. The two differ on a shared rig -- an acquire naming no set still gets the
		 * spaces the rig was built with, and recording the empty request instead would later refuse
		 * that geom its own set.
		 */
		struct AcquiredRig
		{
			bgl::RigHandle              handle;
			std::string                 blend;
			std::vector<BlendSpaceInfo> spaces;
		};

		[[nodiscard]] AcquiredRig
		AcquireRig(
			std::string_view              animationsNorm,
			std::string_view              blendNorm,
			const assetlib::Skeleton&     skeleton,
			const assetlib::AnimationSet& animations,
			const assetlib::BMesh&        mesh,
			const assetlib::BlendSet*     blendSet);

		/**
		 * `blendSet`'s spaces resolved against `animations`: a member's clip name becomes the index
		 * `bgl` takes, and `spaces` is filled with what a caller needs to steer each one.
		 *
		 * Refused rather than warned when a name resolves to nothing, unlike an avatar naming a bone
		 * the rig lacks: an avatar is found by convention beside the skeleton, so a rig that has
		 * none is ordinary, but a `.bblend` was named by the caller and silently dropping its spaces
		 * would hand back a node table missing what was asked for.
		 *
		 * @throws std::runtime_error if a member names a clip `animations` does not hold.
		 */
		[[nodiscard]] static bgl::BlendSetDesc
		BlendSetFor(
			const assetlib::AnimationSet& animations,
			const assetlib::BlendSet*     blendSet,
			std::vector<BlendSpaceInfo>&  spaces);

		/**
		 * What AddRig is handed about a rig's feet: its legs from the avatar beside its skeleton,
		 * each sole fitted to `mesh`, and a plant weight per leg per frame. Empty for a rig with no
		 * avatar, which is what AddRig reads as "plants nothing".
		 */
		[[nodiscard]] bgl::FootPlantDesc
		FootPlantFor(
			const assetlib::BMesh&        mesh,
			const assetlib::Skeleton&     skeleton,
			const assetlib::AnimationSet& animations) const;

		/**
		 * Drops one reference, deleting the rig at zero.
		 *
		 * @pre Every geom skinned to it is already deleted -- bgl refuses otherwise, which is why
		 *      this runs after DeleteGeom.
		 */
		void
		ReleaseRig(std::string_view animationsNorm);

		bgl::SceneRef        m_Scene;
		assetlib::AssetStore m_Store;
		AssetManagerOptions  m_Options;

		core::str::unordered_str_map<uint32_t> m_TextureByPath;
		core::str::unordered_str_map<uint64_t> m_MaterialByPath;
		core::str::unordered_str_map<uint32_t> m_GeomByPath;

		std::unordered_map<uint32_t, TextureRecord>  m_Textures;
		std::unordered_map<uint64_t, MaterialRecord> m_Materials;
		std::unordered_map<uint32_t, GeomRecord>     m_Geoms;

		core::str::unordered_str_map<RigRecord> m_Rigs;

		std::unique_ptr<ContainerReads> m_Reads;

		std::unordered_map<InstanceKey, InstanceRecord, InstanceKeyHash> m_Instances;
	};
}
