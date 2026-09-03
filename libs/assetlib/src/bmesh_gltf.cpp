#include <assetlib/bmesh_gltf.h>
#include <assetlib_structs/BMeshImport.h>

#include "bmesh_texture.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

// stb_image keeps its last error in one global `stbi__g_failure_reason`, and Reimport now parses
// several sources at once -- so without this a failed decode can report the message another
// thread's failure left behind. The decode itself is re-entrant; only the diagnostic is shared.
#define STBI_THREAD_LOCAL thread_local

#include <tiny_gltf.h>

// tiny_gltf.h and stb_image.h both put their implementation *outside* their include guard, so any
// later include of either -- which the headers below make -- would emit a second copy of it.
#undef TINYGLTF_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION

#include <tracy/Tracy.hpp>

#include "gltf_skin.h"
#include "gltf_util.h"

#include <core/err/util.h>
#include <core/type_traits.h>

#include <meshoptimizer.h>

#include <spdlog/spdlog.h>

namespace assetlib
{
	using namespace imp;
	using core::throw_runtime_error;

	namespace
	{
		constexpr size_t c_MeshletMaxVertices  = 64;
		constexpr size_t c_MeshletMaxTriangles = 124;
		constexpr float  c_MeshletConeWeight   = 0.0f;

		/**
		 * A strided view over one glTF vertex attribute. Component type is carried, so the same view
		 * serves the float geometry attributes (read whole through `At`) and the integer skin
		 * attributes JOINTS_0 / WEIGHTS_0 (decoded per component through `UintAt` / `FloatAt`).
		 */
		struct AttributeView
		{
			const std::byte* base          = nullptr;
			size_t           stride        = 0;
			int              components    = 0;
			int              componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
			bool             normalized    = false;

			[[nodiscard]] bool
			Present() const noexcept
			{
				return base != nullptr;
			}

			/**
			 * Attribute `index` read whole as a `T` -- e.g. a float `POSITION` as `glm::vec3`. `memcpy`
			 * rather than a reinterpreting reference, because the interleaved buffer holds no `T`
			 * object to alias and its offsets carry no alignment guarantee. Meaningful only where the
			 * bytes already are a `T`; the integer skin attributes need `UintAt` / `FloatAt`, which
			 * convert.
			 */
			template <core::type_traits::trivially_copyable T>
			[[nodiscard]] T
			At(size_t index) const noexcept
			{
				T value;
				std::memcpy(&value, base + index * stride, sizeof(T));
				return value;
			}

			[[nodiscard]] const std::byte*
			ComponentAt(size_t index, int component) const noexcept
			{
				const auto size = static_cast<size_t>(
					tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(componentType)));
				return base + index * stride + static_cast<size_t>(component) * size;
			}

			[[nodiscard]] uint32_t
			UintAt(size_t index, int component) const noexcept
			{
				const std::byte* ptr = ComponentAt(index, component);
				switch (componentType)
				{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					return std::to_integer<uint8_t>(*ptr);
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				{
					uint16_t value = 0;
					std::memcpy(&value, ptr, sizeof(value));
					return value;
				}
				default:
				{
					uint32_t value = 0;
					std::memcpy(&value, ptr, sizeof(value));
					return value;
				}
				}
			}

			[[nodiscard]] float
			FloatAt(size_t index, int component) const noexcept
			{
				if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
				{
					float value = 0.0f;
					std::memcpy(&value, ComponentAt(index, component), sizeof(value));
					return value;
				}

				const auto raw = static_cast<float>(UintAt(index, component));
				if (!normalized)
					return raw;

				return componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? raw / 255.0f :
				                                                                raw / 65535.0f;
			}
		};

		/** glTF's stride rule: an explicit byteStride, or tightly packed when the view has none. */
		size_t
		effectiveStride(const tinygltf::BufferView& view, int components, size_t componentSize)
		{
			return view.byteStride != 0 ? view.byteStride :
			                              static_cast<size_t>(components) * componentSize;
		}

