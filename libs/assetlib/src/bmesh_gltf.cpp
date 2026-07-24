#include <assetlib/bmesh_gltf.h>

#include "bmesh_texture.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

// tiny_gltf.h and stb_image.h both put their implementation *outside* their include guard, so any
// later include of either -- which the headers below make -- would emit a second copy of it.
#undef TINYGLTF_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION

#include "gltf_skin.h"
#include "gltf_util.h"

#include <meshoptimizer.h>

namespace assetlib
{
	using namespace imp;

	namespace
	{
		constexpr size_t c_MeshletMaxVertices  = 64;
		constexpr size_t c_MeshletMaxTriangles = 124;
		constexpr float  c_MeshletConeWeight   = 0.0f;

		/** A strided view over one float vertex attribute in a glTF buffer. */
		struct AttributeView
		{
			const std::byte* base       = nullptr;
			size_t           stride     = 0;
			int              components = 0;

			[[nodiscard]] bool
			present() const noexcept
			{
				return base != nullptr;
			}

			[[nodiscard]] const float*
			at(size_t index) const noexcept
			{
				return reinterpret_cast<const float*>(base + index * stride);
			}
		};

		AttributeView
		makeView(
			const tinygltf::Model&     model,
			const tinygltf::Primitive& primitive,
			const char*                semantic)
		{
			const auto it = primitive.attributes.find(semantic);
			if (it == primitive.attributes.end())
				return {};

			const auto& accessor = model.accessors[static_cast<size_t>(it->second)];
			if (accessor.sparse.isSparse)
				throw std::runtime_error("bmesh: sparse accessors are not supported");
			if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
				throw std::runtime_error("bmesh: only float vertex attributes are supported");

			const auto& view   = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
			const auto& buffer = model.buffers[static_cast<size_t>(view.buffer)];
			const int   components =
				tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
			const size_t stride = view.byteStride != 0 ?
			                          view.byteStride :
			                          static_cast<size_t>(components) * sizeof(float);

			AttributeView out;
			out.base   = reinterpret_cast<const std::byte*>(buffer.data.data()) + view.byteOffset +
			             accessor.byteOffset;
			out.stride = stride;
			out.components = components;
			return out;
		}

		std::vector<uint32_t>
		readIndices(
			const tinygltf::Model&     model,
			const tinygltf::Primitive& primitive,
			size_t                     vertexCount)
		{
			std::vector<uint32_t> indices;
			if (primitive.indices < 0)
			{
				indices.resize(vertexCount);
				for (size_t i = 0; i < vertexCount; ++i) indices[i] = static_cast<uint32_t>(i);
				return indices;
			}

			const auto& accessor = model.accessors[static_cast<size_t>(primitive.indices)];
			if (accessor.sparse.isSparse)
				throw std::runtime_error("bmesh: sparse index accessors are not supported");

			const auto&      view   = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
			const auto&      buffer = model.buffers[static_cast<size_t>(view.buffer)];
			const std::byte* base   = reinterpret_cast<const std::byte*>(buffer.data.data()) +
			                          view.byteOffset + accessor.byteOffset;
			const size_t     componentSize = static_cast<size_t>(
				tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType)));
			const size_t stride = view.byteStride != 0 ? view.byteStride : componentSize;

