#include <assetlib/bmesh_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>

#include <core/err/util.h>
#include <core/hash.h>
#include <core/type_traits.h>

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		template <core::type_traits::trivially_copyable T>
		T
		readAt(std::span<const std::byte> bytes, size_t offset) noexcept
		{
			T value{};
			std::memcpy(&value, bytes.data() + offset, sizeof(T));
			return value;
		}

		/**
		 * The offset of `semantic`, having checked it is the format this decoder reads and that it
		 * fits inside a vertex. The span check below bounds whole vertices, not the attributes
		 * within one, so without this a layout claiming an attribute past its own stride reads off
		 * the end of the last vertex.
		 */
		std::optional<uint16_t>
		decodableOffset(const VertexLayout& layout, VertexSemantic semantic, VertexFormat expected)
		{
			const std::optional<uint16_t> offset = attributeOffset(layout, semantic);
			if (!offset)
				return std::nullopt;

			for (uint32_t i = 0; i < layout.attributeCount; ++i)
			{
				if (layout.attributes[i].semantic != semantic)
					continue;

				if (layout.attributes[i].format != expected)
					throw_runtime_error(
						"skinning: attribute {} is not the format this decodes",
						static_cast<int>(semantic));
			}

			if (static_cast<size_t>(*offset) + formatSize(expected) > layout.stride)
				throw_runtime_error(
					"skinning: attribute {} extends past the vertex stride",
					static_cast<int>(semantic));

			return offset;
		}

		/** Where a submesh's skinning inputs are, resolved and bounds-checked once. */
		struct SkinLayout
		{
			std::optional<uint16_t> position;
			std::optional<uint16_t> normal;
			std::optional<uint16_t> tangent;
			std::optional<uint16_t> joints;
			std::optional<uint16_t> weights;
			size_t                  stride = 0;
			size_t                  first  = 0;
		};

		SkinLayout
		resolveSkinLayout(const BMesh& mesh, const Submesh& submesh)
		{
			auto out     = SkinLayout();
			out.position = decodableOffset(
				submesh.layout,
				VertexSemantic::kPosition,
				VertexFormat::kFloat32x3);
			out.normal =
				decodableOffset(submesh.layout, VertexSemantic::kNormal, VertexFormat::kFloat32x3);
			out.tangent =
				decodableOffset(submesh.layout, VertexSemantic::kTangent, VertexFormat::kFloat32x4);
			out.joints =
				decodableOffset(submesh.layout, VertexSemantic::kJoints0, VertexFormat::kUint16x4);
			out.weights = decodableOffset(
				submesh.layout,
				VertexSemantic::kWeights0,
				VertexFormat::kUnorm16x4);

			if (!out.position)
				throw_runtime_error("skinning: a submesh with no position attribute");

			// One without the other is a layout nothing can act on: indices with no share, or shares
			// naming no bone.
			if (out.joints.has_value() != out.weights.has_value())
				throw_runtime_error(
					"skinning: a submesh carrying only one half of its skin attributes");

			out.stride        = submesh.layout.stride;
			out.first         = submesh.vertexByteOffset;
			const size_t span = static_cast<size_t>(submesh.vertexCount) * out.stride;

			if (out.stride == 0 || out.first + span > mesh.vertexData.size())
				throw_runtime_error(
					"skinning: a submesh whose {} vertices fall outside the mesh's vertex data",
					submesh.vertexCount);

			return out;
		}

		/** A vertex's bind position and its influences, decoded out of the interleaved blob. */
		struct SkinInfluences
		{
			glm::vec3                                   position;
			std::array<uint16_t, c_InfluencesPerVertex> joints{};
			std::array<float, c_InfluencesPerVertex>    weights{};
			bool                                        skinned = false;
		};

		/**
		 * The submesh's vertices decoded once. None of this changes between poses, so a walk over
		 * many frames that decoded per frame would pay for it every time.
		 *
		 * @throws std::runtime_error if a vertex names a bone the pose does not hold.
		 */
		std::vector<SkinInfluences>
		decodeInfluences(
			const BMesh&      mesh,
			const Submesh&    submesh,
			const SkinLayout& layout,
			size_t            boneCount)
		{
			auto out = std::vector<SkinInfluences>(submesh.vertexCount);

			for (uint32_t v = 0; v < submesh.vertexCount; ++v)
			{
				const size_t base = layout.first + static_cast<size_t>(v) * layout.stride;

				SkinInfluences& vertex = out[v];
				vertex.position = readAt<glm::vec3>(mesh.vertexData, base + *layout.position);

				if (!layout.joints)
					continue;

				for (size_t i = 0; i < c_InfluencesPerVertex; ++i)
				{
					const auto joint = readAt<uint16_t>(
						mesh.vertexData,
						base + *layout.joints + i * sizeof(uint16_t));
					const auto quantized = readAt<uint16_t>(
						mesh.vertexData,
						base + *layout.weights + i * sizeof(uint16_t));

					if (quantized != 0 && joint >= boneCount)
						throw_runtime_error(
							"skinning: a vertex names bone {}, and the pose holds {}",
							joint,
							boneCount);

					vertex.joints[i] = joint;
					vertex.weights[i] =
						static_cast<float>(quantized) / std::numeric_limits<uint16_t>::max();
					vertex.skinned |= vertex.weights[i] > 0.0f;
				}
			}

			return out;
		}

		/**
		 * One decoded vertex's skinned position. An unskinned one keeps its bind position rather than
		 * blending to the origin -- see skinSubmesh.
		 */
		glm::vec3
		skinnedPosition(const SkinInfluences& vertex, std::span<const glm::mat4> skinning) noexcept
		{
			if (!vertex.skinned)
				return vertex.position;

			glm::vec3 skinned(0.0f);
			for (size_t i = 0; i < c_InfluencesPerVertex; ++i)
			{
				if (vertex.weights[i] == 0.0f)
					continue;

				// decodeInfluences refused an out-of-range joint against the bone count; this holds
				// only while that is the same count the pose was built for.
				assert(vertex.joints[i] < skinning.size());

				skinned += vertex.weights[i] *
				           glm::vec3(skinning[vertex.joints[i]] * glm::vec4(vertex.position, 1.0f));
			}

			return skinned;
		}
	}

	std::vector<SkinnedVertex>
	skinSubmesh(const BMesh& mesh, const Submesh& submesh, std::span<const glm::mat4> skinning)
	{
		const SkinLayout layout = resolveSkinLayout(mesh, submesh);

		const auto& positionOffset = layout.position;
		const auto& normalOffset   = layout.normal;
		const auto& tangentOffset  = layout.tangent;
		const auto& jointsOffset   = layout.joints;
		const auto& weightsOffset  = layout.weights;

		const size_t stride = layout.stride;
		const size_t first  = layout.first;

		std::vector<SkinnedVertex> out(submesh.vertexCount);

		for (uint32_t v = 0; v < submesh.vertexCount; ++v)
		{
			const size_t base = first + static_cast<size_t>(v) * stride;

			const auto position = readAt<glm::vec3>(mesh.vertexData, base + *positionOffset);
			const auto normal   = normalOffset ?
			                          readAt<glm::vec3>(mesh.vertexData, base + *normalOffset) :
			                          glm::vec3(0.0f);
			const auto tangent  = tangentOffset ?
			                          readAt<glm::vec3>(mesh.vertexData, base + *tangentOffset) :
			                          glm::vec3(0.0f);

			if (!jointsOffset)
			{
				out[v] = { position, normal, tangent };
				continue;
			}

			glm::vec3 skinnedPosition(0.0f);
			glm::vec3 skinnedNormal(0.0f);
			glm::vec3 skinnedTangent(0.0f);
			float     total = 0.0f;

			for (size_t i = 0; i < c_InfluencesPerVertex; ++i)
			{
				const auto joint =
					readAt<uint16_t>(mesh.vertexData, base + *jointsOffset + i * sizeof(uint16_t));
				const auto quantized =
					readAt<uint16_t>(mesh.vertexData, base + *weightsOffset + i * sizeof(uint16_t));

				const float weight =
					static_cast<float>(quantized) / std::numeric_limits<uint16_t>::max();
				if (weight == 0.0f)
					continue;

				if (joint >= skinning.size())
					throw_runtime_error(
						"skinning: a vertex names bone {}, and the pose holds {}",
						joint,
						skinning.size());

				const glm::mat4& matrix = skinning[joint];
				skinnedPosition += weight * glm::vec3(matrix * glm::vec4(position, 1.0f));
				skinnedNormal += weight * glm::mat3(matrix) * normal;
				skinnedTangent += weight * glm::mat3(matrix) * tangent;
				total += weight;
			}

			// An exporter writes (0,0,0,0) for a vertex it never assigned, and the importer
			// renormalizes that to four zero weights rather than refusing the mesh. Blending them
			// would put the vertex at the origin -- which, once the bake fits one AABB around every
			// clip, drags that box out and costs precision on every other vertex of the rig.
			out[v] = total > 0.0f ?
			             SkinnedVertex{ skinnedPosition, skinnedNormal, skinnedTangent } :
			             SkinnedVertex{ position, normal, tangent };
		}

		return out;
	}

	Bounds
	posedBounds(
		const BMesh&        mesh,
		const uint32_t      meshIndex,
		const Skeleton&     skeleton,
		const AnimationSet& animations)
	{
		core::throw_runtime_error_if(
			meshIndex >= mesh.meshes.size(),
			"posedBounds: mesh index {} out of range",
			meshIndex);

		const Mesh& entry = mesh.meshes[meshIndex];

		auto out = Bounds{ glm::vec3(std::numeric_limits<float>::max()),
			               glm::vec3(std::numeric_limits<float>::lowest()) };

		const auto grow = [&](const glm::vec3& p) {
			out.min = glm::min(out.min, p);
			out.max = glm::max(out.max, p);
		};

		// Only where there are frames to walk: a rig with no clips is bounded by its bind pose below,
		// and decoding here would refuse a malformed layout it used to reach that fallback with.
		auto submeshes = std::vector<std::vector<SkinInfluences>>();
		if (!animations.clips.empty())
		{
			submeshes.reserve(entry.submeshCount);
			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];
				submeshes.push_back(decodeInfluences(
					mesh,
					submesh,
					resolveSkinLayout(mesh, submesh),
					skeleton.bones.size()));
			}
		}

		for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
		{
			for (uint32_t frame = 0; frame < animations.clips[clip].frameCount; ++frame)
			{
				const std::vector<glm::mat4> skinning = skinningMatrices(
					skeleton,
					poseModelTransforms(skeleton, animations, clip, frame));

				for (const std::vector<SkinInfluences>& vertices : submeshes)
				{
					for (const SkinInfluences& vertex : vertices)
						grow(skinnedPosition(vertex, skinning));
				}
			}
		}

		// A rig with no clips has only its bind pose to be bounded by.
		if (glm::any(glm::greaterThan(out.min, out.max)))
		{
			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];
				grow(submesh.aabbMin);
				grow(submesh.aabbMax);
			}
		}

		core::throw_runtime_error_if(
			glm::any(glm::greaterThan(out.min, out.max)),
			"posedBounds: mesh {} has no submesh to bound",
			meshIndex);

		return out;
	}

	uint64_t
	posedBoundsSignature(const BMesh& mesh, const Skeleton& skeleton) noexcept
	{
		uint64_t hash =
			core::hash_bytes(mesh.vertexData.data(), mesh.vertexData.size(), core::hash_seed());

		// The tables that say which of those bytes a mesh index means: without them, a re-export
		// that regroups entries over identical bytes would keep matching a box that no longer
		// holds. Materials are left out -- swapping one does not move a vertex.
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

		for (const Bone& bone : skeleton.bones) hash = core::hash_pod(bone.inverseBind, hash);
		return hash;
	}

	void
	bakePosedBounds(AnimationSet& animations, const BMesh& mesh, const Skeleton& skeleton)
	{
		const uint64_t signature = posedBoundsSignature(mesh, skeleton);

		std::erase_if(animations.posedBoxes, [&](const PosedBox& box) {
			return box.sourceSignature == signature;
		});

		for (uint32_t meshIndex = 0; meshIndex < mesh.meshes.size(); ++meshIndex)
		{
			// An entry with no skin keeps its submesh boxes; a posed box for it would only repeat them.
			if (!isSkinned(mesh, meshIndex))
				continue;

			// See skinning.h: the box is derived data, never a cook gate.
			try
			{
				const Bounds bounds = posedBounds(mesh, meshIndex, skeleton, animations);
				animations.posedBoxes.push_back(
					PosedBox{ signature, bounds.min, bounds.max, meshIndex });
			}
			catch (const std::exception&)
			{}
		}
	}

	std::optional<Bounds>
	findPosedBounds(
		const AnimationSet& animations,
		const BMesh&        mesh,
		const uint32_t      meshIndex,
		const Skeleton&     skeleton) noexcept
	{
		if (animations.posedBoxes.empty())
			return std::nullopt;

		const uint64_t signature = posedBoundsSignature(mesh, skeleton);
		for (const PosedBox& box : animations.posedBoxes)
			if (box.sourceSignature == signature && box.meshIndex == meshIndex)
				return Bounds{ box.min, box.max };

		return std::nullopt;
	}
}