		/**
		 * @param allowInteger When false, a non-float attribute throws -- the geometry attributes must
		 *        be float, and reading an integer POSITION as floats would silently produce garbage.
		 *        The skin attributes pass true.
		 */
		AttributeView
		makeView(
			const tinygltf::Model&     model,
			const tinygltf::Primitive& primitive,
			const char*                semantic,
			bool                       allowInteger = false)
		{
			const auto it = primitive.attributes.find(semantic);
			if (it == primitive.attributes.end())
				return {};

			const auto& accessor = model.accessors[static_cast<size_t>(it->second)];
			if (accessor.sparse.isSparse)
				throw std::runtime_error("bmesh: sparse accessors are not supported");
			if (!allowInteger && accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
				throw std::runtime_error("bmesh: only float vertex attributes are supported");
			if (accessor.bufferView < 0)
				return {};

			const auto& view   = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
			const auto& buffer = model.buffers[static_cast<size_t>(view.buffer)];
			const int   components =
				tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
			const auto componentSize = static_cast<size_t>(
				tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType)));

			AttributeView out;
			out.base   = reinterpret_cast<const std::byte*>(buffer.data.data()) + view.byteOffset +
			             accessor.byteOffset;
			out.stride = effectiveStride(view, components, componentSize);
			out.components    = components;
			out.componentType = accessor.componentType;
			out.normalized    = accessor.normalized;
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

