#include <assetlib/bmesh.h>
#include <assetlib/container_info.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BVat.h>

#include <assetlib/RegenMesh.h>
#include <assetlib/image_io.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>

#include <core/err/util.h>
#include <core/hash.h>

#include "ref_paths.h"
#include "vat_tangent.h"

#include "mounted_io.h"

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		constexpr uint32_t c_PositionTexelBytes = 8;  // R16G16B16A16_UNORM
		constexpr uint32_t c_NormalTexelBytes   = 4;  // R8G8B8A8_UNORM

		ImageData
		makeTexture(uint32_t width, uint32_t height, VkFormat format, uint32_t texelBytes)
		{
			ImageData image;
			image.width    = width;
			image.height   = height;
			image.vkFormat = format;

			const uint64_t pitch = uint64_t(width) * texelBytes;
			image.pixels         = core::fixed_buffer<std::byte>(pitch * height);
			image.subresources.push_back({ 0, pitch, pitch * height });
			return image;
		}

		/**
		 * One frame's positions into row `row` of the position texture. The box extent is zero on
		 * an axis every frame agrees on; such a coordinate packs to 0 and unpacks to the bound
		 * itself, exact.
		 */
		void
		packPositionRow(
			ImageData&                     positions,
			uint32_t                       row,
			std::span<const SkinnedVertex> vertices,
			const glm::vec3&               boundsMin,
			const glm::vec3&               extent) noexcept
		{
			auto* texel = reinterpret_cast<uint16_t*>(
				positions.pixels.data() + uint64_t(row) * positions.subresources[0].rowPitch);

			for (const SkinnedVertex& vertex : vertices)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					const float span = extent[axis];
					texel[axis] =
						span > 0.0f ?
							glm::packUnorm1x16((vertex.position[axis] - boundsMin[axis]) / span) :
							uint16_t(0);
				}

				// Alpha is padding -- three-channel texture formats do not exist -- and written a
				// constant because the pixel buffer arrives uninitialized, and two machines baking
				// the same rig must cook identical bytes.
				texel[3] = std::numeric_limits<uint16_t>::max();
				texel += 4;
			}
		}

		glm::vec3
		unitOrZero(const glm::vec3& v) noexcept
		{
			const float len2 = glm::dot(v, v);
			return len2 > 0.0f ? v / std::sqrt(len2) : v;
		}

		/**
		 * One frame's normals into row `row` of the normal texture, re-unit and biased to unorm,
		 * with the tangent's twist against `bindPose` in the alpha (see vat_tangent.h).
		 */
		void
		packNormalRow(
			ImageData&                     normals,
			uint32_t                       row,
			std::span<const SkinnedVertex> vertices,
			std::span<const SkinnedVertex> bindPose) noexcept
		{
			assert(bindPose.size() == vertices.size());
			auto* texel = normals.pixels.data() + uint64_t(row) * normals.subresources[0].rowPitch;

			for (size_t v = 0; v < vertices.size(); ++v)
			{
				// Blending shortens a normal, so it is re-unit here; one a submesh never carried
				// stays zero, which packs to mid-grey -- the degenerate the shader's guard reads.
				const glm::vec3 n = unitOrZero(vertices[v].blendedNormal);

				const uint8_t nx = glm::packUnorm1x8(n.x * 0.5f + 0.5f);
				const uint8_t ny = glm::packUnorm1x8(n.y * 0.5f + 0.5f);
				const uint8_t nz = glm::packUnorm1x8(n.z * 0.5f + 0.5f);

				// Measured against the normal as the shader will read it back, not the exact one:
				// the arc's antiparallel guard is a branch, and the two sides must take it on the
				// same input.
				const glm::vec3 stored = unitOrZero(
					glm::vec3(
						glm::unpackUnorm1x8(nx),
						glm::unpackUnorm1x8(ny),
						glm::unpackUnorm1x8(nz)) *
						2.0f -
					1.0f);

				const float twist = vatTangentTwist(
					unitOrZero(bindPose[v].blendedNormal),
					unitOrZero(bindPose[v].blendedTangent),
					stored,
					unitOrZero(vertices[v].blendedTangent));

				texel[0] = std::byte(nx);
				texel[1] = std::byte(ny);
				texel[2] = std::byte(nz);
				texel[3] = std::byte(glm::packUnorm1x8(packTwist(twist)));
				texel += 4;
			}
		}

		void
		duplicateRow(ImageData& image, uint32_t from, uint32_t to) noexcept
		{
			const uint64_t pitch = image.subresources[0].rowPitch;
			std::memcpy(
				image.pixels.data() + uint64_t(to) * pitch,
				image.pixels.data() + uint64_t(from) * pitch,
				pitch);
		}

	}

	std::string
	normalizePath(std::string_view path)
	{
		// The public alias of normalizeRef: one body, so the recorded form and the reference
		// graph's keyed form can never drift.
		return normalizeRef(path);
	}

	std::filesystem::path
	vatPathFor(std::string_view meshRelPath, std::string_view animationsRelPath)
	{
		const std::string normalized = normalizePath(animationsRelPath);
		const uint64_t    hash       = core::hash_string(normalized, core::hash_seed());

		auto path = std::filesystem::path(meshRelPath);
		path.replace_filename(
			std::format(
				"{}@{}-{:08x}.bvat",
				path.stem().string(),
				std::filesystem::path(normalized).stem().string(),
				static_cast<uint32_t>(hash ^ (hash >> 32))));
		return path;
	}

	BVat
	bakeVat(const BMesh& mesh, const Skeleton& skeleton, const AnimationSet& animations)
	{
		validateSkeleton(skeleton);
		validateAnimationSet(animations);

		if (!isSkinned(mesh))
			throw_runtime_error(
				"vat: no submesh carries joint indices, so there is nothing to animate");

		if (!animationsMatchSkeleton(animations, skeleton))
			throw_runtime_error(
				"vat: the clips were resampled against a different rig (clip signature {:016x}, "
				"skeleton {:016x})",
				animations.skeletonSignature,
				skeletonSignature(skeleton));

		if (animations.clips.empty())
			throw_runtime_error("vat: the clip set holds no clips, so there is nothing to bake");

		BVat vat;
		vat.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		vat.skeletonSignature = animations.skeletonSignature;

		uint64_t columns = 0;
		for (const Submesh& submesh : mesh.submeshes)
		{
			vat.columns.push_back({ static_cast<uint32_t>(columns), submesh.vertexCount });
			columns += submesh.vertexCount;
		}

		uint64_t rows   = 0;
		uint64_t frames = 0;
		for (const AnimationClip& clip : animations.clips)
		{
			VatClip baked{};
			baked.nameOffset   = vat.stringPool.add(animations.stringPool.at(clip.nameOffset));
			baked.firstRow     = static_cast<uint32_t>(rows);
			baked.frameCount   = clip.frameCount;
			baked.firstPalette = static_cast<uint32_t>(frames * vat.boneCount);
			baked.sampleRate   = clip.sampleRate;
			baked.duration     = clip.duration;
			baked.loop         = clip.loop;
			vat.clips.push_back(baked);

			rows += clip.frameCount + 1;
			frames += clip.frameCount;
		}

		if (columns > c_MaxVatTextureDim)
			throw_runtime_error(
				"vat: {} vertex columns exceed the {} a texture can hold -- bake from a mesh with "
				"fewer vertices",
				columns,
				c_MaxVatTextureDim);

		if (rows > c_MaxVatTextureDim)
			throw_runtime_error(
				"vat: {} padded frame rows across {} clip(s) exceed the {} a texture can hold -- "
				"bake fewer or shorter clips",
				rows,
				animations.clips.size(),
				c_MaxVatTextureDim);

		vat.width  = static_cast<uint32_t>(columns);
		vat.height = static_cast<uint32_t>(rows);

		// The bind pose is the identity skin: the frame every baked tangent's twist is measured from.
		auto bindRow = std::vector<SkinnedVertex>();
		bindRow.reserve(vat.width);
		{
			const auto identity = std::vector<glm::mat4>(vat.boneCount, glm::mat4(1.0f));
			for (const Submesh& submesh : mesh.submeshes)
			{
				const auto skinned = skinSubmesh(mesh, submesh, identity);
				bindRow.insert(bindRow.end(), skinned.begin(), skinned.end());
			}
		}

		// Every frame is skinned once and kept: the AABB must close over all of them before the
		// first texel can be quantized against it.
		auto skinnedFrames = std::vector<std::vector<SkinnedVertex>>();
		skinnedFrames.reserve(frames);
		vat.palettes.reserve(frames * vat.boneCount);

		vat.boundsMin = glm::vec3(std::numeric_limits<float>::max());
		vat.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

		for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
		{
			for (uint32_t frame = 0; frame < animations.clips[clip].frameCount; ++frame)
			{
				const auto palette = skinningMatrices(
					skeleton,
					poseModelTransforms(skeleton, animations, clip, frame));

				auto row = std::vector<SkinnedVertex>();
				row.reserve(vat.width);
				for (const Submesh& submesh : mesh.submeshes)
				{
					const auto skinned = skinSubmesh(mesh, submesh, palette);
					row.insert(row.end(), skinned.begin(), skinned.end());
				}

				for (const SkinnedVertex& vertex : row)
				{
					vat.boundsMin = glm::min(vat.boundsMin, vertex.position);
					vat.boundsMax = glm::max(vat.boundsMax, vertex.position);
				}

				vat.palettes.insert(vat.palettes.end(), palette.begin(), palette.end());
				skinnedFrames.push_back(std::move(row));
			}
		}

		ImageData positions =
			makeTexture(vat.width, vat.height, VkFormat::R16G16B16A16_UNORM, c_PositionTexelBytes);
		ImageData normals =
			makeTexture(vat.width, vat.height, VkFormat::R8G8B8A8_UNORM, c_NormalTexelBytes);

		const glm::vec3 extent = vat.boundsMax - vat.boundsMin;

		size_t frameIndex = 0;
		for (const VatClip& clip : vat.clips)
		{
			for (uint32_t frame = 0; frame < clip.frameCount; ++frame, ++frameIndex)
			{
				const uint32_t row = clip.firstRow + frame;
				packPositionRow(positions, row, skinnedFrames[frameIndex], vat.boundsMin, extent);
				packNormalRow(normals, row, skinnedFrames[frameIndex], bindRow);
			}

			const uint32_t last = clip.firstRow + clip.frameCount - 1;
			duplicateRow(positions, last, last + 1);
			duplicateRow(normals, last, last + 1);
		}

		vat.positionsKtx2 = encodeKTX2(positions);
		vat.normalsKtx2   = encodeKTX2(normals);
		return vat;
	}

	BVat
	AssetStore::BakeVat(const VatBakeDesc& desc) const
	{
		// The regeneration seam, not the plain loads: a bake over a stale group would otherwise
		// freeze the stale geometry into a texture the freshness rule then calls current.
		const BMesh mesh = LoadRegenMesh(desc.mesh).mesh;

		// A static mesh fails the in-memory bake anyway; refusing here names the actual gap --
		// there is no rig to load -- instead of failing to open a file with no name.
		if (mesh.skeleton.empty())
			throw_runtime_error(
				"vat: '{}' names no skeleton, so there is no rig to bake",
				desc.mesh);

		BVat vat =
			bakeVat(mesh, LoadRegenSkeleton(mesh.skeleton), LoadRegenAnimations(desc.animations));

		vat.mesh            = normalizePath(desc.mesh);
		vat.skeleton        = normalizePath(mesh.skeleton);
		vat.animations      = normalizePath(desc.animations);
		vat.meshStamp       = StampOf(vat.mesh);
		vat.skeletonStamp   = StampOf(vat.skeleton);
		vat.animationsStamp = StampOf(vat.animations);
		return vat;
	}

	bool
	vatIsStale(const BVat& vat, const core::file::IFileSystem& fileSystem)
	{
		return stampOf(fileSystem, vat.mesh) != vat.meshStamp ||
		       stampOf(fileSystem, vat.skeleton) != vat.skeletonStamp ||
		       stampOf(fileSystem, vat.animations) != vat.animationsStamp;
	}
}
