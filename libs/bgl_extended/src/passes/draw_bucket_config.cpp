#include "passes/draw_bucket_config.h"
#include "gfx/DrawBucketTable.h"
#include "types/RasterState.h"
#include "util/util.h"
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <bgl_common/gassert.h>
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
			gfatal("A draw bucket's material kind has no program stem");
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
		gassert(desc.layer != LayerType::kBlend, "A transparent draw bucket owns no pixel program");

		return std::format(
			"programs.forward.{}{}",
			ProgramStem(desc.material),
			LayerSuffix(desc.layer));
	}

	std::string_view
	DrawBucketGeometrySrc(const DrawBucketDesc& desc)
	{
		gassert(desc.layer != LayerType::kBlend, "A transparent bucket owns no geometry program");
		return desc.geom == GeomType::kSkinnedMesh ? "programs.forward.SkinnedMesh"sv :
		                                             "programs.forward.StaticMesh"sv;
	}

	uint32_t
	DrawBucketMeshStageCullsBackfaces(const DrawBucketDesc& desc) noexcept
	{
		return DrawBucketCullMode(desc) == RasterCullMode::kNone ? 1u : 0u;
	}

	RasterCullMode
	DrawBucketCullMode(const DrawBucketDesc& desc) noexcept
	{
		return desc.material == MaterialType::kNull || desc.material == MaterialType::kAssert ?
		           RasterCullMode::kBack :
		           RasterCullMode::kNone;
	}
}