		/** The typed form: `count` elements of a pool from `first`, no byte arithmetic in sight. */
		template <typename T>
		void
		appendValues(std::vector<std::byte>& dst, const T* first, size_t count)
		{
			appendBytes(dst, first, count * sizeof(T));
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

		/**
		 * A rigid mesh hanging off a joint: the bone it rides, and the transform taking its vertices
		 * into the space the rig poses in.
		 *
		 * Such a mesh is bound to that bone at full weight, which is what it already means -- so it
		 * draws through the skinned path and the runtime needs no notion of parenting. See
		 * docs/skinning.md.
		 */
		struct Attachment
		{
			uint32_t  bone    = c_InvalidIndex;
			glm::mat4 toModel = glm::mat4(1.0f);

			[[nodiscard]] bool
			IsAttached() const noexcept
			{
				return bone != c_InvalidIndex;
			}
		};

		/**
		 * Which mesh entries are attachments, indexed by glTF mesh.
		 *
		 * A mesh qualifies when exactly one node references it, that node carries no skin of its own,
		 * and an ancestor of it is a joint. The single-reference rule is the limit: a mesh instanced
		 * by two nodes would need one baked transform per node, and it keeps today's behaviour --
		 * drawn once, at its bind pose.
		 */
		std::vector<Attachment>
		planAttachments(const tinygltf::Model& model, const SkinImport& skin)
		{
			std::vector<Attachment> attachments(model.meshes.size());
			if (skin.skeleton.bones.empty())
				return attachments;

			std::vector<uint32_t> referrer(model.meshes.size(), c_InvalidIndex);
			std::vector<bool>     shared(model.meshes.size(), false);

			for (size_t node = 0; node < model.nodes.size(); ++node)
			{
				const int meshIndex = model.nodes[node].mesh;
				if (meshIndex < 0 || static_cast<size_t>(meshIndex) >= model.meshes.size())
					continue;

				const auto entry = static_cast<size_t>(meshIndex);
				if (referrer[entry] != c_InvalidIndex)
					shared[entry] = true;
				else
					referrer[entry] = static_cast<uint32_t>(node);
			}

			const auto nodeParents = buildNodeParents(model);

			for (size_t entry = 0; entry < model.meshes.size(); ++entry)
			{
				if (shared[entry] || referrer[entry] == c_InvalidIndex)
					continue;

				const uint32_t node = referrer[entry];
				if (model.nodes[node].skin >= 0)
					continue;

				uint32_t ancestor = nodeParents[node];
				while (ancestor != c_InvalidIndex && skin.nodeToBone[ancestor] == c_InvalidIndex)
					ancestor = nodeParents[ancestor];

				if (ancestor == c_InvalidIndex)
					continue;

				// Every node from the scene root down, which is the space a bone's model transform
				// lands in -- the root bone's bind pose already carries the chain above it.
				glm::mat4 toModel(1.0f);
				for (uint32_t cur = node; cur != c_InvalidIndex; cur = nodeParents[cur])
					toModel = toMatrix(readNodeTransform(model.nodes[cur])) * toModel;

				attachments[entry] = { skin.nodeToBone[ancestor], toModel };
			}

			return attachments;
		}

		/** Bone `bone` at full weight for every vertex, which is what a rigid parenting is. */
		void
		bindWholly(
			uint32_t               bone,
			size_t                 vertexCount,
			std::vector<uint16_t>& joints,
			std::vector<uint16_t>& weights)
		{
			joints.assign(vertexCount * c_InfluencesPerVertex, 0);
			weights.assign(vertexCount * c_InfluencesPerVertex, 0);

			for (size_t i = 0; i < vertexCount; ++i)
			{
				joints[i * c_InfluencesPerVertex]  = static_cast<uint16_t>(bone);
				weights[i * c_InfluencesPerVertex] = std::numeric_limits<uint16_t>::max();
			}
		}

		/**
		 * A primitive's skin binding, remapped into bone order and quantized: four `uint16` bone
		 * indices and four `unorm16` weights per vertex, or nothing at all.
		 *
		 * Both attributes or neither: a joint index with no weight skins nothing, and a weight with
		 * no joint has nothing to skin to.
		 */
		void
		readSkinAttributes(
			const tinygltf::Model&     model,
			const tinygltf::Primitive& primitive,
			size_t                     vertexCount,
			std::span<const uint32_t>  jointToBone,
			std::vector<uint16_t>&     joints,
			std::vector<uint16_t>&     weights)
		{
			const AttributeView jointView  = makeView(model, primitive, "JOINTS_0", true);
			const AttributeView weightView = makeView(model, primitive, "WEIGHTS_0", true);

			if (jointToBone.empty() || !jointView.Present() || !weightView.Present())
				return;

			constexpr auto c_MaxJointIndex = std::numeric_limits<uint16_t>::max();

			if (jointToBone.size() > c_MaxJointIndex)
				throw_runtime_error(
					"bmesh: a skin of {} joints exceeds the {} a uint16 joint index can name",
					jointToBone.size(),
					c_MaxJointIndex);

			joints.assign(vertexCount * c_InfluencesPerVertex, 0);
			weights.assign(vertexCount * c_InfluencesPerVertex, 0);

			const auto weightComponents = static_cast<size_t>(weightView.components);
			const auto jointComponents  = static_cast<size_t>(jointView.components);

			for (size_t i = 0; i < vertexCount; ++i)
			{
				std::array<float, c_InfluencesPerVertex> weight{};
				float                                    sum = 0.0f;
				for (size_t c = 0; c < c_InfluencesPerVertex && c < weightComponents; ++c)
				{
					weight[c] = weightView.FloatAt(i, static_cast<int>(c));
					sum += weight[c];
				}

				for (size_t c = 0; c < c_InfluencesPerVertex; ++c)
				{
					const uint32_t joint =
						c < jointComponents ? jointView.UintAt(i, static_cast<int>(c)) : 0u;
					if (joint >= jointToBone.size())
						throw_runtime_error(
							"bmesh: a vertex names joint {}, which the skin does not have",
							joint);

					joints[i * c_InfluencesPerVertex + c] =
						static_cast<uint16_t>(jointToBone[joint]);

					// Renormalized before quantizing: glTF requires the four to sum to 1 and exporters
					// drift, which a unorm16 round-trip would then compound.
					const float share                      = sum > 0.0f ? weight[c] / sum : 0.0f;
					weights[i * c_InfluencesPerVertex + c] = static_cast<uint16_t>(std::lround(
						std::clamp(share, 0.0f, 1.0f) * std::numeric_limits<uint16_t>::max()));
				}
			}
		}

		void
		buildSubmesh(
			BMeshImport&               mesh,
			const tinygltf::Model&     model,
			const tinygltf::Primitive& primitive,
			std::span<const uint32_t>  jointToBone,
			const Attachment&          attachment)
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

			std::vector<uint16_t> joints;
			std::vector<uint16_t> weights;
			readSkinAttributes(model, primitive, vertexCount, jointToBone, joints, weights);

			// A primitive that already carries a binding keeps it; planAttachments only ever names a
			// node with no skin, so the two cannot both be meant.
			if (attachment.IsAttached() && joints.empty())
				bindWholly(attachment.bone, vertexCount, joints, weights);

			// Positions come out in bone space rather than the node's, which is what full weight on
			// that bone then undoes -- so the node's own transform must not be applied again.
			const auto  normalMatrix = glm::transpose(glm::inverse(glm::mat3(attachment.toModel)));
			const float handedness =
				glm::determinant(glm::mat3(attachment.toModel)) < 0.0f ? -1.0f : 1.0f;

			// Build the interleaved layout from the present attributes only.
			Submesh  submesh{};
			uint16_t offset = 0;
			for (const auto& attr : candidates)
			{
				if (!attr.view.Present())
					continue;
				submesh.layout.attributes[submesh.layout.attributeCount++] = { attr.semantic,
					                                                           attr.format,
					                                                           offset };
				offset += static_cast<uint16_t>(formatSize(attr.format));
			}

			if (!joints.empty())
			{
				submesh.layout.attributes[submesh.layout.attributeCount++] = {
					VertexSemantic::kJoints0,
					VertexFormat::kUint16x4,
					offset
				};
				offset += static_cast<uint16_t>(formatSize(VertexFormat::kUint16x4));

				submesh.layout.attributes[submesh.layout.attributeCount++] = {
					VertexSemantic::kWeights0,
					VertexFormat::kUnorm16x4,
					offset
				};
				offset += static_cast<uint16_t>(formatSize(VertexFormat::kUnorm16x4));
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
					if (!attr.view.Present())
						continue;

					if (attachment.IsAttached())
					{
						if (attr.semantic == VertexSemantic::kPosition)
						{
							const auto p = glm::vec3(
								attachment.toModel * glm::vec4(attr.view.At<glm::vec3>(i), 1.0f));
							appendBytes(mesh.vertexData, &p, sizeof(p));
							continue;
						}
						if (attr.semantic == VertexSemantic::kNormal)
						{
							const auto n =
								glm::normalize(normalMatrix * attr.view.At<glm::vec3>(i));
							appendBytes(mesh.vertexData, &n, sizeof(n));
							continue;
						}
						if (attr.semantic == VertexSemantic::kTangent)
						{
							const auto source = attr.view.At<glm::vec4>(i);
							const auto t      = glm::vec4(
								glm::normalize(glm::mat3(attachment.toModel) * glm::vec3(source)),
								source.w * handedness);
							appendBytes(mesh.vertexData, &t, sizeof(t));
							continue;
						}
					}

					appendBytes(
						mesh.vertexData,
						attr.view.ComponentAt(i, 0),
						static_cast<size_t>(attr.components) * sizeof(float));
				}

				if (!joints.empty())
				{
					appendValues(
						mesh.vertexData,
						joints.data() + i * c_InfluencesPerVertex,
						c_InfluencesPerVertex);
					appendValues(
						mesh.vertexData,
						weights.data() + i * c_InfluencesPerVertex,
						c_InfluencesPerVertex);
				}
			}

