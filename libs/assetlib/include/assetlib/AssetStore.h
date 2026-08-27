#pragma once
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/BMeshImport.h>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct BSky;
	struct BVat;
	struct DeletionPlan;
	struct DeletionResult;
	struct EnvImportDesc;
	struct EnvImportResult;
	struct ImageData;
	struct ImportTarget;
	struct MeshRefs;
	struct MigrateReport;
	struct ReimportReport;
	struct PackDesc;
	struct PackReport;
	struct ReauthoredDocument;
	struct RebakeBoundsReport;
	struct RenamePlan;
	struct RenameResult;
	struct ResolvedEnvironment;
	struct Skeleton;
	struct SourceStamp;
	struct TexturePruneDesc;
	struct TexturePruneResult;
	struct TexturePruneScan;
	struct TextureRefresh;
	struct VatBakeDesc;
	struct VatRefs;
	struct VatSize;
	struct BMesh;
	struct BMaterial;
	struct BEnvLighting;
	struct BEnv;
	struct AnimationSet;
	struct EnvMapRoute;

	enum class Ktx2Decode : uint32_t;

	struct RegenMesh;
	struct SourceRef;

	/**
	 * One project's assets: what they are read from, and what a change to one is written to.
	 *
	 * Two halves, because they are two things. Reads resolve through a **mount**, which may be a
	 * loose directory, a `.bpak`, or a loose overlay over one. Writes land on the **data root**, the
	 * loose layer -- an archive entry cannot be unlinked or replaced in place, so a bake, a rename
	 * and a delete all address the directory even when the read that preceded them did not.
	 *
	 * The pair travelled together through this library as two parameters before it was a type, and
	 * every function that held both had to say which half it meant. Holding them here says it once.
	 *
	 * Every path a method takes is a mount key: data-root-relative, `/`-separated, already
	 * normalized. A `std::filesystem::path` is a different thing and does not belong here -- see
	 * STYLE.md's Paths section, and the free functions that still take one, for arbitrary files on
	 * the host that no project owns.
	 */
	class AssetStore
	{
	public:
		/**
		 * A loose project: reads and writes both address `dataRoot`.
		 *
		 * @throws std::runtime_error if `dataRoot` is not a directory. A mount over one that is not
		 *         there enumerates empty rather than failing, so this is the last place a mistyped
		 *         root can be told from a project with nothing in it.
		 */
		explicit AssetStore(std::filesystem::path dataRoot);

		/**
		 * Reads resolve through `files`, writes land on `dataRoot`.
		 *
		 * `dataRoot` should be a writable layer of `files` -- typically the loose half of a
		 * `LayeredFileSystem` whose other half is an archive. Nothing checks that, because nothing
		 * can: a mount does not say where its layers came from.
		 *
		 * @throws std::runtime_error if `files` is null.
		 */
		AssetStore(
			std::filesystem::path                          dataRoot,
			std::shared_ptr<const core::file::IFileSystem> files);

		/** What reads resolve through. */
		[[nodiscard]] const core::file::IFileSystem&
		GetFiles() const noexcept
		{
			return *m_Files;
		}

		/** What writes address. */
		[[nodiscard]] const std::filesystem::path&
		GetDataRoot() const noexcept
		{
			return m_DataRoot;
		}

		/**
		 * Whether nothing in the mount can be written.
		 *
		 * The whole mount's answer, not one path's: the question a caller asks is "is there anywhere
		 * to put a rebuilt derived file", and a loose overlay over an archive answers yes even for a
		 * path only the archive currently carries -- the rebuild lands in the overlay.
		 */
		[[nodiscard]] bool
		IsReadOnly() const noexcept
		{
			return m_Files->IsReadOnly();
		}

		/**
		 * Where a write to `path` lands on disk: the data root joined with the key.
		 *
		 * The one place a mount key legitimately becomes a host path. Reads go through the mount,
		 * but an archive entry cannot be rewritten, so anything that edits an asset in place has to
		 * address the loose layer directly.
		 *
		 * @throws std::runtime_error unless `path` names something strictly inside the data root, so
		 *         a key typed on a command line cannot climb out of the project.
		 */
		[[nodiscard]] std::filesystem::path
		ResolveWritePath(std::string_view path) const;

		/**
		 * The mount key for a host path inside the data root -- the inverse of ResolveWritePath.
		 *
		 * For the few callers that legitimately hold a host path and need a key back: the pack
		 * walks the loose tree with a directory iterator, and a rename plan names files as they sit
		 * on disk. Everything else should be holding the key already and never make the round trip.
		 *
		 * @throws std::runtime_error unless `path` is inside the data root, which is the same
		 *         boundary ResolveWritePath enforces in the other direction.
		 */
		[[nodiscard]] std::string
		KeyFor(const std::filesystem::path& path) const;

		/** Whether the mount answers for `path` at all. */
		[[nodiscard]] bool
		Exists(std::string_view path) const
		{
			return m_Files->Exists(path);
		}

		// --- Containers, by codec ----------------------------------------------------------------

		/**
		 * The container at `path`, decoded by `T`'s codec.
		 *
		 * One template rather than a method per container: the type says which codec to use, so a
		 * container is registered once (in its `AssetCodec` specialization) instead of once here as
		 * well. `store.Load<BMesh>("Derived/Meshes/a.bmesh")`.
		 *
		 * @throws std::runtime_error if the container is absent, unreadable or malformed -- the
		 *         codec's own `deserialize` decides which.
		 */
		template <AssetCodecFor T>
		[[nodiscard]] T
		Load(std::string_view path) const
		{
			return AssetCodec<T>::Deserialize(ReadBytes(path));
		}

		/**
		 * Writes `value` to `path`, encoded by `T`'s codec.
		 *
		 * The write lands on the data root, as every write does -- a caller passes the same mount
		 * key it would read by and never resolves one itself, which is the asymmetry this closes.
		 * Directories the key names are created; a caller never makes them.
		 *
		 * @throws std::runtime_error if `path` names the wrong half of the data root for `T` (see
		 *         requireOrigin), escapes the data root (see ResolveWritePath), or cannot be written.
		 *         Atomic: a crash mid-write leaves the previous bytes.
		 */
		template <AssetCodecFor T>
		void
		Save(const T& value, std::string_view path) const
		{
			// The extension without its dot is the container's name, which is what every message
			// this library throws is prefixed with -- "bmaterial: ...", not ".bmaterial: ...".
			const std::string_view what = AssetCodec<T>::c_Extension.substr(1);

			requireOrigin(path, originFor<T>(), what);
			WriteBytes(path, AssetCodec<T>::Serialize(value), what);
		}

		// --- Bakes ---------------------------------------------------------------------------

		/**
		 * Composites `material`'s routes down to its baked triplet, in place, writing the maps into
		 * `c_BakedTexturesDirectoryName`.
		 *
		 * Where the maps land is the layout's to decide, not a caller's: the routes this writes into
		 * `material` are references, and one naming a directory outside `Derived/` would be a
		 * reference the split does not cover. The store is the data root it reads and writes
		 * relative to, so it is not passed one either.
		 *
		 * A material that routes nothing comes back untouched rather than failing: its factors are
		 * the whole description, which is the same verdict BakeIsStale reaches about one.
		 *
		 * @throws std::runtime_error / Cancelled as bakeMaterial.
		 */
		void
		BakeMaterial(BMaterial& material, const CancelToken& cancel = {}) const;

		/**
		 * Writes `material`'s triplet the way BakeMaterial would, and encodes nothing.
		 *
		 * A map is named for everything that determines its bytes, so what a bake *would* produce
		 * costs a stamp of each source and no decode -- which is what tells a dry run whether a bake
		 * has anything to do. Unlike the other Resolve methods this mutates its argument, and the
		 * names it writes may be of maps that are not on disk: that is the answer, not a failure.
		 *
		 * @throws std::runtime_error as bakeMaterial. Not cancellable -- there is nothing long
		 *         enough to interrupt.
		 */
		void
		ResolveMaterialBake(BMaterial& material) const;

		/** @throws std::runtime_error / Cancelled as bakeSky. */
		void
		BakeSky(BSky& sky, const CancelToken& cancel = {}) const;

		/** @throws std::runtime_error / Cancelled as bakeEnvLighting. */
		void
		BakeEnvLighting(BEnvLighting& lighting, const CancelToken& cancel = {}) const;

		/**
		 * bakeVat over this project: loads the mesh, the skeleton it names and the clip set, bakes,
		 * and records the three keys and their SourceStamps -- what `VatIsStale` later compares.
		 * Writing the result is the caller's (`Save`): a `.bvat` is a derived build product, and
		 * where it lands is the caller's convention, not this one's.
		 *
		 * @throws std::runtime_error if an input cannot be read, if the mesh names no skeleton, or
		 *         for anything the in-memory `bakeVat` refuses.
		 */
		[[nodiscard]] BVat
		BakeVat(const VatBakeDesc& desc) const;

		/**
		 * What BakeVat would write, over the same three inputs loaded the same way -- for a caller
		 * that must offer the bake before paying for it. Loading is all it costs; nothing is
		 * skinned.
		 *
		 * @throws what BakeVat throws.
		 */
		[[nodiscard]] VatSize
		VatBakeSize(const VatBakeDesc& desc) const;

		// --- Import writes -----------------------------------------------------------------------

		/**
		 * Writes an import's rig, and its clips when asked, and points `mesh` at the `.bskel`.
		 *
		 * A joint index is a bare number into a bone array, so a mesh carrying joints while naming
		 * no skeleton is one `Save` refuses outright; the clips are the half a user can decline.
		 * Does nothing when `skeleton` has no bones, which is what a static mesh is.
		 *
		 * @return The containers it put on disk -- the `.bskel` only when this import is what wrote
		 *         it, and the `.banim` when it wrote clips. A rig it bound rather than produced is
		 *         named by `mesh.skeleton` and is deliberately not here: deleting this source must
		 *         not take another source's rig with it.
		 *
		 * @throws std::runtime_error if either container cannot be written.
		 */
		std::vector<std::string>
		WriteImportedRig(
			const Skeleton&     skeleton,
			const AnimationSet& animations,
			BMesh&              mesh,
			std::string_view    bskelKey,
			std::string_view    banimKey,
			bool                writeClips,
			const SourceRef&    source) const;

		/**
		 * Writes clips alone, attached to a rig already in this project -- what an import with the
		 * mesh turned off does, and how a rig whose animations the artist exported one per file
		 * gets all of them without a copy of the geometry each time.
		 *
		 * @return The mount key of the rig the clips were attached to -- what the import document
		 *         then records, since nothing else can derive it.
		 *
		 * @throws std::runtime_error if there are no clips or no rig, or if no skeleton in the
		 *         project matches the one they were authored against.
		 */
		std::string
		WriteImportedClips(
			const Skeleton&     skeleton,
			const AnimationSet& animations,
			std::string_view    banimKey,
			const SourceRef&    source) const;

		// --- Containers ------------------------------------------------------------------------

		/** The materials and skeleton a `.bmesh` names, read seek-only. See loadMeshRefs. */
		[[nodiscard]] MeshRefs
		LoadMeshRefs(std::string_view path) const;

		// --- The regeneration seam -------------------------------------------------------------
		//
		// The Regen forms answer with the container as the project's sources say it should be. A
		// read-only store trusts its keys outright -- `pack` made them true -- and serves the baked
		// bytes. A writable store checks the entry's cache key: fresh bytes load as-is; a stale
		// entry regenerates in memory from its copied source, and one whose source is missing or
		// was never recorded, or whose import document is gone, refuses. Either way the import
		// document's bindings are applied over the result, so a rebind is a document edit no mesh
		// file has to follow. Nothing here writes a byte to disk.

		/**
		 * Whether the geometry entry at `path` is a cache miss the LoadRegen forms would re-cook:
		 * a stale bake token, a source stamp that moved, or parameters the `.bimport` no longer
		 * matches. Always false on a read-only store, which trusts its keys. What a *derived*
		 * consumer asks -- a `.bvat` whose group answers true is itself stale, whatever its own
		 * stamps say.
		 *
		 * @throws std::runtime_error if `path` is not a `.bmesh`/`.bskel`/`.banim`, or its header
		 *         cannot be read.
		 */
		[[nodiscard]] bool
		GeometryIsStale(std::string_view path) const;

		/**
		 * The source governing `path`'s group as it stands *now*: the header's recorded key, the
		 * copied source's current stamp, and the import document's current parameters hash. What
		 * a derived bake records beside its output, so "the group I baked from has not moved"
		 * stays answerable after the bake itself made the pair look current. Empty key when the
		 * entry never recorded a source; zeroed stamp when the source file is gone.
		 *
		 * @throws what GeometryIsStale throws for a non-geometry path or an unreadable header.
		 */
		[[nodiscard]] SourceRef
		GeometryGroupSource(std::string_view path) const;

		/**
		 * @throws std::runtime_error on what `Load<BMesh>` throws for an unreadable container, and
		 *         on a stale entry that cannot regenerate: no recorded source, the source or its
		 *         import document gone from the project, a re-exported source whose submesh names
		 *         now collide -- or one that no longer carries a mesh at all.
		 */
		[[nodiscard]] RegenMesh
		LoadRegenMesh(std::string_view path) const;

		/**
		 * @throws what LoadRegenMesh throws, and std::runtime_error when the re-exported source
		 *         no longer carries a rig.
		 */
		[[nodiscard]] Skeleton
		LoadRegenSkeleton(std::string_view path) const;

		/**
		 * A regenerated clip set re-resolves its skeleton -- the group's own `.bskel` when the
		 * project holds one, else by signature as a clips-only import does -- and re-bakes its
		 * posed boxes against the meshes the project holds now.
		 *
		 * @throws what LoadRegenMesh throws, and std::runtime_error when the re-exported source
		 *         no longer carries clips, or the rig its clips attach to has vanished.
		 */
		[[nodiscard]] AnimationSet
		LoadRegenAnimations(std::string_view path) const;

		/**
		 * LoadMeshRefs surviving a foreign bake token: chunks that cannot be parsed answer from
		 * the frozen header and the import document instead -- the document's bindings are the
		 * materials, the group's rig resolves by source key -- so nothing regenerates and a scan
		 * of a whole project stays a header read per file. A matched token's refs read as stored,
		 * stamps unchecked.
		 *
		 * @throws std::runtime_error on a foreign-token mesh with no recorded source or whose
		 *         import document is gone -- what it references cannot be known, and the
		 *         reference scan is fatal on exactly that.
		 */
		[[nodiscard]] MeshRefs
		LoadRegenMeshRefs(std::string_view path) const;

		/**
		 * The same one regeneration for a `.banim`'s skeleton reference: a foreign-token clip set
		 * answers with its group's own `.bskel`, found by source key from the frozen headers
		 * alone -- never the full clip regeneration, whose posed-box walk a scan must not pay.
		 *
		 * @throws std::runtime_error when a foreign-token clip set's source produced no rig in
		 *         this project -- a stale clips-only group, whose re-resolve needs the full seam.
		 */
		[[nodiscard]] std::string
		LoadRegenAnimationSkeletonPath(std::string_view path) const;

		/** The skeleton a `.banim` names, without its samples. */
		[[nodiscard]] std::string
		LoadAnimationSkeletonPath(std::string_view path) const;

		/** Everything but the pixels. See loadVatTables. */
		[[nodiscard]] BVat
		LoadVatTables(std::string_view path) const;

		/** The three inputs a `.bvat` was baked from, read seek-only. */
		[[nodiscard]] VatRefs
		LoadVatRefs(std::string_view path) const;

		// --- Textures --------------------------------------------------------------------------

		/** Called before each texture, so the first call is (0, total). */
		using TextureProgressFn = std::function<void(size_t done, size_t total)>;

		/**
		 * Writes `mesh`'s detached textures into `textureDir`, named by importedTextureFileNames --
		 * the files a material routes at. `mesh.materials` says which are sRGB and is not written.
		 *
		 * Basis-UASTC supercompression dominates the cost of an import; `onProgress` runs on the
		 * calling thread, and `cancel` is polled between encodes, never inside one.
		 *
		 * @throws std::runtime_error if `textureDir` is not under `Derived/` (see requireOrigin),
		 *         escapes the data root, or a write fails.
		 * @throws Cancelled if `cancel` is signalled. What was written stays.
		 *
		 * @return The mount keys written, in image order -- what the naming rule decided, so a
		 *         caller that needs them does not run it a second time. Naming an unnamed image
		 *         hashes its pixels, and that is not free on a source carrying hundreds of MiB.
		 */
		std::vector<std::string>
		WriteTextures(
			const imp::BMeshImport&  mesh,
			std::string_view         textureDir,
			const TextureProgressFn& onProgress = {},
			const CancelToken&       cancel     = {}) const;

		/**
		 * The copied sources re-exported since the import that extracted their textures. Sorted,
		 * and empty on a read-only store. A source with no recorded folder, or no longer in the
		 * project, is not stale -- see docs/asset_containers.md.
		 *
		 * @throws std::runtime_error if an import document cannot be read: it may be the stale one,
		 *         and "nothing to do" would be a silent wrong answer.
		 */
		[[nodiscard]] std::vector<std::string>
		GetStaleImportedTextureSources() const;

		/**
		 * Re-extracts `sourceKey`'s textures into the folder its import document records, so an
		 * edited source reaches the materials routed at it. Safe over those routes because the
		 * names are the images' -- see importedTextureFileNames and docs/asset_containers.md.
		 *
		 * Deliberately not on a load path: the cost is an import's, and `LoadRegen*` runs on every
		 * mesh load and every deletion's reference scan.
		 *
		 * @throws std::runtime_error on a read-only store, a `sourceKey` not in the project, or a
		 *         document that is absent, unreadable, or records no folder (re-import instead).
		 * @throws Cancelled if `cancel` is signalled; the stamp is advanced last, so a cancelled
		 *         refresh is still reported stale.
		 */
		TextureRefresh
		RefreshImportedTextures(
			std::string_view         sourceKey,
			const TextureProgressFn& onProgress = {},
			const CancelToken&       cancel     = {}) const;

		[[nodiscard]] ImageData
		LoadTexture(std::string_view path, Ktx2Decode decode, uint32_t maxDim = 0) const;

		[[nodiscard]] ImageData
		LoadTexture(std::string_view path) const;

		/** A small RGBA8 image for CPU display. See loadKTX2Preview. */
		[[nodiscard]] ImageData
		LoadTexturePreview(std::string_view path, uint32_t maxDim = 128) const;

		// --- Staleness -------------------------------------------------------------------------

		/**
		 * The size and content hash of `path`, or a zeroed stamp when the mount does not hold it --
		 * which never compares equal to a real one, so an absent source reads as stale. Hashed as
		 * the mount serves the bytes, so a source stamps the same loose or packed.
		 */
		[[nodiscard]] SourceStamp
		StampOf(std::string_view path) const;

		/** Whether `material`'s baked triplet no longer reflects its routed sources. */
		[[nodiscard]] bool
		BakeIsStale(const BMaterial& material) const;

		/** Whether `material` draws from its routes rather than its triplet. */
		[[nodiscard]] bool
		DrawsLoose(const BMaterial& material) const;

		[[nodiscard]] bool
		VatIsStale(const BVat& vat) const;

		[[nodiscard]] bool
		IsSkyBakeStale(const BSky& sky) const;

		[[nodiscard]] bool
		IsEnvLightingBakeStale(const BEnvLighting& lighting) const;

		/** The map a consumer draws for `route`. @throws std::runtime_error if neither is present. */
		[[nodiscard]] const std::string&
		EnvMapToDraw(const EnvMapRoute& route) const;

		/** A `.benv` followed to its pixels. `benvPath` is a host file; the chain below it is keyed. */
		[[nodiscard]] ResolvedEnvironment
		ResolveEnvironment(const std::filesystem::path& benvPath) const;

		// --- Operations over the whole project ---------------------------------------------------

		/**
		 * Deletes what `plan` names, and whatever it cascades to, after the target.
		 *
		 * Reports a failure rather than throwing, because a failure here is ordinary: a preview
		 * decode holds a `.ktx2` open, and Windows refuses to unlink an open file. "Still
		 * referenced" and "the file is in use" are different things to tell a user, which is why
		 * they are different statuses. A directory whose removal fails part-way is reported
		 * kFailed, with whatever came off already gone -- there is no undo, and pretending
		 * otherwise would be worse than saying so.
		 *
		 * Deleting a material still leaves the baked maps it alone named; those are what
		 * FindUnusedBakedTextures then sweeps.
		 */
		DeletionResult
		DeleteAsset(const DeletionPlan& plan) const;

		/**
		 * Rewrites every referrer in `plan` and then moves the file.
		 *
		 * The referrers are all read and rewritten in memory before anything is written -- one that
		 * cannot be read or parsed fails the rename while the project is still untouched -- the
		 * files are then saved, and the rename itself comes last: it is the step most likely to be
		 * refused (Windows will not move a file another process holds open), and by then it is the
		 * only step left to undo. A failure anywhere writes the original bytes back over whatever
		 * had already been saved, so kFailed means the project is as it was; that restore is
		 * best-effort, and a machine that fails the restore too is reported with the first error
		 * rather than a pretense of atomicity.
		 *
		 * `plan.to` may already exist when it holds the same bytes as `plan.from`: that is the file
		 * having already moved, not a collision, and the rename collapses the pair and rewrites the
		 * referrers onto the survivor. Any other existing `to` fails.
		 *
		 * @throws std::runtime_error if a referrer in `plan` is not a container that stores
		 *         references -- a plan built by planRename never holds one, so that is a caller
		 *         error, not weather.
		 */
		RenameResult
		RenameAsset(const RenamePlan& plan) const;

		/**
		 * Every baked map under this project that nothing references.
		 *
		 * @throws std::runtime_error if a referrer cannot be read. An unreadable asset is fatal on
		 *         purpose: its references would silently go unmarked, and the maps it alone keeps
		 *         alive would be swept as garbage.
		 */
		[[nodiscard]] TexturePruneScan
		FindUnusedBakedTextures(const TexturePruneDesc& desc) const;

		/** The same, over the project's own texture directories. */
		[[nodiscard]] TexturePruneScan
		FindUnusedBakedTextures() const;

		/**
		 * Removes what a scan found. A file that has vanished since counts as deleted, not as a
		 * failure; one that cannot be removed is collected into `failed` rather than throwing, so
		 * one locked map does not abandon the rest of the sweep.
		 *
		 * Re-baking or editing a material between the scan and this call invalidates the scan --
		 * take a fresh one.
		 */
		TexturePruneResult
		DeleteUnusedBakedTextures(const TexturePruneScan& scan) const;

		/**
		 * Packs this project into the `.bpak` `desc` names.
		 *
		 * `desc.target` is untouched until the archive is whole: `PakWriter` streams to a temp and
		 * renames.
		 *
		 * @throws std::runtime_error if an asset cannot be read, if a stale `.bvat` cannot be
		 *         re-baked, if a geometry group cannot be served (no source to regenerate a stale
		 *         entry from, or a binding naming a submesh the mesh does not have), or if the
		 *         archive cannot be written.
		 */
		[[nodiscard]] PackReport
		Pack(const PackDesc& desc) const;

		/**
		 * Every container in this project re-saved at what its current state says it should be:
		 * a stale cache entry regenerated from its source, an authored document rewritten
		 * canonically, a material's triplet re-baked where its sources no longer produce the maps
		 * it names.
		 *
		 * A moved source's textures are re-extracted first, so the `.bimport` that stamps is on
		 * disk before the walk reads any document.
		 *
		 * A material with none of its routed sources on disk is left alone rather than failed: a
		 * delivered project ships its triplet and none of its sources, and has nothing to re-bake
		 * from. One with only some of them is a reference that has broken, and is reported.
		 *
		 * @param dryRun Report what would change without writing a byte -- a map included, so a
		 *        material's re-bake is resolved rather than encoded.
		 */
		[[nodiscard]] MigrateReport
		Migrate(bool dryRun) const;

		/**
		 * Every container this project's sources say should exist but does not, produced onto
		 * disk. Driven from the `.bimport` documents and the `outputs` each one names, because the
		 * regeneration seam and `Migrate` are both keyed on the derived file already being there
		 * and so can never produce one that is not.
		 *
		 * Absent only. A container that is on disk but stale is `Migrate`'s: it re-saves what it
		 * can read, which is cheaper than a re-import, and one problem then has one report.
		 *
		 * What is written is what a fresh import would have written, byte for byte -- including
		 * sweeping a clip set's boxes exactly as the writer that produced it did. Re-measuring
		 * them across the project is `RebakePosedBounds`.
		 *
		 * A source's extracted textures are covered too, but by a different question: a `.ktx2`
		 * carries no header, so `outputs` cannot name one and the only signal available is the
		 * texture folder being absent or empty.
		 *
		 * Rigs, then meshes, then clips -- a clip set's posed boxes are measured against the
		 * meshes on disk, and a mesh names the rig it binds.
		 *
		 * A source that cannot be re-imported is reported and skipped; the rest still run.
		 *
		 * @param dryRun Report what would be written without writing a byte.
		 */
		[[nodiscard]] ReimportReport
		Reimport(bool dryRun) const;

		/**
		 * Every geometry container in this project whose source has moved out from under it, as
		 * mount keys, sorted. Header peeks and one document hash apiece -- no regeneration -- so
		 * this is the question a project can afford to ask as it opens, where `Migrate(dryRun)`
		 * re-cooks each one to answer.
		 *
		 * Always empty on a read-only store, which trusts its keys.
		 */
		[[nodiscard]] std::vector<std::string>
		GetStaleGeometry() const;

		/**
		 * Every `.banim` in this project given the posed culling boxes an import writes, for clip
		 * sets cooked before the bake existed.
		 *
		 * @param dryRun Report what would change without writing a byte.
		 */
		[[nodiscard]] RebakeBoundsReport
		RebakePosedBounds(bool dryRun) const;

		/**
		 * The `.bskel` in this project whose signature matches `skeleton`, or empty when none does.
		 * What lets an import with the mesh turned off find the rig its clips belong to.
		 *
		 * @throws std::runtime_error if more than one rig matches, since which one the clips attach
		 *         to would otherwise depend on directory order.
		 */
		[[nodiscard]] std::filesystem::path
		FindMatchingSkeleton(const Skeleton& skeleton) const;

		/** `Authored/Meshes/<name>.glb` -- where an import copies its source. */
		[[nodiscard]] std::filesystem::path
		ImportedSourcePath(std::string_view name) const;

		/** The `.bimport` beside the copied source. */
		[[nodiscard]] std::filesystem::path
		ImportDocumentPath(std::string_view name) const;

		/**
		 * Copies the self-contained source into `Authored/Meshes/` and stamps it: the returned reference
		 * -- key, content stamp, parameter hash -- is what the caller sets on every container
		 * derived from it *before* saving them. The document itself is written afterwards by
		 * WriteImportedDocument, once the bindings exist; the split is safe because bindings are
		 * deliberately outside the parameter hash.
		 *
		 * `target.sampleRate` -- the rate clips are resampled to at import, the import's one
		 * parameter -- is what the returned reference's parameter hash covers.
		 *
		 * @throws what requireSelfContainedSource throws, and std::runtime_error on a copy failure.
		 */
		SourceRef
		CopyImportedSource(const std::filesystem::path& source, const ImportTarget& target) const;

		/**
		 * Writes the `.bimport` beside the copied source: the sample rate, where the textures went
		 * and the source as it stood when they did, and -- when `mesh` is given -- the
		 * submesh-name -> material bindings it carries. Null `mesh` is a clips-only import.
		 *
		 * @throws std::runtime_error on a write failure.
		 */
		void
		WriteImportedDocument(const ImportTarget& target, const BMesh* mesh) const;

		/**
		 * Sets `submesh`'s binding to `material` in the import document beside `sourceKey`'s copied
		 * source, leaving the parameters, unknown keys and every other binding as they stand. What
		 * a rebind writes instead of the mesh file: the binding is outside the cache key, so the
		 * mesh is neither rewritten nor staled.
		 *
		 * `sourceKey` is the mount key the mesh's header carries (`BMesh::source.key`); the
		 * document path is derived here, so no caller composes it.
		 *
		 * @throws std::runtime_error if `sourceKey` is empty, the document is absent or malformed,
		 *         or the write fails.
		 */
		void
		RebindSubmeshInDocument(
			std::string_view sourceKey,
			std::string_view submesh,
			std::string_view material) const;

		/**
		 * Rewrites every import document's bindings from its mesh's current state, parameters and
		 * unknown keys preserved -- the one-time adoption pass that makes the documents
		 * authoritative. Until it runs, a rebind saved into a `.bmesh` before documents existed is
		 * recorded nowhere else; after it, the document is what a load applies, so running this
		 * again later would overwrite document-only rebinds with stale mesh state.
		 *
		 * A mesh that will not load, a source claimed by two meshes, or a recorded source whose
		 * document is missing is reported per document and never guessed at.
		 */
		[[nodiscard]] std::vector<ReauthoredDocument>
		ReauthorImportDocuments() const;

		/**
		 * Imports `desc.source` into this project as a `.bsky`, a `.benvl` and the `.benv` composing
		 * them, writing the float intermediates into `Derived/SourceTextures/` as the routed
		 * sources and baking each into `Derived/BakedTextures/`.
		 *
		 * **Rolls back on failure.** A cancelled or failed import removes the files it created, so a
		 * half-written environment is never left behind. It removes only what it *created*: a file
		 * that was already there is one this import overwrote rather than made.
		 *
		 * **Baked maps are deliberately not rolled back.** They are content-addressed and shared, so
		 * the map this import wrote may be the same file another environment already names. An
		 * orphan left by a failed import is what FindUnusedBakedTextures sweeps.
		 *
		 * @param cancel Polled between the projection, each convolution and each bake.
		 * @throws std::runtime_error if nothing is selected, if the source cannot be read, or if any
		 *         directory `desc` names is the wrong half for what would land in it -- checked
		 *         before the projection, so a misplaced one costs no bake.
		 * @throws Cancelled if `cancel` is signalled.
		 */
		[[nodiscard]] EnvImportResult
		ImportEnvironment(const EnvImportDesc& desc, const CancelToken& cancel = {}) const;

		/**
		 * Every file `desc` would write, data-root relative, without writing any of them -- for a
		 * caller that must decide *before* importing whether it would land on something already
		 * there. The baked maps are not included: they are content-addressed, so a collision with
		 * one is two imports agreeing on content rather than one destroying the other.
		 */
		[[nodiscard]] std::vector<std::string>
		EnvironmentImportTargets(const EnvImportDesc& desc) const;

		// --- Describe --------------------------------------------------------------------------

		/**
		 * What a container holds, as text for a person, with every routed source stat'd through the
		 * mount and compared against the stamp its bake recorded -- so a stale bake is visible.
		 *
		 * One overload per container and no other door: rendering one is `src/asset_describe.h`,
		 * which is internal, because the useful question about a container is whether what it
		 * records is still true and only a project can answer that. The three that route nothing
		 * to stat -- a mesh, a rig, a clip set -- still come through here, so a caller never has
		 * to know which kind it is holding.
		 *
		 * The text is for a person, not a parser: it is not stable across versions and nothing
		 * reads it back.
		 */
		[[nodiscard]] std::string
		Describe(const BMaterial& material) const;

		/** @param verbose False lists a summary and the material table rather than every submesh. */
		[[nodiscard]] std::string
		Describe(const BMesh& mesh, bool verbose = true) const;

		[[nodiscard]] std::string
		Describe(const Skeleton& skeleton) const;

		/**
		 * @param skeleton The rig the set names, to have its bone names printed and its signature
		 *        checked -- a mismatch is the failure this format is hardest to see by eye. Null
		 *        prints joint indices bare.
		 */
		[[nodiscard]] std::string
		Describe(const AnimationSet& animations, const Skeleton* skeleton = nullptr) const;

		[[nodiscard]] std::string
		Describe(const BSky& sky) const;

		[[nodiscard]] std::string
		Describe(const BEnvLighting& lighting) const;

		/** For a `.benv`, which holds no pixels: whether each file it names is actually there. */
		[[nodiscard]] std::string
		Describe(const BEnv& env) const;

		/** Pass the tables-only form (LoadVatTables) -- nothing here reads a texel. */
		[[nodiscard]] std::string
		Describe(const BVat& vat) const;

	private:
		/**
		 * The bytes at `path`, and the bytes of `path` written atomically. What Load and Save are
		 * built from -- the templates stay in the header and the mount, the write primitive and the
		 * error messages stay out of it.
		 *
		 * @param what Names the container in any message thrown -- the codec passes its extension.
		 */
		[[nodiscard]] std::vector<std::byte>
		ReadBytes(std::string_view path) const;

		void
		WriteBytes(std::string_view path, std::span<const std::byte> bytes, std::string_view what)
			const;

		std::filesystem::path                          m_DataRoot;
		std::shared_ptr<const core::file::IFileSystem> m_Files;
	};
}
