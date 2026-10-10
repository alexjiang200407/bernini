#include "util/util.h"
#include <algorithm>
#include <bgl/GeomType.h>
#include <bgl/LodLevel.h>
#include <bgl/MaterialType.h>
#include <bgl/MeshInstanceFlag.h>
#include <bgl/SurfaceType.h>
#include <bgl/glm.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/CullView.h>
#include <bgl/idl/DrawBucket.h>
#include <bgl/idl/InstanceLod.h>
#include <bgl/idl/MeshInstance.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/Viewport.h>
#include <bgpu/types/Format.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace bgl
{
	// 1024 is a compute thread group's maximum; PrefixSumInstances.slang is one group.
	static_assert(idl::cMaxDrawLanes <= 1024);

	std::optional<uint32_t>
	GameSlot(MaterialType material) noexcept
	{
		const auto kind  = std::to_underlying(material);
		const auto start = std::to_underlying(MaterialType::kGameStart);
		if (material == MaterialType::kInvalid || kind < start)
			return std::nullopt;
		return kind - start;
	}

	bool
	DrawsToonCharacter(MaterialType material, std::span<const SurfaceType> surfaces) noexcept
	{
		const std::optional<uint32_t> slot = GameSlot(material);
		return slot.has_value() && *slot < surfaces.size() &&
		       surfaces[*slot].shading == SurfaceShading::kToonCharacter;
	}

	bool
	DrawsWater(MaterialType material, std::span<const SurfaceType> surfaces) noexcept
	{
		const std::optional<uint32_t> slot = GameSlot(material);
		return slot.has_value() && *slot < surfaces.size() &&
		       surfaces[*slot].shading == SurfaceShading::kWater;
	}

	MaterialType
	GameSlotKind(uint32_t slot) noexcept
	{
		return static_cast<MaterialType>(std::to_underlying(MaterialType::kGameStart) + slot);
	}

	std::optional<uint32_t>
	CoverageCarrierSlot(const SurfaceParams& params) noexcept
	{
		const auto kindIs = [&params](const SurfaceTextureKind kind) {
			return std::ranges::find(params.textures, kind, &SurfaceTexture::kind);
		};

		if (const auto coverage = kindIs(SurfaceTextureKind::kCoverage);
		    coverage != params.textures.end())
			return coverage->index;

		if (const auto color = kindIs(SurfaceTextureKind::kColor); color != params.textures.end())
			return color->index;

		return std::nullopt;
	}

	bool
	AcceptsMaterial(
		const GeomType                     geomType,
		const MaterialHandle               material,
		const std::span<const SurfaceType> surfaces) noexcept
	{
		if (geomType == GeomType::kStaticMesh)
			return true;

		return material.IsValid() &&
		       (material.materialType == MaterialType::kPBR ||
		        GameSlot(material.materialType).has_value()) &&
		       !DrawsWater(material.materialType, surfaces);
	}

	bool
	HasMeshInstanceFlag(const idl::MeshInstance& instance, const MeshInstanceFlag flag) noexcept
	{
		return (instance.flags & std::to_underlying(flag)) != 0u;
	}

	void
	WriteInstanceTransform(idl::MeshInstance& instance, const glm::mat4& transform) noexcept
	{
		const glm::mat4 rows = glm::transpose(transform);

		instance.transform[0] = rows[0];
		instance.transform[1] = rows[1];
		instance.transform[2] = rows[2];
	}

	void
	WriteInstancePrevTransform(idl::MeshInstance& instance, const glm::mat4& transform) noexcept
	{
		const glm::mat4 rows = glm::transpose(transform);

		instance.prevTransform[0] = rows[0];
		instance.prevTransform[1] = rows[1];
		instance.prevTransform[2] = rows[2];
	}

	glm::mat4
	ReadInstanceTransform(const idl::MeshInstance& instance) noexcept
	{
		return glm::transpose(
			glm::mat4(
				instance.transform[0],
				instance.transform[1],
				instance.transform[2],
				glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
	}

	InstanceLodState
	UnpackInstanceLod(idl::InstanceLod word) noexcept
	{
		auto state = InstanceLodState();
		if ((word.packed & idl::cInstanceLodLevelMask) == 0u)
		{
			return state;
		}

		state.level     = static_cast<LodLevel>((word.packed & idl::cInstanceLodLevelMask) - 1u);
		state.fromTable = (word.packed & idl::cInstanceLodTableBit) != 0u;
		state.granted   = (word.packed & idl::cInstanceLodGrantBit) != 0u;

		const uint32_t outgoingByte = word.packed >> idl::cInstanceLodOutgoingShift;
		if (const uint32_t outgoing = outgoingByte & idl::cInstanceLodLevelMask; outgoing != 0u)
		{
			state.outgoing          = static_cast<LodLevel>(outgoing - 1u);
			state.outgoingFromTable = (outgoingByte & idl::cInstanceLodTableBit) != 0u;
			state.fade = static_cast<float>(word.packed >> idl::cInstanceLodFadeShift) /
			             idl::cInstanceLodFadeScale;
		}
		return state;
	}

	void
	ResolveLodSelection(
		idl::CullView&          cullView,
		const LodSelectionDesc& selection,
		const glm::vec3&        cameraPos,
		const float             pixelsPerUnit,
		const float             frameSeconds) noexcept
	{
		cullView.cameraPosAndPixelsPerUnit = glm::vec4(cameraPos, pixelsPerUnit);
		cullView.lodPixelScale             = selection.pixelScale;
		cullView.lodForcedLevel =
			selection.forceImpostor          ? idl::cLodForceImpostor :
			selection.forceLevel.has_value() ? static_cast<uint32_t>(*selection.forceLevel) :
											   idl::cLodForceNone;
		cullView.lodFadeStep = selection.fadeSeconds > 0.0f && frameSeconds > 0.0f ?
		                           frameSeconds / selection.fadeSeconds :
		                           1.0f;
		cullView.posePixels  = selection.posePixels;
		cullView.poseBudget  = selection.poseBudget;
		cullView.poseForced  = !selection.forcePoseSource.has_value() ? idl::cPoseForceNone :
		                       *selection.forcePoseSource == PoseSource::kBoneAnimTable ?
		                                                                idl::cPoseForceTable :
		                                                                idl::cPoseForcePerInstance;
	}
}