			// Prefer the accessor's declared bounds; otherwise compute from positions. An
			// attachment's are in the node's space, which the vertices above have already left.
			glm::vec3 aabbMin(std::numeric_limits<float>::max());
			glm::vec3 aabbMax(std::numeric_limits<float>::lowest());
			if (!attachment.IsAttached() && posAccessor.minValues.size() == 3 &&
			    posAccessor.maxValues.size() == 3)
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
					auto p = candidates[0].view.At<glm::vec3>(i);
					if (attachment.IsAttached())
						p = glm::vec3(attachment.toModel * glm::vec4(p, 1.0f));

					aabbMin = glm::min(aabbMin, p);
					aabbMax = glm::max(aabbMax, p);
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
				mesh.nodes[i].nameOffset     = mesh.stringPool.add(gltfNode.name);
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

				// The URI stands in for an absent name: a glTF referencing image files names them
				// there and nowhere else.
				mesh.textureNames.push_back(
					!image.name.empty() ? image.name :
										  std::filesystem::path(image.uri).stem().string());
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

		constexpr std::string_view c_SpecGlossExtension = "KHR_materials_pbrSpecularGlossiness";

		// glTF's shading model is metallic-roughness unless the material overrides it, so PBR-ness is
		// decided by the absence of an extension rather than the presence of pbrMetallicRoughness --
		// which tinygltf default-constructs whether or not the file declares it.
		//
		// Specular-glossiness is converted rather than refused (see readSpecularGlossiness), so it is
		// not disqualifying; unlit is, because it names a shading model the engine does not have.
		bool
		isPbrMaterial(const tinygltf::Material& material)
		{
			return !material.extensions.contains("KHR_materials_unlit");
		}

