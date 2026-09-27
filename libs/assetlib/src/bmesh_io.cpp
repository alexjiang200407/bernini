#include <algorithm>
#include <array>
#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/magic.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/GrassGeometry.h>
#include <assetlib_structs/Skeleton.h>

#include <assetlib/mesh_tangents.h>
#include <assetlib/skinning.h>
#include <assetlib/vertex_layout.h>

#include "cache_io.h"
#include "fs_util.h"
#include "progress_report.h"
#include "ref_paths.h"
#include <assetlib/cancel.h>
#include <assetlib/progress.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMaterialImport.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/VertexLayout.h>

#include <cerrno>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

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

		// An allowlist, because anything else after a dot is part of the artist's name for the
		// image (`Body_v1.2`) and truncating it would rename their texture.
		constexpr std::string_view c_ImageExtensions[] = { ".png", ".jpg",  ".jpeg", ".ktx2",
			                                               ".ktx", ".webp", ".tga",  ".bmp",
			                                               ".tif", ".tiff", ".dds",  ".exr",
			                                               ".hdr" };

		/**
		 * `name` reduced to a portable file stem: trailing image extension dropped, anything
		 * outside `[A-Za-z0-9-_]` folded to `_` and collapsed. Empty when nothing survives.
		 * A source may name an image anything, and this becomes a mount key.
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
			kSkeletonSignature,
			kSkeletonBoneNames,  // the cooked rig's bone names, in bone order
			kGeometrySignature,  // the vertex blob and the tables addressing it, hashed at cook
			kMeshletGroups,      // one bound per run of c_MeshletsPerGroup meshlets
			kLods,               // each mesh's levels of detail, see Mesh::firstLod
			kGrassFields,
			kGrassNames,
			kGrassChunks,
			kGrassClumps,
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

		void
		validateGrassGeometry(const BMesh& mesh)
		{
			const auto& grass = mesh.grassFields;
			auto        names = std::unordered_set<std::string_view>();
			for (size_t i = 0; i < grass.fields.size(); ++i)
			{
				const auto& named = grass.fields[i];
				const auto& field = named.field;
				if (named.name.empty() || !names.insert(named.name).second)
					core::throw_runtime_error("bmesh: grass fields need unique nonempty names");
				if (field.mesh >= mesh.meshes.size() || field.look != i || field.chunkCount == 0 ||
				    static_cast<uint64_t>(field.firstChunk) + field.chunkCount >
				        grass.chunks.size())
					core::throw_runtime_error("bmesh: invalid grass field {}", i);
			}
			for (const auto& chunk : grass.chunks)
				if (chunk.clumpCount == 0 || chunk.clumpCount > c_GrassClumpsPerChunk ||
				    static_cast<uint64_t>(chunk.firstClump) + chunk.clumpCount >
				        grass.clumps.size())
					core::throw_runtime_error("bmesh: invalid grass chunk range");
		}

	}

	std::vector<std::byte>
	AssetCodec<BMesh>::Serialize(const BMesh& mesh)
	{
		validateGrassGeometry(mesh);

		cache::Writer writer;
		writer.Add(ChunkId::kNodes, mesh.nodes);
		writer.Add(ChunkId::kRoots, mesh.roots);
		writer.Add(ChunkId::kMeshes, mesh.meshes);
		writer.Add(ChunkId::kSubmeshes, mesh.submeshes);
		writer.Add(ChunkId::kLods, mesh.lods);
		writer.Add(ChunkId::kMeshlets, mesh.meshlets);
		writer.Add(ChunkId::kMeshletGroups, mesh.meshletGroups);
		writer.Add(ChunkId::kMeshletVertices, mesh.meshletVertices);
		writer.Add(ChunkId::kMeshletTriangles, mesh.meshletTriangles);
		writer.Add(ChunkId::kVertexData, mesh.vertexData);
		writer.Add(ChunkId::kIndexData, mesh.indexData);
		writer.Add(ChunkId::kStringPool, mesh.stringPool.bytes());
		writer.Add(
			ChunkId::kSkeletonSignature,
			std::span<const uint64_t>(&mesh.skeletonSignature, 1));
		writer.Add(ChunkId::kSkeletonBoneNames, cache::packStrings(mesh.skeletonBoneNames));
		auto fields     = std::vector<GrassField>();
		auto fieldNames = std::vector<std::string>();
		fields.reserve(mesh.grassFields.fields.size());
		fieldNames.reserve(mesh.grassFields.fields.size());
		for (const auto& named : mesh.grassFields.fields)
		{
			fields.push_back(named.field);
			fieldNames.push_back(named.name);
		}
		writer.Add(ChunkId::kGrassFields, fields);
		writer.Add(ChunkId::kGrassNames, cache::packStrings(fieldNames));
		writer.Add(ChunkId::kGrassChunks, mesh.grassFields.chunks);
		writer.Add(ChunkId::kGrassClumps, mesh.grassFields.clumps);

		// Computed here rather than taken from the struct, so a producer that rewrote the blob and
		// forgot the field cannot write a file that disagrees with its own geometry.
		const uint64_t geometry = geometrySignature(mesh);
		writer.Add(ChunkId::kGeometrySignature, std::span<const uint64_t>(&geometry, 1));
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
		mesh.lods             = reader.Read<MeshLod>(ChunkId::kLods);
		mesh.meshlets         = reader.Read<Meshlet>(ChunkId::kMeshlets);
		mesh.meshletGroups    = reader.Read<MeshletGroup>(ChunkId::kMeshletGroups);
		mesh.meshletVertices  = reader.Read<uint32_t>(ChunkId::kMeshletVertices);
		mesh.meshletTriangles = reader.Read<uint8_t>(ChunkId::kMeshletTriangles);
		mesh.vertexData       = reader.Read<std::byte>(ChunkId::kVertexData);
		mesh.indexData        = reader.Read<std::byte>(ChunkId::kIndexData);
		mesh.stringPool       = core::string_pool(reader.Read<char>(ChunkId::kStringPool));

		const auto signature   = reader.Read<uint64_t>(ChunkId::kSkeletonSignature);
		mesh.skeletonSignature = signature.empty() ? 0 : signature.front();
		mesh.skeletonBoneNames =
			cache::unpackStrings(reader.Read<char>(ChunkId::kSkeletonBoneNames));

		const auto fields     = reader.Read<GrassField>(ChunkId::kGrassFields);
		const auto fieldNames = cache::unpackStrings(reader.Read<char>(ChunkId::kGrassNames));
		if (fields.size() != fieldNames.size())
			core::throw_runtime_error("bmesh: grass field and name counts disagree");
		mesh.grassFields.fields.reserve(fields.size());
		for (size_t i = 0; i < fields.size(); ++i)
			mesh.grassFields.fields.push_back({ fieldNames[i], fields[i] });
		mesh.grassFields.chunks = reader.Read<GrassChunk>(ChunkId::kGrassChunks);
		mesh.grassFields.clumps = reader.Read<GrassClump>(ChunkId::kGrassClumps);
		validateGrassGeometry(mesh);

		const auto geometry    = reader.Read<uint64_t>(ChunkId::kGeometrySignature);
		mesh.geometrySignature = geometry.empty() ? 0 : geometry.front();

		return mesh;
	}

	BMesh
	toBMesh(const imp::BMeshImport& mesh)
	{
		BMesh out;
		out.nodes            = mesh.nodes;
		out.roots            = mesh.roots;
		out.meshes           = mesh.meshes;
		out.submeshes        = mesh.submeshes;
		out.lods             = mesh.lods;
		out.meshlets         = mesh.meshlets;
		out.meshletGroups    = mesh.meshletGroups;
		out.meshletVertices  = mesh.meshletVertices;
		out.meshletTriangles = mesh.meshletTriangles;
		out.vertexData       = mesh.vertexData;
		out.indexData        = mesh.indexData;
		out.stringPool       = mesh.stringPool;
		if (mesh.grass.names.size() != mesh.grass.fields.size())
			core::throw_runtime_error("mesh import: grass field and name counts disagree");
		out.grassFields.fields.reserve(mesh.grass.fields.size());
		for (size_t i = 0; i < mesh.grass.fields.size(); ++i)
		{
			auto field = mesh.grass.fields[i];
			field.look = static_cast<uint32_t>(i);
			out.grassFields.fields.push_back({ mesh.grass.names[i], field });
		}
		out.grassFields.chunks = mesh.grass.chunks;
		out.grassFields.clumps = mesh.grass.clumps;

		return out;
	}

	uint64_t
	geometrySignature(const BMesh& mesh) noexcept
	{
		uint64_t hash =
			core::hash_bytes(mesh.vertexData.data(), mesh.vertexData.size(), core::hash_seed());

		// The tables that say which of those bytes a mesh index means: without them, a re-export
		// that regroups entries over identical bytes would keep matching a measurement that no
		// longer holds. Materials are left out -- swapping one does not move a vertex.
		for (const Mesh& entry : mesh.meshes)
		{
			hash = core::hash_pod(entry.firstSubmesh, hash);
			hash = core::hash_pod(entry.submeshCount, hash);
		}
		for (const Submesh& submesh : mesh.submeshes)
		{
			hash = core::hash_pod(submesh.vertexByteOffset, hash);
			hash = core::hash_pod(submesh.vertexCount, hash);
			hash = core::hash_pod(submesh.layout.stride, hash);
			for (uint32_t i = 0; i < submesh.layout.attributeCount; ++i)
			{
				const VertexAttribute& attribute = submesh.layout.attributes[i];
				hash                             = core::hash_pod(attribute.semantic, hash);
				hash                             = core::hash_pod(attribute.format, hash);
				hash                             = core::hash_pod(attribute.offset, hash);
			}
		}

		// Never zero, which the field reserves for "not recorded": a mesh whose geometry happened
		// to hash to it would be re-hashed on every load, silently and forever.
		return hash != 0 ? hash : 1;
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

	namespace
	{
		/**
		 * The stem for an image the source named nothing: `tex_` and its content, hashed. Positional
		 * (`tex0`, `tex1`) is what this replaces -- inserting an image renumbered every later one, so
		 * a material's route silently resolved to a different picture. An unnamed image has no
		 * identity to keep stable, so naming it after its bytes costs nothing a name would have held.
		 *
		 * Mip 0 alone, and no format tag: that is the decoded source image verbatim, while the chain
		 * below it and the tag are what this engine made of it. Hashing either would rename every
		 * file whenever mip generation or colour handling changed -- leaving every authored route
		 * naming a file that no longer exists, which `followMovedTextures` cannot repair because the
		 * bytes it matches on are the ones that moved.
		 */
		std::string
		unnamedTextureStem(const ImageData& image)
		{
			const size_t mip0 = image.subresources.empty() ?
			                        image.pixels.size() :
			                        static_cast<size_t>(image.subresources.front().slicePitch);

			uint64_t hash = core::hash_bytes(
				image.pixels.data(),
				(std::min)(mip0, image.pixels.size()),
				core::hash_seed());
			hash = core::hash_pod(image.width, hash);
			hash = core::hash_pod(image.height, hash);
			return std::format("tex_{:016x}", hash);
		}
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
				stem = unnamedTextureStem(mesh.textures[i]);
			names.push_back(std::move(stem));
		}

		auto claimed = std::set<std::string>();
		for (size_t i = 0; i < names.size(); ++i)
		{
			// Repeats: the suffixed name can itself collide with an image named that way.
			while (!claimed.insert(asciiLower(names[i])).second)
				names[i] += "_" + std::to_string(i);

			names[i] += c_TextureExtension;
		}

		return names;
	}

	std::vector<std::string>
	AssetStore::WriteTextures(
		const imp::BMeshImport& mesh,
		std::string_view        textureDir,
		const ProgressSink&     onProgress,
		const CancelToken&      cancel) const
	{
		requireOrigin(textureDir, AssetOrigin::kDerived, "textures");

		const std::filesystem::path outDir = ResolveWritePath(textureDir);
		createDirectories(outDir);

		const std::vector<std::string> names = importedTextureFileNames(mesh);

		for (size_t i = 0; i < mesh.textures.size(); ++i)
		{
			throwIfCancelled(cancel);

			reportStep(
				onProgress,
				ProgressPhase::kExtractingTextures,
				names[i],
				i,
				mesh.textures.size());

			// The extract tagged the images its materials read as colour, and the tag is written.
			writeKTX2(mesh.textures[i], outDir / names[i]);
		}

		auto keys = std::vector<std::string>();
		keys.reserve(names.size());
		for (const std::string& name : names) keys.push_back(std::string(textureDir) + "/" + name);
		return keys;
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

	bool
	meshMatchesSkeleton(const BMesh& mesh, const Skeleton& skeleton) noexcept
	{
		return !isSkinned(mesh) || mesh.skeletonSignature == skeletonSignature(skeleton);
	}

}
