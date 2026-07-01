#include <assetlib/bmesh_io.h>

#include "ByteReader.h"
#include "ByteWriter.h"

#include <core/file/file.h>

namespace assetlib::bmesh
{
	namespace
	{
		constexpr uint32_t c_Magic        = 0x48534D42u;  // 'B','M','S','H' little-endian
		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;
		constexpr size_t   c_ChunkAlign   = 16;

		enum class ChunkId : uint32_t
		{
			kNodes = 1,
			kRoots,
			kMeshes,
			kSubmeshes,
			kMeshlets,
			kMeshletVertices,
			kMeshletTriangles,
			kVertexData,
			kIndexData,
			kStringPool
		};

		struct FileHeader
		{
			uint32_t magic;
			uint16_t versionMajor;
			uint16_t versionMinor;
			uint8_t  byteOrder;  // 0 == little-endian
			uint8_t  pad[3];
			uint32_t chunkCount;
			uint32_t chunkTableOffset;
			uint64_t fileSize;
		};

		static_assert(sizeof(FileHeader) == 32);

		struct ChunkEntry
		{
			uint32_t id;
			uint32_t elementSize;
			uint64_t offset;
			uint64_t byteSize;
		};

		static_assert(sizeof(ChunkEntry) == 24);

		template <typename T>
		ChunkEntry
		appendChunk(ByteWriter& writer, ChunkId id, const std::vector<T>& values)
		{
			writer.alignTo(c_ChunkAlign);

			ChunkEntry entry{};
			entry.id          = static_cast<uint32_t>(id);
			entry.elementSize = static_cast<uint32_t>(sizeof(T));
			entry.offset      = writer.size();
			entry.byteSize    = values.size() * sizeof(T);
			writer.writePodArray(std::span<const T>(values));
			return entry;
		}

		template <typename T>
		std::vector<T>
		readChunk(std::span<const std::byte> all, const ChunkEntry& entry)
		{
			if (entry.elementSize != sizeof(T))
				throw std::runtime_error("bmesh: chunk element size mismatch");
			if (entry.byteSize % sizeof(T) != 0)
				throw std::runtime_error(
					"bmesh: chunk byte size is not a multiple of the element size");
			if (entry.offset + entry.byteSize > all.size())
				throw std::runtime_error("bmesh: chunk extends past end of stream");

			std::vector<T> out(entry.byteSize / sizeof(T));
			std::copy_n(
				all.data() + entry.offset,
				entry.byteSize,
				reinterpret_cast<std::byte*>(out.data()));
			return out;
		}
	}

	std::vector<std::byte>
	serialize(const BMesh& mesh)
	{
		ByteWriter writer;
		writer.writePod(FileHeader{});  // placeholder, patched below

		std::array<ChunkEntry, 10> chunks = {
			appendChunk(writer, ChunkId::kNodes, mesh.nodes),
			appendChunk(writer, ChunkId::kRoots, mesh.roots),
			appendChunk(writer, ChunkId::kMeshes, mesh.meshes),
			appendChunk(writer, ChunkId::kSubmeshes, mesh.submeshes),
			appendChunk(writer, ChunkId::kMeshlets, mesh.meshlets),
			appendChunk(writer, ChunkId::kMeshletVertices, mesh.meshletVertices),
			appendChunk(writer, ChunkId::kMeshletTriangles, mesh.meshletTriangles),
			appendChunk(writer, ChunkId::kVertexData, mesh.vertexData),
			appendChunk(writer, ChunkId::kIndexData, mesh.indexData),
			appendChunk(writer, ChunkId::kStringPool, mesh.stringPool),
		};

		writer.alignTo(c_ChunkAlign);
		const auto tableOffset = writer.size();
		writer.writePodArray(std::span<const ChunkEntry>(chunks));

		FileHeader header{};
		header.magic            = c_Magic;
		header.versionMajor     = c_VersionMajor;
		header.versionMinor     = c_VersionMinor;
		header.byteOrder        = 0;
		header.chunkCount       = static_cast<uint32_t>(chunks.size());
		header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.fileSize         = writer.size();
		writer.patchPod(0, header);

		return writer.take();
	}