		/**
		 * KHR_materials_transmission's `transmissionFactor`, or 0 when the extension is absent --
		 * glTF's own default, and the coverage reading BLEND has always had here.
		 *
		 * Transmission is what separates a lens from a hair card, both of which export as BLEND.
		 * Without this the engine has no signal to tell them apart and reads every blended material
		 * as coverage, which costs a lens its reflection.
		 */
		float
		toTransmission(const tinygltf::Material& material)
		{
			const auto it = material.extensions.find("KHR_materials_transmission");
			if (it == material.extensions.end() || !it->second.Has("transmissionFactor"))
				return 0.0f;

			const tinygltf::Value& factor = it->second.Get("transmissionFactor");
			if (!factor.IsNumber())
				return 0.0f;

			return std::clamp(static_cast<float>(factor.GetNumberAsDouble()), 0.0f, 1.0f);
		}

		/**
		 * KHR_materials_specular's `specularColorFactor` and `specularFactor`, or glTF's own defaults
		 * -- white and 1 -- when the extension or a field of it is absent.
		 *
		 * The extension is the only thing in glTF that can say a surface has *no* specular, which is
		 * what a Phong export with its specular switched off means. Without it every such material
		 * arrives at the flat 0.04 dielectric and wears a sheen its author removed.
		 */
		void
		readSpecular(const tinygltf::Material& material, BMaterialImport& out)
		{
			const auto it = material.extensions.find("KHR_materials_specular");
			if (it == material.extensions.end())
				return;

			if (it->second.Has("specularFactor"))
			{
				const tinygltf::Value& factor = it->second.Get("specularFactor");
				if (factor.IsNumber())
					out.specularFactor =
						std::clamp(static_cast<float>(factor.GetNumberAsDouble()), 0.0f, 1.0f);
			}

			if (!it->second.Has("specularColorFactor"))
				return;

			const tinygltf::Value& color = it->second.Get("specularColorFactor");
			if (!color.IsArray() || color.ArrayLen() != static_cast<size_t>(glm::vec3::length()))
				return;

			for (glm::length_t i = 0; i < glm::vec3::length(); ++i)
			{
				const tinygltf::Value& component = color.Get(static_cast<size_t>(i));
				if (component.IsNumber())
					out.specularColorFactor[i] =
						std::max(static_cast<float>(component.GetNumberAsDouble()), 0.0f);
			}
		}

