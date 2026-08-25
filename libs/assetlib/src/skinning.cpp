#include <assetlib/bmesh.h>
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

		Bounds
		unboundedBox() noexcept
		{
			return Bounds{ glm::vec3(std::numeric_limits<float>::max()),
				           glm::vec3(std::numeric_limits<float>::lowest()) };
		}

		bool
		isEmpty(const Bounds& box) noexcept
		{
			return glm::any(glm::greaterThan(box.min, box.max));
		}

		void
		grow(Bounds& box, const glm::vec3& point) noexcept
		{
			box.min = glm::min(box.min, point);
			box.max = glm::max(box.max, point);
		}

		void
		grow(Bounds& box, const Bounds& other) noexcept
		{
			box.min = glm::min(box.min, other.min);
			box.max = glm::max(box.max, other.max);
		}

		/** `box` swept by `transform`, as the axis-aligned box holding the result. */
		Bounds
		transformed(const glm::mat4& transform, const Bounds& box) noexcept
		{
			const glm::vec3 centre = (box.min + box.max) * 0.5f;
			const glm::vec3 extent = (box.max - box.min) * 0.5f;

			const glm::vec3 moved  = glm::vec3(transform * glm::vec4(centre, 1.0f));
			const glm::vec3 spread = glm::mat3(
										 glm::abs(glm::vec3(transform[0])),
										 glm::abs(glm::vec3(transform[1])),
										 glm::abs(glm::vec3(transform[2]))) *
			                         extent;

			return Bounds{ moved - spread, moved + spread };
		}

		/**
		 * One mesh entry reduced to what a pose can move: a box per bone, in that bone's own frame,
		 * plus the vertices no bone moves.
		 *
		 * A bone's box holds only the vertices it has weight on, inverse-bound into its space, so a
		 * pose reaches it as `modelTransform * box` -- the same product skinning takes, with the
		 * inverse bind folded in once instead of per vertex per frame.
		 */
		struct BoneBoxes
		{
			std::vector<uint32_t> bones;
			std::vector<Bounds>   boxes;  // parallel to `bones`, in bone space
			Bounds                unskinned = unboundedBox();
		};

		/**
		 * @throws std::runtime_error for anything decodeInfluences refuses: a malformed vertex
		 *         layout, or a vertex naming a bone the skeleton does not hold.
		 */
		BoneBoxes
		boneBoxesFor(const BMesh& mesh, const uint32_t meshIndex, const Skeleton& skeleton)
		{
			const Mesh& entry = mesh.meshes[meshIndex];

			auto perBone = std::vector<Bounds>(skeleton.bones.size(), unboundedBox());
			auto out     = BoneBoxes();

			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];

				const std::vector<SkinInfluences> vertices = decodeInfluences(
					mesh,
					submesh,
					resolveSkinLayout(mesh, submesh),
					skeleton.bones.size());

				for (const SkinInfluences& vertex : vertices)
				{
					// An exporter writes four zero weights for a vertex it never assigned, and
					// skinnedPosition leaves it at its bind position -- so does this.
					if (!vertex.skinned)
					{
						grow(out.unskinned, vertex.position);
						continue;
					}

					for (size_t influence = 0; influence < c_InfluencesPerVertex; ++influence)
					{
						if (vertex.weights[influence] == 0.0f)
							continue;

						const uint32_t bone = vertex.joints[influence];
						grow(
							perBone[bone],
							glm::vec3(
								skeleton.bones[bone].inverseBind *
								glm::vec4(vertex.position, 1.0f)));
					}
				}
			}

			for (uint32_t bone = 0; bone < perBone.size(); ++bone)
			{
				if (isEmpty(perBone[bone]))
					continue;

				out.bones.push_back(bone);
				out.boxes.push_back(perBone[bone]);
			}

			return out;
		}

		/**
		 * One box per entry, over every pose of every clip. The pose is walked once and shared:
		 * evaluating it per entry costs a rig's whole hierarchy again for each mesh it is drawn as.
		 *
		 * @throws std::runtime_error for anything poseModelTransforms refuses -- above all a clip
		 *         set cooked against another rig.
		 */
		std::vector<Bounds>
		sweepPoses(
			std::span<const BoneBoxes> entries,
			const Skeleton&            skeleton,
			const AnimationSet&        animations)
		{
			auto out = std::vector<Bounds>(entries.size(), unboundedBox());

			for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
			{
				for (uint32_t frame = 0; frame < animations.clips[clip].frameCount; ++frame)
				{
					const std::vector<glm::mat4> model =
						poseModelTransforms(skeleton, animations, clip, frame);

					for (size_t entry = 0; entry < entries.size(); ++entry)
						for (size_t i = 0; i < entries[entry].bones.size(); ++i)
							grow(
								out[entry],
								transformed(
									model[entries[entry].bones[i]],
									entries[entry].boxes[i]));
				}
			}

			// Whatever the pose, these sit where they were authored.
			for (size_t entry = 0; entry < entries.size(); ++entry)
				if (!isEmpty(entries[entry].unskinned))
					grow(out[entry], entries[entry].unskinned);

			return out;
		}

		/** The union of the entry's submesh boxes -- all a rig with no clips has to be bounded by. */
		Bounds
		bindPoseBounds(const BMesh& mesh, const Mesh& entry) noexcept
		{
			auto out = unboundedBox();
			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];
				grow(out, submesh.aabbMin);
				grow(out, submesh.aabbMax);
			}
			return out;
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

		auto out = unboundedBox();

		// Only where there are frames to walk: a rig with no clips is bounded by its bind pose below,
		// and decoding here would refuse a malformed layout it used to reach that fallback with.
		if (!animations.clips.empty())
		{
			const auto entries = std::array{ boneBoxesFor(mesh, meshIndex, skeleton) };
			out                = sweepPoses(entries, skeleton, animations).front();
		}

		if (isEmpty(out))
			out = bindPoseBounds(mesh, mesh.meshes[meshIndex]);

		core::throw_runtime_error_if(
			isEmpty(out),
			"posedBounds: mesh {} has no submesh to bound",
			meshIndex);

		return out;
	}

	Bounds
	exactPosedBounds(
		const BMesh&        mesh,
		const uint32_t      meshIndex,
		const Skeleton&     skeleton,
		const AnimationSet& animations)
	{
		core::throw_runtime_error_if(
			meshIndex >= mesh.meshes.size(),
			"exactPosedBounds: mesh index {} out of range",
			meshIndex);

		const Mesh& entry = mesh.meshes[meshIndex];

		auto out = unboundedBox();

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
						grow(out, skinnedPosition(vertex, skinning));
				}
			}
		}

		if (isEmpty(out))
			out = bindPoseBounds(mesh, entry);

		core::throw_runtime_error_if(
			isEmpty(out),
			"exactPosedBounds: mesh {} has no submesh to bound",
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

		auto indices = std::vector<uint32_t>();
		auto entries = std::vector<BoneBoxes>();
		indices.reserve(mesh.meshes.size());
		entries.reserve(mesh.meshes.size());

		for (uint32_t meshIndex = 0; meshIndex < mesh.meshes.size(); ++meshIndex)
		{
			// An entry with no skin keeps its submesh boxes; a posed box for it would only repeat them.
			if (!isSkinned(mesh, meshIndex))
				continue;

			// See skinning.h: the box is derived data, never a cook gate.
			try
			{
				entries.push_back(boneBoxesFor(mesh, meshIndex, skeleton));
				indices.push_back(meshIndex);
			}
			catch (const std::exception&)
			{}
		}

		if (entries.empty())
			return;

		auto bounds = std::vector<Bounds>();
		try
		{
			bounds = sweepPoses(entries, skeleton, animations);
		}
		catch (const std::exception&)
		{
			// A clip set cooked against another rig fails every entry at once, and a box measured
			// against the wrong rig would be trusted verbatim at load. Leave them unbaked.
			return;
		}

		for (size_t i = 0; i < indices.size(); ++i)
		{
			if (isEmpty(bounds[i]))
				bounds[i] = bindPoseBounds(mesh, mesh.meshes[indices[i]]);

			if (isEmpty(bounds[i]))
				continue;

			animations.posedBoxes.push_back(
				PosedBox{ signature, bounds[i].min, bounds[i].max, indices[i] });
		}
	}

	std::vector<std::optional<Bounds>>
	findPosedBounds(const AnimationSet& animations, const BMesh& mesh, const Skeleton& skeleton)
	{
		auto out = std::vector<std::optional<Bounds>>(mesh.meshes.size());

		if (animations.posedBoxes.empty())
			return out;

		const uint64_t signature = posedBoundsSignature(mesh, skeleton);
		for (const PosedBox& box : animations.posedBoxes)
			if (box.sourceSignature == signature && box.meshIndex < out.size())
				out[box.meshIndex] = Bounds{ box.min, box.max };

		return out;
	}
}
