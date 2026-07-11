#include <assetlib/bmesh_io.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>

#include "ByteReader.h"
#include "ByteWriter.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic = 0x48534D42u;  // 'B','M','S','H' little-endian
		// v3 changed what `materials` paths are relative to: the project's data root, not the mesh
		// file. The bytes are unchanged, so a v2 file would load and silently resolve its materials
		// against the wrong directory. It is rejected instead; re-import or re-save the mesh.
		constexpr uint16_t c_VersionMajor = 3;
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
			kStringPool,
			kMaterialPaths
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

		// Material paths are stored as one blob of NUL-terminated strings (one terminator per path).
		std::vector<char>
		packStrings(const std::vector<std::string>& strings)
		{
			std::vector<char> pool;
			for (const auto& s : strings)
			{
				pool.insert(pool.end(), s.begin(), s.end());
				pool.push_back('\0');
			}
			return pool;
		}

		std::vector<std::string>
		unpackStrings(const std::vector<char>& pool)
		{
			std::vector<std::string> out;
			std::string              current;
			for (const char c : pool)
			{
				if (c == '\0')
				{
					out.push_back(current);
					current.clear();
				}
				else
				{
					current.push_back(c);
				}
			}
			return out;
		}

		// The NUL-terminated name at `offset` in the string pool (empty for offset 0 / out of range).
		std::string
		nameFromPool(const std::vector<char>& pool, uint32_t offset)
		{
			if (offset == 0 || offset >= pool.size())
				return {};
			return std::string(pool.data() + offset);
		}

		// The standalone file name writeTextures emits for texture `index` (empty when absent).
		std::string
		texturePath(uint32_t index)
		{
			return index == c_InvalidIndex ? std::string{} :
			                                 "tex" + std::to_string(index) + ".ktx2";
		}

		// The file name toBMesh assembles for material `index`.
		std::string
		materialPath(size_t index)
		{
			return "mat" + std::to_string(index) + ".bmaterial";
		}

		// Resolves an inline import material into its modular, path-referencing form.
		BMaterial
		toBMaterial(const imp::BMeshImport& mesh, const imp::BMaterialImport& material)
		{
			BMaterial out;
			out.baseColorTexture = texturePath(material.baseColorTexture);
			out.normalTexture    = texturePath(material.normalTexture);
			out.ormTexture       = texturePath(material.ormTexture);
			out.baseColorFactor  = material.baseColorFactor;
			out.metallicFactor   = material.metallicFactor;
			out.roughnessFactor  = material.roughnessFactor;
			out.name             = nameFromPool(mesh.stringPool, material.nameOffset);
			return out;
		}
	}

	std::vector<std::byte>
	serialize(const BMesh& mesh)
	{
		ByteWriter writer;
		writer.writePod(FileHeader{});  // placeholder, patched below

		const auto materialPool = packStrings(mesh.materials);

		std::array<ChunkEntry, 11> chunks = {
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
			appendChunk(writer, ChunkId::kMaterialPaths, materialPool),
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
		mesh.materials =
			unpackStrings(readChunk<char>(bytes, optional(ChunkId::kMaterialPaths, empty)));
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

	BMesh
	toBMesh(const imp::BMeshImport& mesh)
	{
		BMesh out;
		out.nodes            = mesh.nodes;
		out.roots            = mesh.roots;
		out.meshes           = mesh.meshes;
		out.submeshes        = mesh.submeshes;
		out.meshlets         = mesh.meshlets;
		out.meshletVertices  = mesh.meshletVertices;
		out.meshletTriangles = mesh.meshletTriangles;
		out.vertexData       = mesh.vertexData;
		out.indexData        = mesh.indexData;
		out.stringPool       = mesh.stringPool;

		// Submesh::material indexes this parallel list of `.bmaterial` path handles; the files
		// themselves are written by writeMaterials / bake.
		out.materials.reserve(mesh.materials.size());
		for (size_t i = 0; i < mesh.materials.size(); ++i) out.materials.push_back(materialPath(i));
		return out;
	}

	bool
	attachMaterial(BMesh& mesh, uint32_t submeshIndex, std::string_view relativePath)
	{
		if (submeshIndex >= mesh.submeshes.size())
			throw std::runtime_error("attachMaterial: submeshIndex out of range");

		Submesh&          submesh  = mesh.submeshes[submeshIndex];
		const std::string material = std::string(relativePath);

		// Rewriting the slot in place is only safe when this submesh is its sole user; otherwise every
		// sibling sharing the slot would silently change material too.
		const bool hasSlot = submesh.material < mesh.materials.size();
		const bool shared =
			hasSlot && std::ranges::count_if(mesh.submeshes, [&](const Submesh& other) {
						   return other.material == submesh.material;
					   }) > 1;

		if (hasSlot && !shared)
		{
			if (mesh.materials[submesh.material] == material)
				return false;
			mesh.materials[submesh.material] = material;
			return true;
		}

		// Move to a slot of its own, reusing one that already names this material.
		if (const auto it = std::ranges::find(mesh.materials, material); it != mesh.materials.end())
		{
			const auto index = static_cast<uint32_t>(std::distance(mesh.materials.begin(), it));
			if (submesh.material == index)
				return false;
			submesh.material = index;
			return true;
		}

		mesh.materials.push_back(material);
		submesh.material = static_cast<uint32_t>(mesh.materials.size() - 1);
		return true;
	}

	void
	writeTextures(
		const imp::BMeshImport&      mesh,
		const std::filesystem::path& outDir,
		const TextureProgressFn&     onProgress)
	{
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);

		// Textures used as base color are sRGB (tagged so the GPU sampler decodes them); normal and
		// ORM maps carry linear data and are written as-is.
		std::set<uint32_t> srgbTextures;
		for (const imp::BMaterialImport& material : mesh.materials)
			if (material.baseColorTexture != c_InvalidIndex)
				srgbTextures.insert(material.baseColorTexture);

		for (size_t i = 0; i < mesh.textures.size(); ++i)
		{
			if (onProgress)
				onProgress(i, mesh.textures.size());

			auto path = outDir / ("tex" + std::to_string(i));
			path.replace_extension(".ktx2");
			writeKTX2(mesh.textures[i], path, srgbTextures.contains(static_cast<uint32_t>(i)));
		}
	}

	void
	writeMaterials(const imp::BMeshImport& mesh, const std::filesystem::path& outDir)
	{
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);

		for (size_t i = 0; i < mesh.materials.size(); ++i)
			saveMaterial(toBMaterial(mesh, mesh.materials[i]), outDir / materialPath(i));
	}

	void
	bake(const imp::BMeshImport& mesh, const std::filesystem::path& outDir, std::string_view name)
	{
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);

		writeTextures(mesh, outDir);
		writeMaterials(mesh, outDir);
		save(toBMesh(mesh), outDir / (std::string(name) + ".bmesh"));
	}

	namespace
	{
		// Byte offset of a vertex attribute within one interleaved vertex, or -1 if the submesh's
		// layout does not carry it.
		int
		attributeByteOffset(const VertexLayout& layout, VertexSemantic semantic)
		{
			for (uint32_t i = 0; i < layout.attributeCount; ++i)
				if (layout.attributes[i].semantic == semantic)
					return layout.attributes[i].offset;
			return -1;
		}

		// One index from a submesh's raw index buffer, honoring its 16- or 32-bit width.
		uint32_t
		rawIndexAt(const BMesh& mesh, const Submesh& submesh, uint32_t i)
		{
			const std::byte* base = mesh.indexData.data() + submesh.indexByteOffset;
			if (submesh.indexType == IndexType::kUint16)
			{
				uint16_t value = 0;
				std::memcpy(&value, base + static_cast<size_t>(i) * 2, sizeof(value));
				return value;
			}
			uint32_t value = 0;
			std::memcpy(&value, base + static_cast<size_t>(i) * 4, sizeof(value));
			return value;
		}
	}

	void
	writeObj(const BMesh& mesh, const std::filesystem::path& path, bool fromMeshlets)
	{
		std::ofstream out(path);
		if (!out)
			throw std::runtime_error("obj: cannot open file for writing: " + path.string());

		out << "# Bernini BMesh -> OBJ ("
			<< (fromMeshlets ? "reconstructed from meshlets" : "raw index buffer") << ")\n";

		// OBJ vertex indices are global and 1-based; each submesh appends its vertices after the last.
		uint32_t vertexBase = 0;

		for (size_t mi = 0; mi < mesh.meshes.size(); ++mi)
		{
			const Mesh& meshEntry = mesh.meshes[mi];
			for (uint32_t s = 0; s < meshEntry.submeshCount; ++s)
			{
				const Submesh& submesh = mesh.submeshes[meshEntry.firstSubmesh + s];
				const int      posOffset =
					attributeByteOffset(submesh.layout, VertexSemantic::kPosition);
				const uint32_t stride = submesh.layout.stride;

				out << "o mesh" << mi << "_submesh" << s << "\n";

				for (uint32_t v = 0; v < submesh.vertexCount; ++v)
				{
					float            p[3]     = { 0.0f, 0.0f, 0.0f };
					const std::byte* vertBase = mesh.vertexData.data() + submesh.vertexByteOffset +
					                            static_cast<size_t>(v) * stride;
					if (posOffset >= 0)
						std::memcpy(p, vertBase + posOffset, sizeof(p));
					out << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << "\n";
				}

				const auto emitFace = [&](uint32_t a, uint32_t b, uint32_t c) {
					out << "f " << (vertexBase + a + 1) << ' ' << (vertexBase + b + 1) << ' '
						<< (vertexBase + c + 1) << "\n";
				};

				if (fromMeshlets)
				{
					// Same reconstruction the GPU (and Scene::AddStaticMesh) performs: meshlet-local
					// triangle indices -> submesh-local vertex indices via the meshlet vertex map.
					for (uint32_t m = 0; m < submesh.meshletCount; ++m)
					{
						const Meshlet& ml = mesh.meshlets[submesh.firstMeshlet + m];
						for (uint32_t t = 0; t < ml.triangleCount; ++t)
						{
							uint32_t tri[3];
							for (uint32_t k = 0; k < 3; ++k)
							{
								const uint8_t local =
									mesh.meshletTriangles[ml.triangleOffset + t * 3 + k];
								tri[k] = mesh.meshletVertices[ml.vertexOffset + local];
							}
							emitFace(tri[0], tri[1], tri[2]);
						}
					}
				}
				else
				{
					for (uint32_t i = 0; i + 2 < submesh.indexCount; i += 3)
					{
						emitFace(
							rawIndexAt(mesh, submesh, i),
							rawIndexAt(mesh, submesh, i + 1),
							rawIndexAt(mesh, submesh, i + 2));
					}
				}

				vertexBase += submesh.vertexCount;
			}
		}
	}
}