		/**
		 * The metallic a diffuse/specular pair implies, by Khronos' own specular-glossiness
		 * conversion -- the positive root of the quadratic that makes a metallic-roughness surface
		 * reflect what the authored pair did.
		 *
		 * @return 0 for anything at or below the 0.04 dielectric, which is every non-metal.
		 */
		float
		solveMetallic(float diffuse, float specular, float oneMinusSpecularStrength) noexcept
		{
			constexpr auto c_DielectricSpecular = 0.04f;
			if (specular <= c_DielectricSpecular)
				return 0.0f;

			const auto a = c_DielectricSpecular;
			const auto b = diffuse * oneMinusSpecularStrength / (1.0f - c_DielectricSpecular) +
			               specular - 2.0f * c_DielectricSpecular;
			const auto c = c_DielectricSpecular - specular;
			const auto discriminant = std::max(b * b - 4.0f * a * c, 0.0f);

			return std::clamp((-b + std::sqrt(discriminant)) / (2.0f * a), 0.0f, 1.0f);
		}

		/** glTF's perceived-brightness weighting, which the conversion above solves against. */
		float
		perceivedBrightness(const glm::vec3& colour) noexcept
		{
			return std::sqrt(
				0.299f * colour.r * colour.r + 0.587f * colour.g * colour.g +
				0.114f * colour.b * colour.b);
		}

		glm::vec3
		readVec3(const tinygltf::Value& ext, const char* key, const glm::vec3& fallback)
		{
			if (!ext.Has(key))
				return fallback;

			const tinygltf::Value& value = ext.Get(key);
			if (!value.IsArray() || value.ArrayLen() < static_cast<size_t>(glm::vec3::length()))
				return fallback;

			glm::vec3 out = fallback;
			for (glm::length_t i = 0; i < glm::vec3::length(); ++i)
			{
				const tinygltf::Value& component = value.Get(static_cast<size_t>(i));
				if (component.IsNumber())
					out[i] = static_cast<float>(component.GetNumberAsDouble());
			}
			return out;
		}

		/**
		 * Converts KHR_materials_pbrSpecularGlossiness onto `out`'s metallic-roughness fields, by the
		 * conversion Khronos publishes with the extension and every other importer implements
		 * (glTF-Transform's `metalRough`, Blender's importer, three.js).
		 *
		 * The extension is archived -- superseded by metallic-roughness plus KHR_materials_specular --
		 * but Sketchfab exported it for years, so refusing it means refusing a large share of the
		 * models anyone actually has. Converting is what the rest of the ecosystem does; the
		 * alternative here was reading the material as PBR anyway, which silently takes tinygltf's
		 * default-constructed pbrMetallicRoughness and imports every such surface as white metal.
		 *
		 * **Factors only, and the texture is not composited.** `specularGlossinessTexture` carries
		 * per-texel specular in RGB and glossiness in A, and the engine's ORM wants roughness in G --
		 * an inversion no ChannelRoute can express, so it would have to be baked into a new image at
		 * import. A material carrying one therefore gets a constant roughness from `glossinessFactor`
		 * rather than a varying one; its base colour and normal are unaffected.
		 *
		 * @return false when the material does not declare the extension, leaving `out` untouched.
		 */
		bool
		readSpecularGlossiness(
			const tinygltf::Material&    material,
			const tinygltf::Model&       model,
			const std::vector<uint32_t>& imageToTexture,
			BMaterialImport&             out)
		{
			const auto it = material.extensions.find(std::string(c_SpecGlossExtension));
			if (it == material.extensions.end())
				return false;

			const tinygltf::Value& ext = it->second;

			auto diffuse    = glm::vec4(1.0f);
			auto glossiness = 1.0f;

			if (ext.Has("diffuseFactor"))
			{
				const tinygltf::Value& value = ext.Get("diffuseFactor");
				if (value.IsArray() && value.ArrayLen() >= static_cast<size_t>(glm::vec4::length()))
					for (glm::length_t i = 0; i < glm::vec4::length(); ++i)
					{
						const tinygltf::Value& component = value.Get(static_cast<size_t>(i));
						if (component.IsNumber())
							diffuse[i] = static_cast<float>(component.GetNumberAsDouble());
					}
			}

			if (ext.Has("glossinessFactor"))
			{
				const tinygltf::Value& value = ext.Get("glossinessFactor");
				if (value.IsNumber())
					glossiness =
						std::clamp(static_cast<float>(value.GetNumberAsDouble()), 0.0f, 1.0f);
			}

			const glm::vec3 specular = readVec3(ext, "specularFactor", glm::vec3(1.0f));

			const auto oneMinusSpecularStrength =
				1.0f - std::max({ specular.r, specular.g, specular.b });
			const auto metallic = solveMetallic(
				perceivedBrightness(glm::vec3(diffuse)),
				perceivedBrightness(specular),
				oneMinusSpecularStrength);

			// The base colour both halves agree on: the diffuse lobe as a non-metal sees it, blended
			// with the specular colour a metal reflects, by the metallic just solved.
			constexpr auto  c_DielectricSpecular = 0.04f;
			const glm::vec3 fromDiffuse =
				glm::vec3(diffuse) * (oneMinusSpecularStrength / (1.0f - c_DielectricSpecular) /
			                          std::max(1.0f - metallic, 1e-4f));
			const glm::vec3 fromSpecular =
				(specular - glm::vec3(c_DielectricSpecular) * (1.0f - metallic)) *
				(1.0f / std::max(metallic, 1e-4f));

			const glm::vec3 baseColor =
				glm::clamp(glm::mix(fromDiffuse, fromSpecular, metallic * metallic), 0.0f, 1.0f);

			out.baseColorFactor = glm::vec4(baseColor, diffuse.a);
			out.metallicFactor  = metallic;
			out.roughnessFactor = 1.0f - glossiness;

			if (ext.Has("diffuseTexture"))
			{
				const tinygltf::Value& texture = ext.Get("diffuseTexture");
				if (texture.Has("index"))
					out.baseColorTexture =
						mapTexture(model, texture.Get("index").GetNumberAsInt(), imageToTexture);
			}

			return true;
		}

