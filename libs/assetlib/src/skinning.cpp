#include <assetlib/avatar.h>
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
#include <tracy/Tracy.hpp>

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
		 * One mesh entry's vertices, decoded submesh by submesh.
		 *
		 * @throws std::runtime_error for anything decodeInfluences refuses: a malformed vertex
		 *         layout, or a vertex naming a bone the skeleton does not hold.
		 */
		std::vector<std::vector<SkinInfluences>>
		entryInfluences(const BMesh& mesh, const uint32_t meshIndex, const Skeleton& skeleton)
		{
			const Mesh& entry = mesh.meshes[meshIndex];

			auto out = std::vector<std::vector<SkinInfluences>>();
			out.reserve(entry.submeshCount);

			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];
				out.emplace_back(decodeInfluences(
					mesh,
					submesh,
					resolveSkinLayout(mesh, submesh),
					skeleton.bones.size()));
			}

			return out;
		}

		/** Every bone `submeshes` has weight on, boxed over the vertices it moves, in its own frame. */
		BoneBoxes
		boneBoxesFrom(
			std::span<const std::vector<SkinInfluences>> submeshes,
			const Skeleton&                              skeleton)
		{
			auto perBone = std::vector<Bounds>(skeleton.bones.size(), unboundedBox());
			auto out     = BoneBoxes();

			for (const std::vector<SkinInfluences>& vertices : submeshes)
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
		 * @throws std::runtime_error for anything decodeInfluences refuses: a malformed vertex
		 *         layout, or a vertex naming a bone the skeleton does not hold.
		 */
		BoneBoxes
		boneBoxesFor(const BMesh& mesh, const uint32_t meshIndex, const Skeleton& skeleton)
		{
			return boneBoxesFrom(entryInfluences(mesh, meshIndex, skeleton), skeleton);
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

		/** One skinned entry: the boxes a pose sweeps, and the vertices those boxes hold. */
		struct SkinnedEntry
		{
			BoneBoxes                                boxes;
			std::vector<std::vector<SkinInfluences>> submeshes;
		};

		/** Every skinned entry of `mesh`, its vertices decoded once and its boxes built from them. */
		std::vector<SkinnedEntry>
		skinnedEntries(const BMesh& mesh, const Skeleton& skeleton)
		{
			auto out = std::vector<SkinnedEntry>();
			for (uint32_t meshIndex = 0; meshIndex < mesh.meshes.size(); ++meshIndex)
			{
				if (!isSkinned(mesh, meshIndex))
					continue;

				auto submeshes = entryInfluences(mesh, meshIndex, skeleton);
				out.emplace_back(boneBoxesFrom(submeshes, skeleton), std::move(submeshes));
			}
			return out;
		}

		/**
		 * The lowest `y` any bone box reaches under `model` -- a lower bound on the lowest vertex,
		 * because a skinned position is a convex combination of its bones' products and so lies in
		 * the hull of the boxes those products sweep.
		 */
		float
		boundedFloor(
			std::span<const SkinnedEntry> entries,
			std::span<const glm::mat4>    model) noexcept
		{
			auto lowest = std::numeric_limits<float>::max();
			for (const SkinnedEntry& entry : entries)
				for (size_t i = 0; i < entry.boxes.bones.size(); ++i)
					lowest = std::min(
						lowest,
						transformed(model[entry.boxes.bones[i]], entry.boxes.boxes[i]).min.y);
			return lowest;
		}

		/**
		 * `best`, lowered to any `y` a vertex of `entry` actually reaches under `skinning`.
		 *
		 * A vertex whose every bone is already at or above `best` is skipped rather than skinned --
		 * boundedFloor's bound, taken per vertex. See measureClipFloors in skinning.h.
		 *
		 * `boneLow` is scratch across calls: it is written for exactly the bones `entry` has weight
		 * on, which is the only set its vertices read.
		 */
		float
		entryFloor(
			const SkinnedEntry&        entry,
			std::span<const glm::mat4> model,
			std::span<const glm::mat4> skinning,
			std::span<float>           boneLow,
			float                      best) noexcept
		{
			for (size_t i = 0; i < entry.boxes.bones.size(); ++i)
				boneLow[entry.boxes.bones[i]] =
					transformed(model[entry.boxes.bones[i]], entry.boxes.boxes[i]).min.y;

			for (const std::vector<SkinInfluences>& vertices : entry.submeshes)
				for (const SkinInfluences& vertex : vertices)
				{
					// Whatever the pose these sit where they were authored, and measureClipFloors
					// already floors every clip by BoneBoxes::unskinned.
					if (!vertex.skinned)
						continue;

					auto bound = std::numeric_limits<float>::max();
					for (size_t i = 0; i < c_InfluencesPerVertex; ++i)
						if (vertex.weights[i] != 0.0f)
							bound = std::min(bound, boneLow[vertex.joints[i]]);

					if (bound >= best)
						continue;

					best = std::min(best, skinnedPosition(vertex, skinning).y);
				}

			return best;
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
		ZoneScopedN("assetlib posed bounds");
		ZoneTextF(
			"%zu bones, %zu entries, %zu frames",
			skeleton.bones.size(),
			mesh.meshes.size(),
			animations.boneCount > 0 ? animations.samples.size() / animations.boneCount : 0);

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

			// Value-initialised, not aggregate-initialised: PosedBox carries four bytes of
			// tail padding that are written to the file verbatim, and aggregate init leaves
			// them indeterminate -- two runs then produce .banim files that differ by junk.
			PosedBox box{};
			box.sourceSignature = signature;
			box.min             = bounds[i].min;
			box.max             = bounds[i].max;
			box.meshIndex       = indices[i];
			animations.posedBoxes.push_back(box);
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

	std::vector<float>
	measureClipFloors(
		const AnimationSet&    animations,
		std::span<const BMesh> meshes,
		const Skeleton&        skeleton)
	{
		auto out = std::vector<float>(animations.clips.size(), 0.0f);

		auto entries = std::vector<SkinnedEntry>();
		for (const BMesh& mesh : meshes)
		{
			std::vector<SkinnedEntry> mine = skinnedEntries(mesh, skeleton);
			std::ranges::move(mine, std::back_inserter(entries));
		}

		if (entries.empty())
			return out;

		// Whatever the pose, these sit where they were authored, so they floor every clip alike.
		auto unskinned = std::numeric_limits<float>::max();
		for (const SkinnedEntry& entry : entries)
			if (!isEmpty(entry.boxes.unskinned))
				unskinned = std::min(unskinned, entry.boxes.unskinned.min.y);

		auto boneLow = std::vector<float>(skeleton.bones.size(), std::numeric_limits<float>::max());

		for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
		{
			const uint32_t frames = animations.clips[clip].frameCount;

			// The poses themselves are deliberately not kept: a dense rig's clip would be tens of
			// megabytes of them, and re-evaluating the handful that survive the prune is a forward
			// pass over the bones against skinning every vertex.
			auto bound = std::vector<float>(frames);
			auto order = std::vector<uint32_t>(frames);
			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				bound[frame] =
					boundedFloor(entries, poseModelTransforms(skeleton, animations, clip, frame));
				order[frame] = frame;
			}

			std::ranges::sort(order, [&](uint32_t a, uint32_t b) { return bound[a] < bound[b]; });

			auto lowest = std::numeric_limits<float>::max();
			for (const uint32_t frame : order)
			{
				// Every frame after this one is bounded at or above what has already been found.
				if (bound[frame] >= lowest)
					break;

				const std::vector<glm::mat4> model =
					poseModelTransforms(skeleton, animations, clip, frame);
				const std::vector<glm::mat4> skinning = skinningMatrices(skeleton, model);

				for (const SkinnedEntry& entry : entries)
					lowest = entryFloor(entry, model, skinning, boneLow, lowest);
			}

			lowest = std::min(lowest, unskinned);
			if (lowest != std::numeric_limits<float>::max())
				out[clip] = lowest;
		}

		return out;
	}

	void
	groundClips(
		AnimationSet&              animations,
		std::span<const BMesh>     meshes,
		const Skeleton&            skeleton,
		std::span<const ClipFloor> authored)
	{
		ZoneScopedN("assetlib clip floors");
		ZoneTextF(
			"%zu bones, %zu meshes, %zu clips, %zu frames",
			skeleton.bones.size(),
			meshes.size(),
			animations.clips.size(),
			animations.boneCount > 0 ? animations.samples.size() / animations.boneCount : 0);

		auto floors = std::vector<float>(animations.clips.size(), 0.0f);
		try
		{
			floors = measureClipFloors(animations, meshes, skeleton);
		}
		catch (const std::exception&)
		{
			// See skinning.h: a refused measurement leaves every clip where it was authored, but
			// an authored floor needs no pose to apply and is not lost with it.
		}

		for (const ClipFloor& authoredFloor : authored)
			for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
				if (animations.stringPool.at(animations.clips[clip].nameOffset) ==
				    authoredFloor.clip)
					floors[clip] = authoredFloor.floor - animations.clips[clip].groundOffset;

		auto roots = std::vector<uint32_t>();
		for (uint32_t bone = 0; bone < skeleton.bones.size(); ++bone)
			if (skeleton.bones[bone].parent == c_InvalidIndex)
				roots.push_back(bone);

		for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
		{
			if (floors[clip] == 0.0f)
				continue;

			for (uint32_t frame = 0; frame < animations.clips[clip].frameCount; ++frame)
			{
				const size_t pose = animations.clips[clip].firstSample +
				                    static_cast<size_t>(frame) * animations.boneCount;
				for (const uint32_t bone : roots)
					animations.samples[pose + bone].translation.y -= floors[clip];
			}

			animations.clips[clip].groundOffset += floors[clip];
		}
	}

	namespace
	{
		/**
		 * The model-space position of every vertex `meshes` weight to `ankle` or `toe`, at bind pose
		 * -- which is the position the vertex already carries: a bone's skinning matrix at bind pose
		 * is its model transform times its own inverse, so the whole skin is identity there.
		 */
		std::vector<glm::vec3>
		footVertices(
			std::span<const BMesh> meshes,
			const Skeleton&        skeleton,
			const uint32_t         ankle,
			const uint32_t         toe)
		{
			auto out = std::vector<glm::vec3>();

			for (const BMesh& mesh : meshes)
				for (uint32_t entry = 0; entry < mesh.meshes.size(); ++entry)
					for (const std::vector<SkinInfluences>& submesh :
					     entryInfluences(mesh, entry, skeleton))
						for (const SkinInfluences& vertex : submesh)
						{
							if (!vertex.skinned)
								continue;

							// Any weight at all, not a majority: the sole's own vertices are shared
							// with the toe and often mostly weighted to it, and a threshold high
							// enough to exclude the ankle's neighbours excludes them too.
							float held = 0.0f;
							for (size_t i = 0; i < c_InfluencesPerVertex; ++i)
								if (vertex.joints[i] == ankle || vertex.joints[i] == toe)
									held += vertex.weights[i];

							if (held > 0.0f)
								out.push_back(vertex.position);
						}

			return out;
		}

		/**
		 * The plane under `points`, as `y = ax + bz + d`.
		 *
		 * False when the system is singular -- fewer than three points, or a set that projects onto
		 * a line in xz, which is a foot with no sole to fit.
		 */
		bool
		fitSole(std::span<const glm::vec3> points, glm::vec3& normal, glm::vec3& on)
		{
			if (points.size() < 3)
				return false;

			// The lower envelope, reached by fitting and dropping whatever came out above: the
			// foot's vertices are a closed shell whose upper half and whose vertices up the shin
			// would otherwise pull the plane off the sole. Keeping the lowest *fraction* instead was
			// tried and is wrong -- on a tilted sole the band it keeps is the heel alone, so it
			// measures the tilt away.
			auto sole = std::vector<glm::vec3>(points.begin(), points.end());

			double a = 0, b = 0, d = 0;
			bool   fitted = false;

			for (int pass = 0; pass < 4; ++pass)
			{
				// Normal equations of the least-squares fit of y = ax + bz + d.
				double sxx = 0, sxz = 0, sx = 0, szz = 0, sz = 0, n = 0;
				double sxy = 0, szy = 0, sy = 0;
				for (const glm::vec3& p : sole)
				{
					sxx += double(p.x) * p.x;
					sxz += double(p.x) * p.z;
					sx += p.x;
					szz += double(p.z) * p.z;
					sz += p.z;
					n += 1.0;
					sxy += double(p.x) * p.y;
					szy += double(p.z) * p.y;
					sy += p.y;
				}

				const double det = sxx * (szz * n - sz * sz) - sxz * (sxz * n - sz * sx) +
				                   sx * (sxz * sz - szz * sx);

				// A set that projects onto a line in xz -- a two-triangle foot, or a coplanar
				// strip. Scaled by the count so the tolerance means the same at any density.
				if (std::abs(det) < 1e-12 * n)
					break;

				a      = (sxy * (szz * n - sz * sz) - sxz * (szy * n - sz * sy) +
				          sx * (szy * sz - szz * sy)) /
				         det;
				b      = (sxx * (szy * n - sz * sy) - sxy * (sxz * n - sz * sx) +
				          sx * (sxz * sy - szy * sx)) /
				         det;
				d      = (sxx * (szz * sy - sz * szy) - sxz * (sxz * sy - sz * sxy) +
				          sxy * (sxz * sz - szz * sx)) /
				         det;
				fitted = true;

				auto below = std::vector<glm::vec3>();
				for (const glm::vec3& p : sole)
					if (double(p.y) <= a * p.x + b * p.z + d)
						below.push_back(p);

				// Converged, or down to a set too small to fit again: this pass's plane stands.
				if (below.size() < 3 || below.size() == sole.size())
					break;

				sole = std::move(below);
			}

			if (!fitted)
				return false;

			normal = glm::normalize(glm::vec3(-float(a), 1.0f, -float(b)));

			// A point on the plane below the sole's own centre, rather than the fit's intercept at
			// the origin, which for a foot standing away from it is nowhere near the foot.
			const glm::vec3 centre =
				std::accumulate(sole.begin(), sole.end(), glm::vec3(0.0f)) / float(sole.size());
			on = glm::vec3(centre.x, float(a * centre.x + b * centre.z + d), centre.z);
			return true;
		}
	}

	std::vector<SolePlane>
	solePlanes(
		const std::span<const BMesh>          meshes,
		const Skeleton&                       skeleton,
		const std::span<const AvatarLegChain> chains)
	{
		ZoneScopedN("assetlib sole planes");

		const std::vector<glm::mat4> bind = bindPoseModelTransforms(skeleton);

		auto out = std::vector<SolePlane>();
		out.reserve(chains.size());

		for (const AvatarLegChain& chain : chains)
		{
			core::throw_runtime_error_if(
				chain.ankle >= skeleton.bones.size() || chain.toe >= skeleton.bones.size(),
				"skinning: a leg names a bone outside the {}-bone skeleton",
				skeleton.bones.size());

			const glm::mat4 toAnkle = skeleton.bones[chain.ankle].inverseBind;

			glm::vec3 normal;
			glm::vec3 on;
			if (!fitSole(footVertices(meshes, skeleton, chain.ankle, chain.toe), normal, on))
			{
				// The flat plane through the joint: a leg no mesh here carries has no sole to fit,
				// and inventing a tilt would turn the foot at runtime for no reason.
				out.emplace_back(
					glm::vec3(toAnkle * glm::vec4(glm::vec3(bind[chain.ankle][3]), 1.0f)),
					glm::normalize(glm::vec3(toAnkle * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f))));
				continue;
			}

			// Carried by the inverse bind rather than its inverse transpose, which is the trade
			// skinSubmesh already makes: exact while a bone's scale is uniform, and every rig we
			// cook is.
			out.emplace_back(
				glm::vec3(toAnkle * glm::vec4(on, 1.0f)),
				glm::normalize(glm::vec3(toAnkle * glm::vec4(normal, 0.0f))));
		}

		return out;
	}

	uint64_t
	plantWeightsSignature(
		const std::span<const BMesh>          meshes,
		const Skeleton&                       skeleton,
		const std::span<const AvatarLegChain> chains) noexcept
	{
		uint64_t hash = core::hash_pod(skeletonSignature(skeleton), core::hash_seed());

		for (const AvatarLegChain& chain : chains)
		{
			hash = core::hash_pod(chain.hip, hash);
			hash = core::hash_pod(chain.knee, hash);
			hash = core::hash_pod(chain.ankle, hash);
			hash = core::hash_pod(chain.toe, hash);
		}

		// The geometry the soles were fitted on, by the same hash a posed box keys on: it is the
		// vertex data and the tables that address it, which is exactly what a sole fit reads.
		for (const BMesh& mesh : meshes)
			hash = core::hash_pod(posedBoundsSignature(mesh, skeleton), hash);

		return hash;
	}

	std::vector<uint8_t>
	measurePlantWeights(
		const AnimationSet&                   animations,
		const Skeleton&                       skeleton,
		const std::span<const AvatarLegChain> chains,
		const std::span<const SolePlane>      soles)
	{
		ZoneScopedN("assetlib plant weights");

		core::throw_runtime_error_if(
			soles.size() != chains.size(),
			"skinning: {} sole planes for {} legs",
			soles.size(),
			chains.size());

		// Checked here and not left to the pose walk: this reads `model[chain.ankle]` directly, and
		// a public function that indexes a caller's number has to judge it rather than trust that
		// solePlanes was asked first.
		for (const AvatarLegChain& chain : chains)
			core::throw_runtime_error_if(
				chain.ankle >= skeleton.bones.size(),
				"skinning: a leg names bone {}, which is outside the {}-bone skeleton",
				chain.ankle,
				skeleton.bones.size());

		const size_t legs = chains.size();
		const size_t frames =
			animations.boneCount == 0 ? 0 : animations.samples.size() / animations.boneCount;

		auto out = std::vector<uint8_t>(frames * legs, 0u);
		if (legs == 0 || frames == 0)
			return out;

		for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
		{
			const AnimationClip& played = animations.clips[clip];

			// A clip that does not start on a frame boundary has no frame index to write its
			// weights at, so it gets none rather than a run landing on another clip's frames. bgl
			// refuses such a file outright; here the weights are derived and the rest of the set is
			// still worth measuring.
			if (played.firstSample % animations.boneCount != 0)
				continue;

			const uint32_t first = played.firstSample / animations.boneCount;

			// Where each sole sits in each frame of this clip, walked once: the pose is the whole
			// hierarchy, and evaluating it per leg would cost that again for each foot.
			auto sole = std::vector<glm::vec3>(size_t(played.frameCount) * legs);
			for (uint32_t frame = 0; frame < played.frameCount; ++frame)
			{
				const std::vector<glm::mat4> model =
					poseModelTransforms(skeleton, animations, clip, frame);

				for (size_t leg = 0; leg < legs; ++leg)
					sole[size_t(frame) * legs + leg] =
						glm::vec3(model[chains[leg].ankle] * glm::vec4(soles[leg].point, 1.0f));
			}

			for (size_t leg = 0; leg < legs; ++leg)
			{
				auto planted = std::vector<bool>(played.frameCount, false);
				for (uint32_t frame = 0; frame < played.frameCount; ++frame)
				{
					const glm::vec3& here = sole[size_t(frame) * legs + leg];
					if (here.y > c_PlantHeightEpsilon)
						continue;

					// The frame either side, clamped at the ends: a clip's first and last frame have
					// only one neighbour, and a foot planted through the whole clip must not lose
					// its ends to the absence of one.
					const uint32_t before = frame == 0 ? 0 : frame - 1;
					const uint32_t after =
						frame + 1 >= played.frameCount ? played.frameCount - 1 : frame + 1;

					const glm::vec3& was  = sole[size_t(before) * legs + leg];
					const glm::vec3& will = sole[size_t(after) * legs + leg];

					const float slide = glm::length(glm::vec2(will.x - was.x, will.z - was.z));
					planted[frame]    = slide <= c_PlantSlideEpsilon;
				}

				// Ramped by distance to the nearest frame the foot is *not* planted in, which is the
				// same thing as ramping each run in and out without having to find its boundaries:
				// a run shorter than two ramps simply never reaches 1, which is what a foot that
				// touched down and lifted straight off should read as.
				//
				// A run reaching the clip's own edge is not ramped there. That edge is where the
				// clip stops, not a foot leaving the ground, and ramping a looping idle out at its
				// last frame would put a pop exactly on the loop point.
				const float ramp = float(c_PlantRampFrames - 1);

				for (uint32_t frame = 0; frame < played.frameCount; ++frame)
				{
					if (!planted[frame])
						continue;

					uint32_t in = 0;
					while (in < frame && planted[frame - in - 1]) ++in;

					uint32_t held = 0;
					while (frame + held + 1 < played.frameCount && planted[frame + held + 1])
						++held;

					// Each walk above stops either on an unplanted frame -- a transition, which the
					// ramp is for -- or on the clip's edge, which is not one.
					float edge = ramp;
					if (in < frame)
						edge = std::min(edge, float(in));
					if (frame + held + 1 < played.frameCount)
						edge = std::min(edge, float(held));

					out[(size_t(first) + frame) * legs + leg] =
						static_cast<uint8_t>(std::lround(edge / ramp * 255.0f));
				}
			}
		}

		return out;
	}

	void
	bakePlantWeights(
		AnimationSet&                         animations,
		const std::span<const BMesh>          meshes,
		const Skeleton&                       skeleton,
		const std::span<const AvatarLegChain> chains)
	{
		animations.plantWeights = PlantWeights();
		if (chains.empty())
			return;

		try
		{
			const std::vector<SolePlane> soles = solePlanes(meshes, skeleton, chains);

			animations.plantWeights.weights =
				measurePlantWeights(animations, skeleton, chains, soles);
			animations.plantWeights.legCount  = static_cast<uint32_t>(chains.size());
			animations.plantWeights.signature = plantWeightsSignature(meshes, skeleton, chains);
		}
		catch (const std::exception&)
		{
			// Derived data: a load measures, and reports, exactly as it would had this never run.
			// Whatever refused has already been reported where it was read.
			animations.plantWeights = PlantWeights();
		}
	}

	std::optional<std::vector<uint8_t>>
	findPlantWeights(
		const AnimationSet&                   animations,
		const std::span<const BMesh>          meshes,
		const Skeleton&                       skeleton,
		const std::span<const AvatarLegChain> chains)
	{
		if (animations.plantWeights.Empty() || chains.empty())
			return std::nullopt;

		if (animations.plantWeights.legCount != chains.size())
			return std::nullopt;

		if (animations.plantWeights.signature != plantWeightsSignature(meshes, skeleton, chains))
			return std::nullopt;

		return animations.plantWeights.weights;
	}
}
