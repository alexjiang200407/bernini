#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/magic.h>

#include <assetlib/container_format.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

#include <assetlib/mesh_tangents.h>
#include <assetlib/skinning.h>

#include "cache_io.h"
#include "fs_util.h"
#include "ref_paths.h"

#include <core/file/file.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "bmesh";

		std::string
		asciiLower(std::string_view text)
		{
			auto lowered = std::string(text);
			for (char& c : lowered)
				if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c - 'A' + 'a');
			return lowered;
		}

		// Only these come off a name; anything else after a dot is part of what the artist called
		// the image (`Body_v1.2`, `diffuse.2k`) and truncating it would rename their texture.
		constexpr std::string_view c_ImageExtensions[] = { ".png", ".jpg",  ".jpeg", ".ktx2",
			                                               ".ktx", ".webp", ".tga",  ".bmp",
			                                               ".tif", ".tiff", ".dds",  ".exr",
			                                               ".hdr" };

		/**
		 * `name` reduced to a portable file stem: a trailing image extension dropped, every
		 * character outside `[A-Za-z0-9-_]` folded to `_`, and the runs that produces collapsed.
		 * Empty when nothing survives, which is the caller's cue to fall back to the index.
		 *
		 * A source may name an image anything at all -- a path, a UTF-8 label, `Base Color.png` --
		 * and this name becomes a mount key, which is matched byte-for-byte inside an archive.
		 */
		std::string
		sanitizeTextureStem(std::string_view name)
		{
			const std::string extension = extensionOf(name);
			if (std::ranges::find(c_ImageExtensions, extension) !=
			    std::ranges::end(c_ImageExtensions))
				name.remove_suffix(extension.size());

			auto stem = std::string();
			stem.reserve(name.size());
			for (const char c : name)
			{
				const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				                  (c >= '0' && c <= '9') || c == '-' || c == '_';
				if (keep)
					stem += c;
				else if (!stem.empty() && stem.back() != '_')
					stem += '_';
			}

			while (!stem.empty() && stem.back() == '_') stem.pop_back();
			return stem;
		}

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
			kMaterialPaths,
			kSkeletonPath
		};

		bool
		carriesJoints(const Submesh& submesh) noexcept
		{
			return std::ranges::any_of(
				std::span(submesh.layout.attributes.data(), submesh.layout.attributeCount),
				[](const VertexAttribute& attribute) {
					return attribute.semantic == VertexSemantic::kJoints0;
				});
		}

		/**
		 * Joint indices address a bone array, so a mesh carrying them and naming no skeleton is a mesh
		 * whose vertices point at nothing -- and nothing downstream can tell, because a joint index is
		 * a bare number. Checked at both ends: at write, so the file is never produced, and at read,
		 * because a file may not have come from here.
		 *
		 * Only one direction. Naming a skeleton without carrying joints is how a static attachment --
		 * a scabbard, a saddle -- hangs off a bone.
		 */
		void
		requireSkeletonIfSkinned(const BMesh& mesh)
		{
			if (isSkinned(mesh) && mesh.skeleton.empty())
				throw std::runtime_error(
					"bmesh: carries joint indices but names no skeleton, so they resolve to nothing"
					" (bake it with assetlib_cli, which writes the rig alongside the mesh)");
		}
	}

	std::vector<std::byte>
	AssetCodec<BMesh>::Serialize(const BMesh& mesh)
	{
		requireSkeletonIfSkinned(mesh);

		cache::Writer writer;
		writer.Add(ChunkId::kNodes, mesh.nodes);
		writer.Add(ChunkId::kRoots, mesh.roots);
		writer.Add(ChunkId::kMeshes, mesh.meshes);
		writer.Add(ChunkId::kSubmeshes, mesh.submeshes);
		writer.Add(ChunkId::kMeshlets, mesh.meshlets);
		writer.Add(ChunkId::kMeshletVertices, mesh.meshletVertices);
		writer.Add(ChunkId::kMeshletTriangles, mesh.meshletTriangles);
		writer.Add(ChunkId::kVertexData, mesh.vertexData);
		writer.Add(ChunkId::kIndexData, mesh.indexData);
		writer.Add(ChunkId::kStringPool, mesh.stringPool.bytes());
		writer.Add(ChunkId::kMaterialPaths, cache::packStrings(mesh.materials));
		writer.Add(ChunkId::kSkeletonPath, std::span<const char>(mesh.skeleton));
		return writer.Finish(magic::c_BMesh, AssetCodec<BMesh>::c_BakeToken, mesh.source);
	}

	BMesh
	AssetCodec<BMesh>::Deserialize(std::span<const std::byte> bytes)
	{
		const cache::Reader reader(bytes, magic::c_BMesh, AssetCodec<BMesh>::c_BakeToken, c_What);

		BMesh mesh;
		mesh.source           = reader.GetSource();
		mesh.nodes            = reader.Require<Node>(ChunkId::kNodes);
		mesh.meshes           = reader.Require<Mesh>(ChunkId::kMeshes);
		mesh.roots            = reader.Read<uint32_t>(ChunkId::kRoots);
		mesh.submeshes        = reader.Read<Submesh>(ChunkId::kSubmeshes);
		mesh.meshlets         = reader.Read<Meshlet>(ChunkId::kMeshlets);
		mesh.meshletVertices  = reader.Read<uint32_t>(ChunkId::kMeshletVertices);
		mesh.meshletTriangles = reader.Read<uint8_t>(ChunkId::kMeshletTriangles);
		mesh.vertexData       = reader.Read<std::byte>(ChunkId::kVertexData);
		mesh.indexData        = reader.Read<std::byte>(ChunkId::kIndexData);
		mesh.stringPool       = core::string_pool(reader.Read<char>(ChunkId::kStringPool));
		mesh.materials        = cache::unpackStrings(reader.Read<char>(ChunkId::kMaterialPaths));

		const auto skeleton = reader.Read<char>(ChunkId::kSkeletonPath);
		mesh.skeleton.assign(skeleton.begin(), skeleton.end());

		requireSkeletonIfSkinned(mesh);
		return mesh;
	}

	namespace
	{
		constexpr std::array<uint32_t, 2> c_WantedRefChunks = {
			{ static_cast<uint32_t>(ChunkId::kMaterialPaths),
			  static_cast<uint32_t>(ChunkId::kSkeletonPath) }
		};

		MeshRefs
		refsFromChunks(const cache::CacheData& chunks)
		{
			// Absent, not malformed: both chunks are optional, and a mesh that names neither is
			// exactly what a static import produces.
			MeshRefs   refs;
			const auto paths = chunks.Read<char>(ChunkId::kMaterialPaths, c_What);
			if (!paths.empty())
				refs.materials = cache::unpackStrings(paths);

			const auto skeleton = chunks.Read<char>(ChunkId::kSkeletonPath, c_What);
			refs.skeleton.assign(skeleton.begin(), skeleton.end());

			return refs;
		}
	}

	MeshRefs
	loadMeshRefs(const std::filesystem::path& path)
	{
		return refsFromChunks(
			cache::readCacheChunksFromFile(
				path,
				magic::c_BMesh,
				AssetCodec<BMesh>::c_BakeToken,
				c_WantedRefChunks,
				c_What));
	}

	MeshRefs
	loadMeshRefs(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return refsFromChunks(
			cache::readCacheChunksFrom(
				fileSystem,
				path,
				magic::c_BMesh,
				AssetCodec<BMesh>::c_BakeToken,
				c_WantedRefChunks,
				c_What));
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

		for (Submesh& submesh : out.submeshes) submesh.material = c_InvalidIndex;

		return out;
	}

	bool
	isSkinned(const BMesh& mesh) noexcept
	{
		return std::ranges::any_of(mesh.submeshes, carriesJoints);
	}

	bool
	isSkinned(const BMesh& mesh, uint32_t meshIndex) noexcept
	{
		if (meshIndex >= mesh.meshes.size())
			return false;

		const Mesh& entry = mesh.meshes[meshIndex];
		if (entry.firstSubmesh > mesh.submeshes.size() ||
		    entry.submeshCount > mesh.submeshes.size() - entry.firstSubmesh)
			return false;

		return std::ranges::any_of(
			std::span(mesh.submeshes).subspan(entry.firstSubmesh, entry.submeshCount),
			carriesJoints);
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

	std::vector<std::string>
	importedTextureFileNames(const imp::BMeshImport& mesh)
	{
		auto names = std::vector<std::string>();
		names.reserve(mesh.textures.size());

		for (size_t i = 0; i < mesh.textures.size(); ++i)
		{
			const std::string_view given =
				i < mesh.textureNames.size() ? std::string_view(mesh.textureNames[i]) : "";

			std::string stem = sanitizeTextureStem(given);
			if (stem.empty())
				stem = "tex" + std::to_string(i);
			names.push_back(std::move(stem));
		}

		auto claimed = std::set<std::string>();
		for (size_t i = 0; i < names.size(); ++i)
		{
			// Appending the index can itself collide with an image literally named that way, so
			// this repeats until the name is free rather than assuming one pass is enough.
			while (!claimed.insert(asciiLower(names[i])).second)
				names[i] += "_" + std::to_string(i);

			names[i] += c_TextureExtension;
		}

		return names;
	}

	void
	writeTextures(
		const imp::BMeshImport&      mesh,
		const std::filesystem::path& outDir,
		const TextureProgressFn&     onProgress,
		const CancelToken&           cancel)
	{
		createDirectories(outDir);

		// Textures used as base color are sRGB (tagged so the GPU sampler decodes them); normal and
		// ORM maps carry linear data and are written as-is.
		std::set<uint32_t> srgbTextures;
		for (const imp::BMaterialImport& material : mesh.materials)
			if (material.baseColorTexture != c_InvalidIndex)
				srgbTextures.insert(material.baseColorTexture);

		const std::vector<std::string> names = importedTextureFileNames(mesh);

		for (size_t i = 0; i < mesh.textures.size(); ++i)
		{
			throwIfCancelled(cancel);

			if (onProgress)
				onProgress(i, mesh.textures.size());

			writeKTX2(
				mesh.textures[i],
				outDir / names[i],
				srgbTextures.contains(static_cast<uint32_t>(i)));
		}
	}

	std::string
	skeletonFileName(std::string_view name)
	{
		return std::format("{}.bskel", name);
	}

	std::string
	animationFileName(std::string_view name)
	{
		return std::format("{}.banim", name);
	}

	namespace
	{
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
		errno = 0;
		std::ofstream out(path);
		if (!out)
			throw std::runtime_error(fileErrorMessage("obj: cannot open file for writing", path));

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
				const auto posOffset   = attributeOffset(submesh.layout, VertexSemantic::kPosition);
				const uint32_t stride  = submesh.layout.stride;

				out << "o mesh" << mi << "_submesh" << s << "\n";

				for (uint32_t v = 0; v < submesh.vertexCount; ++v)
				{
					float            p[3]     = { 0.0f, 0.0f, 0.0f };
					const std::byte* vertBase = mesh.vertexData.data() + submesh.vertexByteOffset +
					                            static_cast<size_t>(v) * stride;
					if (posOffset)
						std::memcpy(p, vertBase + *posOffset, sizeof(p));
					out << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << "\n";
				}

				const auto emitFace = [&](uint32_t a, uint32_t b, uint32_t c) {
					out << "f " << (vertexBase + a + 1) << ' ' << (vertexBase + b + 1) << ' '
						<< (vertexBase + c + 1) << "\n";
				};

				if (fromMeshlets)
				{
					// Same reconstruction the GPU (and Scene::AddStaticMeshGeom) performs: meshlet-local
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