		/**
		 * The occlusion map a glTF material names, mapped into imp::BMeshImport::textures.
		 *
		 * @return c_InvalidIndex when the material names none, or when it names one this importer
		 *         cannot honour: a texCoord other than 0 is refused rather than sampled through
		 *         TEXCOORD_0, which is the only set read (see readVertices) and the wrong
		 *         parameterisation for a map baked against another.
		 */
		uint32_t
		readOcclusion(
			const tinygltf::Material&    gltfMat,
			const tinygltf::Model&       model,
			const std::vector<uint32_t>& imageToTexture)
		{
			const tinygltf::OcclusionTextureInfo& occlusion = gltfMat.occlusionTexture;
			if (occlusion.index < 0)
				return c_InvalidIndex;

			if (occlusion.texCoord != 0)
			{
				spdlog::warn(
					"material '{}': occlusion map dropped, it is addressed by TEXCOORD_{} and only "
					"TEXCOORD_0 is read",
					gltfMat.name,
					occlusion.texCoord);
				return c_InvalidIndex;
			}

			if (occlusion.strength != 1.0)
				spdlog::warn(
					"material '{}': occlusion strength {} ignored, the engine has no parameter for "
					"it",
					gltfMat.name,
					occlusion.strength);

			return mapTexture(model, occlusion.index, imageToTexture);
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
				material.isPbr              = isPbrMaterial(gltfMat);
				material.alphaMode          = toAlphaMode(gltfMat.alphaMode);
				material.alphaCutoff        = static_cast<float>(gltfMat.alphaCutoff);
				material.transmissionFactor = toTransmission(gltfMat);
				readSpecular(gltfMat, material);
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

				// Last, so it overwrites the metallic-roughness block above: tinygltf
				// default-constructs that whether or not the file declares it, and a
				// specular-glossiness material declares it never.
				readSpecularGlossiness(gltfMat, model, imageToTexture, material);

				// Outside that block on purpose: occlusionTexture is a sibling of
				// pbrMetallicRoughness, so a specular-glossiness material can carry one.
				material.occlusionTexture = readOcclusion(gltfMat, model, imageToTexture);

				material.nameOffset = mesh.stringPool.add(gltfMat.name);

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

	namespace
	{
		/** The parse alone: keeps tinygltf to the glTF's JSON and buffers, no image decode. */
		void
		skipImageDecode(tinygltf::TinyGLTF& loader)
		{
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
		}
	}

	std::vector<GltfMaterial>
	probeGltfMaterials(const std::filesystem::path& path)
	{
		tinygltf::TinyGLTF loader;
		tinygltf::Model    model;

		skipImageDecode(loader);

		loadModel(loader, model, path);

		auto probed = std::vector<GltfMaterial>();
		probed.reserve(model.materials.size());
		for (const auto& gltfMat : model.materials)
			probed.push_back({ .name = gltfMat.name, .isPbr = isPbrMaterial(gltfMat) });

		return probed;
	}

	BMeshImport
	loadFromGltf(const std::filesystem::path& path, const GltfLoadOptions& options)
	{
		// Read for the zone alone, so both are [[maybe_unused]]: with profiling off ZoneTextF
		// expands to nothing and -Werror would call them dead.
		[[maybe_unused]] std::error_code sizeError;
		[[maybe_unused]] const auto      sourceBytes = std::filesystem::file_size(path, sizeError);

		ZoneScopedN("assetlib glTF parse");
		ZoneTextF(
			"%s, %llu bytes",
			path.filename().string().c_str(),
			static_cast<unsigned long long>(sizeError ? 0 : sourceBytes));

		tinygltf::TinyGLTF loader;
		tinygltf::Model    model;

		if (options.textures == GltfTextures::kSkip)
			skipImageDecode(loader);

		loadModel(loader, model, path);

		BMeshImport mesh;
		buildNodes(mesh, model);

		// Before the submeshes: their JOINTS_0 indices are the skin's joint order, and what they must
		// come out as is the skeleton's bone order.
		const SkinImport skin = importSkin(model);
		mesh.skeleton         = skin.skeleton;
		mesh.animations       = importAnimations(model, skin, options.sampleRate);

		// Before the submeshes too: a rigid mesh parented to a joint is bound to it at import, and
		// that rewrites its vertices.
		const std::vector<Attachment> attachments = planAttachments(model, skin);

		for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
		{
			throwIfCancelled(options.cancel);

			const tinygltf::Mesh& gltfMesh = model.meshes[meshIndex];

			Mesh entry{};
			entry.firstSubmesh = static_cast<uint32_t>(mesh.submeshes.size());
			entry.nameOffset   = mesh.stringPool.add(gltfMesh.name);
			for (size_t p = 0; p < gltfMesh.primitives.size(); ++p)
			{
				const auto& primitive = gltfMesh.primitives[p];
				if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
					throw std::runtime_error("bmesh: only triangle primitives are supported");

				const size_t before = mesh.submeshes.size();
				buildSubmesh(mesh, model, primitive, skin.jointToBone, attachments[meshIndex]);
				if (mesh.submeshes.size() == before)
					continue;  // primitive was skipped (e.g. no positions)

				std::string submeshName = gltfMesh.name;
				if (gltfMesh.primitives.size() > 1)
					submeshName += "[" + std::to_string(p) + "]";
				mesh.submeshes.back().nameOffset = mesh.stringPool.add(submeshName);
			}
			entry.submeshCount = static_cast<uint32_t>(mesh.submeshes.size()) - entry.firstSubmesh;
			mesh.meshes.push_back(entry);
		}

		if (options.textures == GltfTextures::kDecode)
		{
			std::vector<uint32_t> imageToTexture;
			buildTextures(mesh, model, imageToTexture, options.cancel);
			buildMaterials(mesh, model, imageToTexture);
		}
		return mesh;
	}
}