	BMesh
	deserialize(std::span<const std::byte> bytes)
	{
		ByteReader reader(bytes);
		const auto header = reader.readPod<FileHeader>();

		if (header.magic != c_Magic)
			throw std::runtime_error("bmesh: bad magic");
		if (header.versionMajor != c_VersionMajor)
			throw std::runtime_error("bmesh: unsupported major version");
		if (header.byteOrder != 0)
			throw std::runtime_error("bmesh: unsupported byte order");
		if (header.fileSize > bytes.size())
			throw std::runtime_error("bmesh: stream shorter than declared file size");

		const auto tableBytes = static_cast<size_t>(header.chunkCount) * sizeof(ChunkEntry);
		if (header.chunkTableOffset + tableBytes > bytes.size())
			throw std::runtime_error("bmesh: chunk table extends past end of stream");

		reader.seek(header.chunkTableOffset);
		std::unordered_map<uint32_t, ChunkEntry> table;
		for (uint32_t i = 0; i < header.chunkCount; ++i)
		{
			const auto entry = reader.readPod<ChunkEntry>();
			table.emplace(entry.id, entry);
		}

		const auto require = [&](ChunkId id) -> const ChunkEntry& {
			const auto it = table.find(static_cast<uint32_t>(id));
			if (it == table.end())
				throw std::runtime_error("bmesh: missing required chunk");
			return it->second;
		};
		const auto optional = [&](ChunkId id, const ChunkEntry& fallback) -> const ChunkEntry& {
			const auto it = table.find(static_cast<uint32_t>(id));
			return it == table.end() ? fallback : it->second;
		};

		const ChunkEntry empty{};

		BMesh mesh;
		mesh.nodes     = readChunk<Node>(bytes, require(ChunkId::kNodes));
		mesh.meshes    = readChunk<Mesh>(bytes, require(ChunkId::kMeshes));
		mesh.roots     = readChunk<uint32_t>(bytes, optional(ChunkId::kRoots, empty));
		mesh.submeshes = readChunk<Submesh>(bytes, optional(ChunkId::kSubmeshes, empty));
		mesh.meshlets  = readChunk<Meshlet>(bytes, optional(ChunkId::kMeshlets, empty));
		mesh.meshletVertices =
			readChunk<uint32_t>(bytes, optional(ChunkId::kMeshletVertices, empty));
		mesh.meshletTriangles =
			readChunk<uint8_t>(bytes, optional(ChunkId::kMeshletTriangles, empty));
		mesh.vertexData = readChunk<std::byte>(bytes, optional(ChunkId::kVertexData, empty));
		mesh.indexData  = readChunk<std::byte>(bytes, optional(ChunkId::kIndexData, empty));
		mesh.stringPool = readChunk<char>(bytes, optional(ChunkId::kStringPool, empty));
		return mesh;
	}

	void
	save(const BMesh& mesh, const std::filesystem::path& path)
	{
		const auto    bytes = serialize(mesh);
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error("bmesh: cannot open file for writing: " + path.string());
		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error("bmesh: failed to write file: " + path.string());
	}

	BMesh
	load(const std::filesystem::path& path)
	{
		const auto bytes = core::file::readFileBytes(path.string());
		return deserialize(bytes);
	}

	void
	writeTextures(const BMesh& mesh, const std::filesystem::path& outDir)
	{
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);

		for (size_t i = 0; i < mesh.textures.size(); ++i)
		{
			const auto& texture  = mesh.textures[i];
			auto        fileName = texture.name.empty() ?
			                           std::filesystem::path("tex" + std::to_string(i)) :
			                           std::filesystem::path(texture.name).filename();
			fileName.replace_extension(".dds");

			const auto    fullPath = outDir / fileName;
			std::ofstream out(fullPath, std::ios::binary);
			if (!out)
				throw std::runtime_error(
					"bmesh: cannot open texture file for writing: " + fullPath.string());
			out.write(
				reinterpret_cast<const char*>(texture.dds.data()),
				static_cast<std::streamsize>(texture.dds.size()));
			if (!out)
				throw std::runtime_error(
					"bmesh: failed to write texture file: " + fullPath.string());
		}
	}
}
