#pragma once
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct AnimationSet;
	struct BEnv;
	struct BEnvLighting;
	struct BMaterial;
	struct BMesh;
	struct BSky;
	struct BVat;
	struct EnvMapRoute;
	struct ImageData;
	struct MeshRefs;
	struct ResolvedEnvironment;
	struct Skeleton;
	struct SourceStamp;
	struct VatRefs;

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
		 * well. `store.Load<BMesh>("Meshes/a.bmesh")`.
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
		 *
		 * @throws std::runtime_error if `path` escapes the data root (see ResolveWritePath), or the
		 *         file cannot be written. Atomic: a crash mid-write leaves the previous bytes.
		 */
		template <AssetCodecFor T>
		void
		Save(const T& value, std::string_view path) const
		{
			// The extension without its dot is the container's name, which is what every message
			// this library throws is prefixed with -- "bmaterial: ...", not ".bmaterial: ...".
			WriteBytes(path, AssetCodec<T>::Serialize(value), AssetCodec<T>::c_Extension.substr(1));
		}

		// --- Bakes ---------------------------------------------------------------------------

		/**
		 * Composites `material`'s routes down to its baked triplet, in place, writing the maps into
		 * the project's texture directory.
		 *
		 * The store is the data root a bake reads and writes relative to, so it is not passed one.
		 *
		 * @throws std::runtime_error / Cancelled as bakeMaterial.
		 */
		void
		BakeMaterial(BMaterial& material, const CancelToken& cancel = {}) const;

		/** BakeMaterial, writing the maps into `textureDir` instead of the project's default. */
		void
		BakeMaterial(
			BMaterial&         material,
			std::string_view   textureDir,
			const CancelToken& cancel = {}) const;

		/** @throws std::runtime_error / Cancelled as bakeSky. */
		void
		BakeSky(BSky& sky, const CancelToken& cancel = {}) const;

		/** BakeSky, writing the map into `textureDir` instead of the project's default. */
		void
		BakeSky(BSky& sky, std::string_view textureDir, const CancelToken& cancel = {}) const;

		/** @throws std::runtime_error / Cancelled as bakeEnvLighting. */
		void
		BakeEnvLighting(BEnvLighting& lighting, const CancelToken& cancel = {}) const;

		/** BakeEnvLighting, writing the maps into `textureDir` instead of the project's default. */
		void
		BakeEnvLighting(
			BEnvLighting&      lighting,
			std::string_view   textureDir,
			const CancelToken& cancel = {}) const;

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
