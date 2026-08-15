#pragma once
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

		/** Whether the mount answers for `path` at all. */
		[[nodiscard]] bool
		Exists(std::string_view path) const
		{
			return m_Files->Exists(path);
		}

		// --- Containers ------------------------------------------------------------------------

		/** @throws std::runtime_error if the container is absent, unreadable or malformed. */
		[[nodiscard]] BMesh
		LoadMesh(std::string_view path) const;

		/** The materials and skeleton a `.bmesh` names, read seek-only. See loadMeshRefs. */
		[[nodiscard]] MeshRefs
		LoadMeshRefs(std::string_view path) const;

		[[nodiscard]] Skeleton
		LoadSkeleton(std::string_view path) const;

		[[nodiscard]] AnimationSet
		LoadAnimations(std::string_view path) const;

		/** The skeleton a `.banim` names, without its samples. */
		[[nodiscard]] std::string
		LoadAnimationSkeletonPath(std::string_view path) const;

		[[nodiscard]] BMaterial
		LoadMaterial(std::string_view path) const;

		[[nodiscard]] BEnv
		LoadEnv(std::string_view path) const;

		[[nodiscard]] BSky
		LoadSky(std::string_view path) const;

		[[nodiscard]] BEnvLighting
		LoadEnvLighting(std::string_view path) const;

		[[nodiscard]] BVat
		LoadVat(std::string_view path) const;

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
		 * The size and mtime of `path`, or a zeroed stamp when the mount does not hold it -- which
		 * never compares equal to a real one, so an absent source reads as stale.
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

	private:
		std::filesystem::path                          m_DataRoot;
		std::shared_ptr<const core::file::IFileSystem> m_Files;
	};
}
