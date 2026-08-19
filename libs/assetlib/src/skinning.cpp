#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>

#include <core/err/util.h>
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
	}

	std::vector<SkinnedVertex>
	skinSubmesh(const BMesh& mesh, const Submesh& submesh, std::span<const glm::mat4> skinning)
	{
		const auto positionOffset =
			decodableOffset(submesh.layout, VertexSemantic::kPosition, VertexFormat::kFloat32x3);
		const auto normalOffset =
			decodableOffset(submesh.layout, VertexSemantic::kNormal, VertexFormat::kFloat32x3);
		const auto jointsOffset =
			decodableOffset(submesh.layout, VertexSemantic::kJoints0, VertexFormat::kUint16x4);
		const auto weightsOffset =
			decodableOffset(submesh.layout, VertexSemantic::kWeights0, VertexFormat::kUnorm16x4);

		if (!positionOffset)
			throw_runtime_error("skinning: a submesh with no position attribute");

		// One without the other is a layout nothing can act on: indices with no share, or shares
		// naming no bone.
		if (jointsOffset.has_value() != weightsOffset.has_value())
			throw_runtime_error(
				"skinning: a submesh carrying only one half of its skin attributes");

		const size_t stride = submesh.layout.stride;
		const size_t first  = submesh.vertexByteOffset;
		const size_t span   = static_cast<size_t>(submesh.vertexCount) * stride;

		if (stride == 0 || first + span > mesh.vertexData.size())
			throw_runtime_error(
				"skinning: a submesh whose {} vertices fall outside the mesh's vertex data",
				submesh.vertexCount);

		std::vector<SkinnedVertex> out(submesh.vertexCount);

		for (uint32_t v = 0; v < submesh.vertexCount; ++v)
		{
			const size_t base = first + static_cast<size_t>(v) * stride;

			const auto position = readAt<glm::vec3>(mesh.vertexData, base + *positionOffset);
			const auto normal   = normalOffset ?
			                          readAt<glm::vec3>(mesh.vertexData, base + *normalOffset) :
			                          glm::vec3(0.0f);

			if (!jointsOffset)
			{
				out[v] = { position, normal };
				continue;
			}

			glm::vec3 skinnedPosition(0.0f);
			glm::vec3 skinnedNormal(0.0f);
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
				total += weight;
			}

			// An exporter writes (0,0,0,0) for a vertex it never assigned, and the importer
			// renormalizes that to four zero weights rather than refusing the mesh. Blending them
			// would put the vertex at the origin -- which, once the bake fits one AABB around every
			// clip, drags that box out and costs precision on every other vertex of the rig.
			out[v] = total > 0.0f ? SkinnedVertex{ skinnedPosition, skinnedNormal } :
			                        SkinnedVertex{ position, normal };
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

		for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
		{
			for (uint32_t frame = 0; frame < animations.clips[clip].frameCount; ++frame)
			{
				const std::vector<glm::mat4> skinning = skinningMatrices(
					skeleton,
					poseModelTransforms(skeleton, animations, clip, frame));

				for (uint32_t i = 0; i < entry.submeshCount; ++i)
				{
					const Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];
					for (const SkinnedVertex& vertex : skinSubmesh(mesh, submesh, skinning))
						grow(vertex.position);
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
}
