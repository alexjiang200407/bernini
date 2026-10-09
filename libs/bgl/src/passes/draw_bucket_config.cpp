#include "passes/draw_bucket_config.h"
#include "gfx/DrawBucketTable.h"
#include "util/util.h"
#include <bgl/MaterialType.h>
#include <bgl/types/LayerType.h>
#include <bgpu/types/RasterState.h>
#include <core/err/util.h>
#include <format>
#include <string>
#include <string_view>

namespace bgl
{
	namespace
	{
		using namespace std::string_view_literals;

		// A program is named `programs.forward.<stem><layer suffix>` -- a file for an
		// engine kind, generated text for a surface's: one stem per material kind, one suffix per
		// layer.
		std::string
		ProgramStem(const MaterialType material)
		{
			if (const auto slot = GameSlot(material))
			{
				return std::format("GameSlot{}", *slot);
			}

			switch (material)
			{
			case MaterialType::kPBR:
				return "PBR";
			case MaterialType::kLoosePbr:
				return "PBR_Loose";
			case MaterialType::kNull:
				return "Null";
			case MaterialType::kAssert:
				return "Assert";
			case MaterialType::kGameStart:
			case MaterialType::kInvalid:
				break;
			}
			core::fatal("A draw bucket's material kind has no program stem");
		}

		std::string_view
		LayerSuffix(const LayerType layer) noexcept
		{
			switch (layer)
			{
			case LayerType::kMask:
				return "_AlphaTest"sv;
			case LayerType::kHashed:
				return "_HashedAlpha"sv;
			case LayerType::kOpaque:
			case LayerType::kBlend:
			case LayerType::kInvalid:
			case LayerType::kCount:
				break;
			}
			return ""sv;
		}
	}

	std::string
	DrawBucketPixelSrc(const DrawBucketDesc& desc)
	{
		core::ensure(
			desc.layer != LayerType::kBlend,
			"A transparent draw bucket owns no pixel program");

		// A blade shades from what the grass stage builds, which no mesh's program reads.
		if (desc.geom == GeometryStage::kGrass)
		{
			return std::format("programs.forward.Grass_{}", ProgramStem(desc.material));
		}

		return std::format(
			"programs.forward.{}{}",
			ProgramStem(desc.material),
			LayerSuffix(desc.layer));
	}

	std::string
	DrawBucketGroundColorSrc(const DrawBucketDesc& desc)
	{
		core::ensure(
			desc.geom == GeometryStage::kTerrain,
			"Only a terrain's bucket draws the ground colour");
		return std::format("programs.forward.GroundColor_{}", ProgramStem(desc.material));
	}

	std::string_view
	DrawBucketGeometrySrc(const DrawBucketDesc& desc)
	{
		core::ensure(
			desc.layer != LayerType::kBlend,
			"A transparent bucket owns no geometry program");
		switch (desc.geom)
		{
		case GeometryStage::kStaticMesh:
			return "programs.forward.StaticMesh"sv;
		case GeometryStage::kSkinnedMesh:
			return "programs.forward.SkinnedMesh"sv;
		case GeometryStage::kGrass:
			return "programs.forward.Grass"sv;
		case GeometryStage::kTerrain:
			return "programs.forward.Terrain"sv;
		}
		core::fatal("An unknown geometry stage");
	}

	uint32_t
	DrawBucketMeshStageCullsBackfaces(const DrawBucketDesc& desc) noexcept
	{
		return DrawBucketCullMode(desc) == bgpu::RasterCullMode::kNone ? 1u : 0u;
	}

	bgpu::RasterCullMode
	DrawBucketCullMode(const DrawBucketDesc& desc) noexcept
	{
		// A blade is seen from either side, and a material's doubleSided flag is a mesh's question.
		if (desc.geom == GeometryStage::kGrass)
		{
			return bgpu::RasterCullMode::kNone;
		}
		// A terrain is ground, seen from above: its back faces are culled in hardware whatever its
		// material says, and the stage reads no flag.
		if (desc.geom == GeometryStage::kTerrain)
		{
			return bgpu::RasterCullMode::kBack;
		}
		return desc.material == MaterialType::kNull || desc.material == MaterialType::kAssert ?
		           bgpu::RasterCullMode::kBack :
		           bgpu::RasterCullMode::kNone;
	}
}