			indices.resize(accessor.count);
			for (size_t i = 0; i < accessor.count; ++i)
			{
				const std::byte* ptr = base + i * stride;
				switch (accessor.componentType)
				{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					indices[i] = static_cast<uint32_t>(std::to_integer<uint8_t>(*ptr));
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				{
					uint16_t value = 0;
					std::copy_n(ptr, sizeof(value), reinterpret_cast<std::byte*>(&value));
					indices[i] = value;
					break;
				}
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				{
					uint32_t value = 0;
					std::copy_n(ptr, sizeof(value), reinterpret_cast<std::byte*>(&value));
					indices[i] = value;
					break;
				}
				default:
					throw std::runtime_error("bmesh: unsupported index component type");
				}
			}
			return indices;
		}

		void
		appendBytes(std::vector<std::byte>& dst, const void* data, size_t size)
		{
			const auto* first = static_cast<const std::byte*>(data);
			dst.insert(dst.end(), first, first + size);
		}

		void
		buildMeshlets(BMeshImport& mesh, Submesh& submesh, const std::vector<uint32_t>& indices)
		{
			const auto* positions =
				reinterpret_cast<const float*>(mesh.vertexData.data() + submesh.vertexByteOffset);
			const size_t vertexCount = submesh.vertexCount;
			const size_t stride      = submesh.layout.stride;

			const size_t maxMeshlets = meshopt_buildMeshletsBound(
				indices.size(),
				c_MeshletMaxVertices,
				c_MeshletMaxTriangles);
			std::vector<meshopt_Meshlet> moMeshlets(maxMeshlets);
			std::vector<uint32_t>        moVertices(maxMeshlets * c_MeshletMaxVertices);
			std::vector<uint8_t>         moTriangles(maxMeshlets * c_MeshletMaxTriangles * 3);

			const size_t count = meshopt_buildMeshlets(
				moMeshlets.data(),
				moVertices.data(),
				moTriangles.data(),
				indices.data(),
				indices.size(),
				positions,
				vertexCount,
				stride,
				c_MeshletMaxVertices,
				c_MeshletMaxTriangles,
				c_MeshletConeWeight);

			submesh.firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
			submesh.meshletCount = static_cast<uint32_t>(count);
			if (count == 0)
				return;

			const auto&  last      = moMeshlets[count - 1];
			const size_t usedVerts = last.vertex_offset + last.vertex_count;
			const size_t usedTris =
				last.triangle_offset + static_cast<size_t>(last.triangle_count) * 3;

			const auto baseVertex   = static_cast<uint32_t>(mesh.meshletVertices.size());
			const auto baseTriangle = static_cast<uint32_t>(mesh.meshletTriangles.size());
			mesh.meshletVertices.insert(
				mesh.meshletVertices.end(),
				moVertices.begin(),
				moVertices.begin() + static_cast<ptrdiff_t>(usedVerts));
			mesh.meshletTriangles.insert(
				mesh.meshletTriangles.end(),
				moTriangles.begin(),
				moTriangles.begin() + static_cast<ptrdiff_t>(usedTris));

			for (size_t i = 0; i < count; ++i)
			{
				const auto& mo     = moMeshlets[i];
				const auto  bounds = meshopt_computeMeshletBounds(
					&moVertices[mo.vertex_offset],
					&moTriangles[mo.triangle_offset],
					mo.triangle_count,
					positions,
					vertexCount,
					stride);

				Meshlet meshlet{};
				meshlet.vertexOffset   = baseVertex + mo.vertex_offset;
				meshlet.triangleOffset = baseTriangle + mo.triangle_offset;
				meshlet.vertexCount    = mo.vertex_count;
				meshlet.triangleCount  = mo.triangle_count;
				meshlet.boundingCenter =
					glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
				meshlet.boundingRadius = bounds.radius;
				mesh.meshlets.push_back(meshlet);
			}
		}

		void
		buildSubmesh(
			BMeshImport&               mesh,
			const tinygltf::Model&     model,
			const tinygltf::Primitive& primitive)
		{
			const auto posIt = primitive.attributes.find("POSITION");
			if (posIt == primitive.attributes.end())
				return;  // nothing to draw without positions

			const auto& posAccessor = model.accessors[static_cast<size_t>(posIt->second)];
			const auto  vertexCount = posAccessor.count;

			struct SourceAttribute
			{
				VertexSemantic semantic;
				VertexFormat   format;
				AttributeView  view;
				int            components;
			};

			const SourceAttribute candidates[] = {
				{ VertexSemantic::kPosition,
				  VertexFormat::kFloat32x3,
				  makeView(model, primitive, "POSITION"),
				  3 },
				{ VertexSemantic::kNormal,
				  VertexFormat::kFloat32x3,
				  makeView(model, primitive, "NORMAL"),
				  3 },
				{ VertexSemantic::kTexCoord0,
				  VertexFormat::kFloat32x2,
				  makeView(model, primitive, "TEXCOORD_0"),
				  2 },
				{ VertexSemantic::kTangent,
				  VertexFormat::kFloat32x4,
				  makeView(model, primitive, "TANGENT"),
				  4 },
			};

			// Build the interleaved layout from the present attributes only.
			Submesh  submesh{};
			uint16_t offset = 0;
			for (const auto& attr : candidates)
			{
				if (!attr.view.present())
					continue;
				submesh.layout.attributes[submesh.layout.attributeCount++] = { attr.semantic,
					                                                           attr.format,
					                                                           offset };
				offset += static_cast<uint16_t>(formatSize(attr.format));
			}

			submesh.layout.stride = offset;

			submesh.vertexByteOffset = static_cast<uint32_t>(mesh.vertexData.size());
			submesh.vertexCount      = static_cast<uint32_t>(vertexCount);
			submesh.material = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) :
			                                             c_InvalidIndex;

			// Interleave the present attributes into the vertex blob at the layout's stride.
			mesh.vertexData.reserve(mesh.vertexData.size() + vertexCount * submesh.layout.stride);
			for (size_t i = 0; i < vertexCount; ++i)
			{
				for (const auto& attr : candidates)
				{
					if (!attr.view.present())
						continue;
					appendBytes(
						mesh.vertexData,
						attr.view.at(i),
						static_cast<size_t>(attr.components) * sizeof(float));
				}
			}

			// Prefer the accessor's declared bounds; otherwise compute from positions.
			glm::vec3 aabbMin(std::numeric_limits<float>::max());
			glm::vec3 aabbMax(std::numeric_limits<float>::lowest());
			if (posAccessor.minValues.size() == 3 && posAccessor.maxValues.size() == 3)
			{
				aabbMin = glm::vec3(
					posAccessor.minValues[0],
					posAccessor.minValues[1],
					posAccessor.minValues[2]);
				aabbMax = glm::vec3(
					posAccessor.maxValues[0],
					posAccessor.maxValues[1],
					posAccessor.maxValues[2]);
			}
			else
			{
				for (size_t i = 0; i < vertexCount; ++i)
				{
					const float* p = candidates[0].view.at(i);
					aabbMin        = glm::min(aabbMin, glm::vec3(p[0], p[1], p[2]));
					aabbMax        = glm::max(aabbMax, glm::vec3(p[0], p[1], p[2]));
				}
			}
			submesh.aabbMin = aabbMin;
			submesh.aabbMax = aabbMax;

			const auto indices      = readIndices(model, primitive, vertexCount);
			submesh.indexByteOffset = static_cast<uint32_t>(mesh.indexData.size());
			submesh.indexCount      = static_cast<uint32_t>(indices.size());
			if (vertexCount <= 0xFFFF)
			{
				submesh.indexType = IndexType::kUint16;
				for (const auto index : indices)
				{
					const auto narrow = static_cast<uint16_t>(index);
					appendBytes(mesh.indexData, &narrow, sizeof(narrow));
				}
			}
			else
			{
				submesh.indexType = IndexType::kUint32;
				appendBytes(mesh.indexData, indices.data(), indices.size() * sizeof(uint32_t));
			}

			buildMeshlets(mesh, submesh, indices);
			mesh.submeshes.push_back(submesh);
		}

		uint32_t
		addName(BMeshImport& mesh, const std::string& name)
		{
			if (name.empty())
				return 0;
			const auto offset = static_cast<uint32_t>(mesh.stringPool.size());
			mesh.stringPool.insert(mesh.stringPool.end(), name.begin(), name.end());
			mesh.stringPool.push_back('\0');
			return offset;
		}

		void
		buildNodes(BMeshImport& mesh, const tinygltf::Model& model)
		{
			mesh.nodes.resize(model.nodes.size());
			for (auto& node : mesh.nodes)
			{
				node.parent      = c_InvalidIndex;
				node.firstChild  = c_InvalidIndex;
				node.nextSibling = c_InvalidIndex;
				node.mesh        = c_InvalidIndex;
				node.nameOffset  = 0;
			}

			for (size_t i = 0; i < model.nodes.size(); ++i)
			{
				const auto& gltfNode         = model.nodes[i];
				mesh.nodes[i].localTransform = readNodeTransform(gltfNode);
				mesh.nodes[i].nameOffset     = addName(mesh, gltfNode.name);
				if (gltfNode.mesh >= 0)
					mesh.nodes[i].mesh = static_cast<uint32_t>(gltfNode.mesh);

				uint32_t previous = c_InvalidIndex;
				for (const int childIndex : gltfNode.children)
				{
					const auto child         = static_cast<uint32_t>(childIndex);
					mesh.nodes[child].parent = static_cast<uint32_t>(i);
					if (previous == c_InvalidIndex)
						mesh.nodes[i].firstChild = child;
					else
						mesh.nodes[previous].nextSibling = child;
					previous = child;
				}
			}

			for (size_t i = 0; i < mesh.nodes.size(); ++i)
				if (mesh.nodes[i].parent == c_InvalidIndex)
					mesh.roots.push_back(static_cast<uint32_t>(i));
		}

		// Fills imageToTexture so material parsing can map a glTF texture (-> image) to a
		// BMeshImport::textures index; skipped/unsupported images stay c_InvalidIndex.
		void
		buildTextures(
			BMeshImport&           mesh,
			const tinygltf::Model& model,
			std::vector<uint32_t>& imageToTexture,
			const CancelToken&     cancel)
		{
			imageToTexture.assign(model.images.size(), c_InvalidIndex);
			for (size_t i = 0; i < model.images.size(); ++i)
			{
				throwIfCancelled(cancel);

				const auto& image = model.images[i];
				if (image.image.empty() || image.width <= 0 || image.height <= 0 || image.bits != 8)
					continue;

				const size_t           width  = static_cast<size_t>(image.width);
				const size_t           height = static_cast<size_t>(image.height);
				const int              comp   = image.component;
				std::vector<std::byte> rgba(width * height * 4);
				for (size_t px = 0; px < width * height; ++px)
				{
					const uint8_t* src = image.image.data() + px * static_cast<size_t>(comp);
					uint8_t        r = 0, g = 0, b = 0, a = 255;
					if (comp >= 1)
						r = src[0];
					if (comp >= 2)
						g = src[1];
					if (comp >= 3)
						b = src[2];
					if (comp >= 4)
						a = src[3];
					if (comp == 1)
					{
						g = r;
						b = r;
					}
					rgba[px * 4 + 0] = std::byte{ r };
					rgba[px * 4 + 1] = std::byte{ g };
					rgba[px * 4 + 2] = std::byte{ b };
					rgba[px * 4 + 3] = std::byte{ a };
				}

				imageToTexture[i] = static_cast<uint32_t>(mesh.textures.size());
				mesh.textures.push_back(rgba8ToImage(
					rgba,
					static_cast<uint32_t>(width),
					static_cast<uint32_t>(height)));
			}
		}

		// Maps a glTF texture index (-> its source image -> BMeshImport::textures) to a BMeshImport
		// texture index, or c_InvalidIndex.
		uint32_t
		mapTexture(
			const tinygltf::Model&       model,
			int                          gltfTextureIndex,
			const std::vector<uint32_t>& imageToTexture)
		{
			if (gltfTextureIndex < 0 ||
			    static_cast<size_t>(gltfTextureIndex) >= model.textures.size())
				return c_InvalidIndex;

			const int source = model.textures[static_cast<size_t>(gltfTextureIndex)].source;
			if (source < 0 || static_cast<size_t>(source) >= imageToTexture.size())
				return c_InvalidIndex;

			return imageToTexture[static_cast<size_t>(source)];
		}

		// glTF's shading model is metallic-roughness unless the material overrides it, so PBR-ness is
		// decided by the absence of an extension rather than the presence of pbrMetallicRoughness --
		// which tinygltf default-constructs whether or not the file declares it.
		bool
		isPbrMaterial(const tinygltf::Material& material)
		{
			return !material.extensions.contains("KHR_materials_unlit") &&
			       !material.extensions.contains("KHR_materials_pbrSpecularGlossiness");
		}

		AlphaMode
		toAlphaMode(const std::string& gltfAlphaMode)
		{
			if (gltfAlphaMode == "MASK")
				return AlphaMode::kMask;
			if (gltfAlphaMode == "BLEND")
				return AlphaMode::kBlend;
			return AlphaMode::kOpaque;
		}

		void
		buildMaterials(
			BMeshImport&                 mesh,
			const tinygltf::Model&       model,
			const std::vector<uint32_t>& imageToTexture)
		{
			mesh.materials.reserve(model.materials.size());
			for (const auto& gltfMat : model.materials)
			{
				const auto& pbr = gltfMat.pbrMetallicRoughness;

				BMaterialImport material{};
				material.isPbr       = isPbrMaterial(gltfMat);
				material.alphaMode   = toAlphaMode(gltfMat.alphaMode);
				material.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
				material.baseColorTexture =
					mapTexture(model, pbr.baseColorTexture.index, imageToTexture);
				material.ormTexture =
					mapTexture(model, pbr.metallicRoughnessTexture.index, imageToTexture);
				material.normalTexture =
					mapTexture(model, gltfMat.normalTexture.index, imageToTexture);

				if (pbr.baseColorFactor.size() == 4)
					material.baseColorFactor = glm::vec4(
						static_cast<float>(pbr.baseColorFactor[0]),
						static_cast<float>(pbr.baseColorFactor[1]),
						static_cast<float>(pbr.baseColorFactor[2]),
						static_cast<float>(pbr.baseColorFactor[3]));
				material.metallicFactor  = static_cast<float>(pbr.metallicFactor);
				material.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
				material.nameOffset      = addName(mesh, gltfMat.name);

				mesh.materials.push_back(material);
			}
		}
	}

	namespace
	{
		void
		loadModel(
			tinygltf::TinyGLTF&          loader,
			tinygltf::Model&             model,
			const std::filesystem::path& path)
		{
			std::string error;
			std::string warning;

			auto extension = path.extension().string();
			std::ranges::transform(extension, extension.begin(), [](char c) {
				return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
			});

			const bool ok = extension == ".glb" ?
			                    loader.LoadBinaryFromFile(&model, &error, &warning, path.string()) :
			                    loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
			if (!ok)
				throw std::runtime_error(
					"bmesh: failed to load glTF '" + path.string() + "': " + error);
		}
	}

	GltfMaterialProbe
	probeGltfMaterials(const std::filesystem::path& path)
	{
		tinygltf::TinyGLTF loader;
		tinygltf::Model    model;

		// Decoding every texture to answer a question about the material table is the whole cost of an
		// import; the stub keeps the parse to the glTF's JSON and buffers.
		loader.SetImageLoader(
			[](tinygltf::Image*,
		       const int,
		       std::string*,
		       std::string*,
		       int,
		       int,
		       const unsigned char*,
		       int,
		       void*) { return true; },
			nullptr);

		loadModel(loader, model, path);

		GltfMaterialProbe probe{};
		probe.materialCount = model.materials.size();
		probe.pbrMaterialCount =
			static_cast<size_t>(std::ranges::count_if(model.materials, isPbrMaterial));
		return probe;
	}

	BMeshImport
	loadFromGltf(const std::filesystem::path& path, const CancelToken& cancel, float sampleRate)
	{
		tinygltf::TinyGLTF loader;
		tinygltf::Model    model;

		loadModel(loader, model, path);

		BMeshImport mesh;
		mesh.stringPool.push_back('\0');  // offset 0 == empty string

		buildNodes(mesh, model);

		// Before the submeshes: their JOINTS_0 indices are the skin's joint order, and what they must
		// come out as is the skeleton's bone order.
		const SkinImport skin = importSkin(model);
		mesh.skeleton         = skin.skeleton;
		mesh.animations       = importAnimations(model, skin, sampleRate);

		for (const auto& gltfMesh : model.meshes)
		{
			throwIfCancelled(cancel);

			Mesh entry{};
			entry.firstSubmesh = static_cast<uint32_t>(mesh.submeshes.size());
			entry.nameOffset   = addName(mesh, gltfMesh.name);
			for (size_t p = 0; p < gltfMesh.primitives.size(); ++p)
			{
				const auto& primitive = gltfMesh.primitives[p];
				if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
					throw std::runtime_error("bmesh: only triangle primitives are supported");

				const size_t before = mesh.submeshes.size();
				buildSubmesh(mesh, model, primitive);
				if (mesh.submeshes.size() == before)
					continue;  // primitive was skipped (e.g. no positions)

				std::string submeshName = gltfMesh.name;
				if (gltfMesh.primitives.size() > 1)
					submeshName += "[" + std::to_string(p) + "]";
				mesh.submeshes.back().nameOffset = addName(mesh, submeshName);
			}
			entry.submeshCount = static_cast<uint32_t>(mesh.submeshes.size()) - entry.firstSubmesh;
			mesh.meshes.push_back(entry);
		}

		std::vector<uint32_t> imageToTexture;
		buildTextures(mesh, model, imageToTexture, cancel);
		buildMaterials(mesh, model, imageToTexture);
		return mesh;
	}
}
