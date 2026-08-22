#include <assetlib/bvat_io.h>
#include <assetlib_structs/BVat.h>

#include "bake_tokens.h"
#include "cache_io.h"
#include "chunk_io.h"
#include "fs_util.h"

#include <assetlib_structs/magic.h>
#include <core/err/util.h>

#include <core/file/file.h>

#include "mounted_io.h"

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		constexpr std::string_view c_What = "bvat";

		enum class ChunkId : uint32_t
		{
			kInfo = 1,
			kClips,
			kColumns,
			kPalettes,
			kInputs,
			kStringPool,
			kInputPaths = 9,  // mesh, skeleton, animations -- the three paths kInputs stamps

			// The pixel payloads stay last in intent: every seek-only read names the chunks above
			// and never touches these. A bake from before the twist rode the normals' alpha has
			// them under id 8, so it fails to load and is re-baked rather than drawn twisted.
			kPositionsKtx2 = 7,
			kNormalsKtx2   = 10,
		};

		struct VatInfo
		{
			glm::vec3 boundsMin;
			glm::vec3 boundsMax;
			uint32_t  width;
			uint32_t  height;
			uint32_t  boneCount;
		};

		static_assert(sizeof(VatInfo) == 36);

		struct VatInputsRef
		{
			uint64_t    signature;
			SourceStamp mesh;
			SourceStamp skeleton;
			SourceStamp animations;
		};

		static_assert(sizeof(VatInputsRef) == 56);

		std::vector<VatInputsRef>
		packInputs(const BVat& vat)
		{
			VatInputsRef ref{};
			ref.signature  = vat.skeletonSignature;
			ref.mesh       = vat.meshStamp;
			ref.skeleton   = vat.skeletonStamp;
			ref.animations = vat.animationsStamp;
			return { ref };
		}

		std::vector<char>
		packInputPaths(const BVat& vat)
		{
			const std::array<std::string, 3> paths = { vat.mesh, vat.skeleton, vat.animations };
			return chunk::packStrings(paths);
		}

		void
		unpackInputs(BVat& vat, std::span<const VatInputsRef> ref, std::span<const char> paths)
		{
			core::throw_runtime_error_if(
				ref.size() != 1,
				"bvat: the inputs chunk holds {} entries, not one",
				ref.size());

			const auto names = chunk::unpackStrings(paths);
			core::throw_runtime_error_if(
				names.size() != 3,
				"bvat: the input paths chunk names {} paths, not three",
				names.size());

			vat.skeletonSignature = ref[0].signature;
			vat.meshStamp         = ref[0].mesh;
			vat.skeletonStamp     = ref[0].skeleton;
			vat.animationsStamp   = ref[0].animations;
			vat.mesh              = names[0];
			vat.skeleton          = names[1];
			vat.animations        = names[2];
		}

		/**
		 * The cross-chunk invariants a reader would otherwise trip over one fetch at a time: every
		 * consumer indexes the textures and the palettes through these tables, so a table that
		 * disagrees with the dimensions is a malformed file, not a caller's problem.
		 */
		void
		validateVat(const BVat& vat)
		{
			uint64_t columns = 0;
			for (const VatColumns& submesh : vat.columns) columns += submesh.vertexCount;
			if (columns != vat.width)
				throw_runtime_error(
					"bvat: the column table covers {} vertices but the texture is {} wide",
					columns,
					vat.width);

			uint64_t rows   = 0;
			uint64_t frames = 0;
			for (const VatClip& clip : vat.clips)
			{
				if (clip.frameCount == 0)
					throw_runtime_error("bvat: a clip with no frames");
				if (clip.firstRow != rows || clip.firstPalette != frames * vat.boneCount)
					throw_runtime_error("bvat: a clip's rows or palettes overlap its neighbour's");
				rows += clip.frameCount + 1;
				frames += clip.frameCount;
			}
			if (rows != vat.height)
				throw_runtime_error(
					"bvat: the clip table covers {} padded rows but the texture is {} tall",
					rows,
					vat.height);

			if (vat.palettes.size() != frames * vat.boneCount)
				throw_runtime_error(
					"bvat: {} palette entries for {} frames of {} bones",
					vat.palettes.size(),
					frames,
					vat.boneCount);
		}
	}

	std::vector<std::byte>
	serializeVat(const BVat& vat)
	{
		if (vat.positionsKtx2.empty() || vat.normalsKtx2.empty())
			throw_runtime_error(
				"bvat: nothing to write -- the texture payloads are empty (a tables-only read "
				"cannot be written back)");

		validateVat(vat);

		VatInfo info{};
		info.boundsMin = vat.boundsMin;
		info.boundsMax = vat.boundsMax;
		info.width     = vat.width;
		info.height    = vat.height;
		info.boneCount = vat.boneCount;

		std::vector<VatInfo> infoChunk(1, info);

		cache::Writer writer;
		writer.Add(ChunkId::kInfo, infoChunk);
		writer.Add(ChunkId::kClips, vat.clips);
		writer.Add(ChunkId::kColumns, vat.columns);
		writer.Add(ChunkId::kPalettes, vat.palettes);
		writer.Add(ChunkId::kInputs, packInputs(vat));
		writer.Add(ChunkId::kInputPaths, packInputPaths(vat));
		writer.Add(ChunkId::kStringPool, vat.stringPool.bytes());
		writer.Add(ChunkId::kPositionsKtx2, vat.positionsKtx2);
		writer.Add(ChunkId::kNormalsKtx2, vat.normalsKtx2);

		// No source in the key: a `.bvat` records its three inputs and their stamps as chunks,
		// and its freshness rule reads those -- the token covers the format alone.
		return writer.Finish(magic::c_BVat, c_BVatBakeToken, SourceRef{});
	}

	namespace
	{
		/**
		 * The table chunks arrive through two doors -- typed from cache::Reader on a full
		 * deserialize, ranged from readCacheChunks on a seek-only one -- and the tables must
		 * assemble identically through both, so one readTables reads through this.
		 */
		struct TableSource
		{
			const cache::Reader*    reader = nullptr;
			const cache::CacheData* chunks = nullptr;

			template <typename T>
			std::vector<T>
			Read(ChunkId id) const
			{
				return reader != nullptr ? reader->Read<T>(id) : chunks->Read<T>(id, c_What);
			}
		};

		BVat
		readTables(const TableSource& source)
		{
			const auto info = source.Read<VatInfo>(ChunkId::kInfo);
			if (info.size() != 1)
				throw_runtime_error("bvat: the info chunk holds {} entries, not one", info.size());

			BVat vat;
			vat.boundsMin  = info[0].boundsMin;
			vat.boundsMax  = info[0].boundsMax;
			vat.width      = info[0].width;
			vat.height     = info[0].height;
			vat.boneCount  = info[0].boneCount;
			vat.clips      = source.Read<VatClip>(ChunkId::kClips);
			vat.columns    = source.Read<VatColumns>(ChunkId::kColumns);
			vat.palettes   = source.Read<glm::mat4>(ChunkId::kPalettes);
			vat.stringPool = core::string_pool(source.Read<char>(ChunkId::kStringPool));

			unpackInputs(
				vat,
				source.Read<VatInputsRef>(ChunkId::kInputs),
				source.Read<char>(ChunkId::kInputPaths));

			validateVat(vat);
			return vat;
		}
	}

	BVat
	deserializeVat(std::span<const std::byte> bytes)
	{
		// Wholly derived, so a bake from before the cache format has no carry -- re-baking it is
		// cheaper than any reader for a file nobody authored.
		core::throw_runtime_error_if(
			!cache::isCacheEntry(bytes),
			"bvat: written before the cache format; re-bake it");

		const cache::Reader reader(bytes, magic::c_BVat, c_BVatBakeToken, c_What);

		BVat vat          = readTables(TableSource{ &reader, nullptr });
		vat.positionsKtx2 = reader.Require<std::byte>(ChunkId::kPositionsKtx2);
		vat.normalsKtx2   = reader.Require<std::byte>(ChunkId::kNormalsKtx2);
		return vat;
	}

	void
	saveVat(const BVat& vat, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeVat(vat), "bvat");
	}

	BVat
	loadVat(const std::filesystem::path& path)
	{
		return deserializeVat(core::file::read_file_bytes(path.string()));
	}

	BVat
	loadVat(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeVat(fileSystem.Read(path));
	}

	namespace
	{
		constexpr std::array<uint32_t, 7> c_WantedTableChunks = {
			{ static_cast<uint32_t>(ChunkId::kInfo),
			  static_cast<uint32_t>(ChunkId::kClips),
			  static_cast<uint32_t>(ChunkId::kColumns),
			  static_cast<uint32_t>(ChunkId::kPalettes),
			  static_cast<uint32_t>(ChunkId::kInputs),
			  static_cast<uint32_t>(ChunkId::kInputPaths),
			  static_cast<uint32_t>(ChunkId::kStringPool) }
		};

		constexpr std::array<uint32_t, 2> c_WantedRefChunks = {
			{ static_cast<uint32_t>(ChunkId::kInputs), static_cast<uint32_t>(ChunkId::kInputPaths) }
		};

		VatRefs
		refsFromChunks(const cache::CacheData& chunks)
		{
			BVat vat;
			unpackInputs(
				vat,
				chunks.Read<VatInputsRef>(ChunkId::kInputs, c_What),
				chunks.Read<char>(ChunkId::kInputPaths, c_What));
			return VatRefs{ vat.mesh, vat.skeleton, vat.animations };
		}
	}

	BVat
	loadVatTables(const std::filesystem::path& path)
	{
		const auto chunks = cache::readCacheChunksFromFile(
			path,
			magic::c_BVat,
			c_BVatBakeToken,
			c_WantedTableChunks,
			c_What);

		return readTables(TableSource{ nullptr, &chunks });
	}

	BVat
	loadVatTables(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		const auto chunks = cache::readCacheChunksFrom(
			fileSystem,
			path,
			magic::c_BVat,
			c_BVatBakeToken,
			c_WantedTableChunks,
			c_What);

		return readTables(TableSource{ nullptr, &chunks });
	}

	VatRefs
	loadVatRefs(const std::filesystem::path& path)
	{
		return refsFromChunks(
			cache::readCacheChunksFromFile(
				path,
				magic::c_BVat,
				c_BVatBakeToken,
				c_WantedRefChunks,
				c_What));
	}

	VatRefs
	loadVatRefs(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return refsFromChunks(
			cache::readCacheChunksFrom(
				fileSystem,
				path,
				magic::c_BVat,
				c_BVatBakeToken,
				c_WantedRefChunks,
				c_What));
	}
}
