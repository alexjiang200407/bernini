#include <assetlib/bvat_io.h>
#include <assetlib_structs/BVat.h>

#include "chunk_io.h"
#include "fs_util.h"

#include <assetlib_structs/magic.h>
#include <core/err/util.h>

#include <core/file/file.h>

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;

		constexpr std::string_view c_What = "bvat";

		enum class ChunkId : uint32_t
		{
			kInfo = 1,
			kClips,
			kColumns,
			kPalettes,
			kInputs,
			kStringPool,

			// The pixel payloads stay last in id as in intent: every seek-only read names the
			// chunks before this line and never touches these.
			kPositionsKtx2,
			kNormalsKtx2
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
			uint32_t    meshLength;
			uint32_t    skeletonLength;
			uint32_t    animationsLength;
			uint32_t    pad;
		};

		static_assert(sizeof(VatInputsRef) == 72);

		std::vector<std::byte>
		packInputs(const BVat& vat)
		{
			VatInputsRef ref{};
			ref.signature        = vat.skeletonSignature;
			ref.mesh             = vat.meshStamp;
			ref.skeleton         = vat.skeletonStamp;
			ref.animations       = vat.animationsStamp;
			ref.meshLength       = static_cast<uint32_t>(vat.mesh.size());
			ref.skeletonLength   = static_cast<uint32_t>(vat.skeleton.size());
			ref.animationsLength = static_cast<uint32_t>(vat.animations.size());

			std::vector<std::byte> out(
				sizeof(ref) + vat.mesh.size() + vat.skeleton.size() + vat.animations.size());
			std::byte* at = out.data();
			std::memcpy(at, &ref, sizeof(ref));
			at += sizeof(ref);
			std::memcpy(at, vat.mesh.data(), vat.mesh.size());
			at += vat.mesh.size();
			std::memcpy(at, vat.skeleton.data(), vat.skeleton.size());
			at += vat.skeleton.size();
			std::memcpy(at, vat.animations.data(), vat.animations.size());
			return out;
		}

		void
		unpackInputs(BVat& vat, std::span<const std::byte> bytes)
		{
			if (bytes.size() < sizeof(VatInputsRef))
				throw_runtime_error("bvat: the inputs chunk is truncated");

			VatInputsRef ref{};
			std::memcpy(&ref, bytes.data(), sizeof(ref));

			const size_t paths = size_t(ref.meshLength) + ref.skeletonLength + ref.animationsLength;
			if (sizeof(ref) + paths > bytes.size())
				throw_runtime_error("bvat: an input path runs past its chunk");

			vat.skeletonSignature = ref.signature;
			vat.meshStamp         = ref.mesh;
			vat.skeletonStamp     = ref.skeleton;
			vat.animationsStamp   = ref.animations;

			const char* at = reinterpret_cast<const char*>(bytes.data()) + sizeof(ref);
			vat.mesh.assign(at, ref.meshLength);
			at += ref.meshLength;
			vat.skeleton.assign(at, ref.skeletonLength);
			at += ref.skeletonLength;
			vat.animations.assign(at, ref.animationsLength);
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

		chunk::Writer writer;
		writer.Add(ChunkId::kInfo, infoChunk);
		writer.Add(ChunkId::kClips, vat.clips);
		writer.Add(ChunkId::kColumns, vat.columns);
		writer.Add(ChunkId::kPalettes, vat.palettes);
		writer.Add(ChunkId::kInputs, packInputs(vat));
		writer.Add(ChunkId::kStringPool, vat.stringPool.bytes());
		writer.Add(ChunkId::kPositionsKtx2, vat.positionsKtx2);
		writer.Add(ChunkId::kNormalsKtx2, vat.normalsKtx2);
		return writer.Finish(magic::c_BVat, c_VersionMajor, c_VersionMinor);
	}

	namespace
	{
		using ChunkMap = std::unordered_map<uint32_t, std::vector<std::byte>>;

		/**
		 * The table chunks arrive through two doors -- typed and validated from chunk::Reader on a
		 * full deserialize, raw bytes from readChunksFromFile on a seek-only one -- and the tables
		 * must assemble identically through both, so one readTables reads through this.
		 */
		struct TableSource
		{
			const chunk::Reader* reader = nullptr;
			const ChunkMap*      chunks = nullptr;

			template <typename T>
			std::vector<T>
			Read(ChunkId id) const
			{
				if (reader != nullptr)
					return reader->Read<T>(id);

				const auto it = chunks->find(static_cast<uint32_t>(id));
				if (it == chunks->end())
					return {};

				if (it->second.size() % sizeof(T) != 0)
					throw_runtime_error(
						"bvat: chunk byte size is not a multiple of the element size");

				std::vector<T> out(it->second.size() / sizeof(T));
				std::memcpy(out.data(), it->second.data(), it->second.size());
				return out;
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

			const auto inputs = source.Read<std::byte>(ChunkId::kInputs);
			if (inputs.empty())
				throw_runtime_error("bvat: the inputs chunk is missing");
			unpackInputs(vat, inputs);

			validateVat(vat);
			return vat;
		}
	}

	BVat
	deserializeVat(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, magic::c_BVat, c_VersionMajor, c_What);

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
		constexpr std::array<uint32_t, 6> c_WantedTableChunks = { { uint32_t(ChunkId::kInfo),
			                                                        uint32_t(ChunkId::kClips),
			                                                        uint32_t(ChunkId::kColumns),
			                                                        uint32_t(ChunkId::kPalettes),
			                                                        uint32_t(ChunkId::kInputs),
			                                                        uint32_t(
																		ChunkId::kStringPool) } };

		constexpr std::array<uint32_t, 1> c_WantedRefChunks = { { uint32_t(ChunkId::kInputs) } };

		VatRefs
		refsFromChunks(const std::unordered_map<uint32_t, std::vector<std::byte>>& chunks)
		{
			const auto it = chunks.find(uint32_t(ChunkId::kInputs));
			if (it == chunks.end())
				throw_runtime_error("bvat: the inputs chunk is missing");

			BVat vat;
			unpackInputs(vat, it->second);
			return VatRefs{ vat.mesh, vat.skeleton, vat.animations };
		}
	}

	BVat
	loadVatTables(const std::filesystem::path& path)
	{
		const auto chunks = chunk::readChunksFromFile(
			path,
			magic::c_BVat,
			c_VersionMajor,
			c_WantedTableChunks,
			c_What);

		return readTables(TableSource{ nullptr, &chunks });
	}

	BVat
	loadVatTables(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		const auto chunks = chunk::readChunksFrom(
			fileSystem,
			path,
			magic::c_BVat,
			c_VersionMajor,
			c_WantedTableChunks,
			c_What);

		return readTables(TableSource{ nullptr, &chunks });
	}

	VatRefs
	loadVatRefs(const std::filesystem::path& path)
	{
		return refsFromChunks(
			chunk::readChunksFromFile(
				path,
				magic::c_BVat,
				c_VersionMajor,
				c_WantedRefChunks,
				c_What));
	}

	VatRefs
	loadVatRefs(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return refsFromChunks(
			chunk::readChunksFrom(
				fileSystem,
				path,
				magic::c_BVat,
				c_VersionMajor,
				c_WantedRefChunks,
				c_What));
	}
}
