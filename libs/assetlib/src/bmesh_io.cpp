#include <assetlib/bmesh_io.h>
#include <assetlib_structs/magic.h>

#include <assetlib/banim_io.h>
#include <assetlib/bskel_io.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

#include <assetlib/mesh_tangents.h>

#include "chunk_io.h"
#include "fs_util.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint16_t c_VersionMajor = 3;

		// +1: kSkeletonPath. A chunk is addressed by id and an absent one is not an error, so a v3.0
		// mesh still reads -- it simply names no skeleton, which is what a static mesh is.
		constexpr uint16_t c_VersionMinor = 1;

		constexpr std::string_view c_What = "bmesh";

		enum ChunkId : uint32_t
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
	serialize(const BMesh& mesh)
	{
		requireSkeletonIfSkinned(mesh);

		chunk::Writer writer;
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
		writer.Add(ChunkId::kMaterialPaths, chunk::packStrings(mesh.materials));
		writer.Add(
			ChunkId::kSkeletonPath,
			std::vector<char>(mesh.skeleton.begin(), mesh.skeleton.end()));
		return writer.Finish(magic::c_BMesh, c_VersionMajor, c_VersionMinor);
	}

	BMesh
	deserialize(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, magic::c_BMesh, c_VersionMajor, c_What);

		BMesh mesh;
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
		mesh.materials        = chunk::unpackStrings(reader.Read<char>(ChunkId::kMaterialPaths));

		const auto skeleton = reader.Read<char>(ChunkId::kSkeletonPath);
		mesh.skeleton.assign(skeleton.begin(), skeleton.end());

		requireSkeletonIfSkinned(mesh);
		return mesh;
	}

	void
	save(const BMesh& mesh, const std::filesystem::path& path)
	{
		writeFileBytes(path, serialize(mesh), "bmesh");
	}

	BMesh
	load(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserialize(bytes);
	}

	MeshRefs
	loadMeshRefs(const std::filesystem::path& path)
	{
		constexpr std::array<uint32_t, 2> c_Wanted = { { ChunkId::kMaterialPaths,
			                                             ChunkId::kSkeletonPath } };

		const auto chunks =
			chunk::readChunksFromFile(path, magic::c_BMesh, c_VersionMajor, c_Wanted, c_What);

		// Absent, not malformed: both chunks are optional, and a mesh that names neither is exactly
		// what a static import produces.
		MeshRefs refs;
		if (const auto it = chunks.find(ChunkId::kMaterialPaths); it != chunks.end())
			refs.materials = chunk::unpackStrings(
				std::span<const char>(
					reinterpret_cast<const char*>(it->second.data()),
					it->second.size()));

		if (const auto it = chunks.find(ChunkId::kSkeletonPath); it != chunks.end())
			refs.skeleton.assign(
				reinterpret_cast<const char*>(it->second.data()),
				it->second.size());

		return refs;
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

	std::string
	textureFileName(size_t index)
	{
		return "tex" + std::to_string(index) + ".ktx2";
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

		for (size_t i = 0; i < mesh.textures.size(); ++i)
		{
			throwIfCancelled(cancel);

			if (onProgress)
				onProgress(i, mesh.textures.size());

			writeKTX2(
				mesh.textures[i],
				outDir / textureFileName(i),
				srgbTextures.contains(static_cast<uint32_t>(i)));
		}
	}

	std::string
	skeletonFileName(std::string_view name)
	{
		return std::string(name) + ".bskel";
	}

	std::string
	animationFileName(std::string_view name)
	{
		return std::string(name) + ".banim";
	}

	TangentGenResult
	bake(
		const imp::BMeshImport&      mesh,
		const std::filesystem::path& outDir,
		std::string_view             name,
		const CancelToken&           cancel)
	{
		createDirectories(outDir);

		writeTextures(mesh, outDir, {}, cancel);

		BMesh      baked    = toBMesh(mesh);
		const auto tangents = generateTangents(baked);

		// A baked directory is its own data root, so the rig is named the way a texture is: by file
		// name alone, relative to the directory the three land in together.
		if (!mesh.skeleton.bones.empty())
		{
			baked.skeleton = skeletonFileName(name);
			saveSkeleton(mesh.skeleton, outDir / baked.skeleton);

			if (!mesh.animations.clips.empty())
			{
				AnimationSet animations = mesh.animations;
				animations.skeleton     = baked.skeleton;
				saveAnimations(animations, outDir / animationFileName(name));
			}
		}

		save(baked, outDir / (std::string(name) + ".bmesh"));
		return tangents;
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
