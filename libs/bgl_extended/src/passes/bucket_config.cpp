#include "passes/bucket_config.h"
#include "gfx/BucketTable.h"
#include "types/RasterState.h"
#include "util/util.h"
#include <array>
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <bgl_common/gassert.h>
#include <cstdint>
#include <string_view>

namespace bgl
{
	namespace
	{
		using namespace std::string_view_literals;

		// A program is a file with an entry point, so each reserved slot is one triple, plus the
		// depth-only twins of its coverage layers. Hand-written until the wrapper generation task.
		struct GameSlotSrcs
		{
			std::string_view opaque;
			std::string_view cutout;
			std::string_view hashed;
			std::string_view cutoutDepth;
			std::string_view hashedDepth;
		};

		constexpr std::array<GameSlotSrcs, cGameSlots> c_GameSlotSrcs = { {
			{ "programs.forward.GameSlot0"sv,
			  "programs.forward.GameSlot0_AlphaTest"sv,
			  "programs.forward.GameSlot0_HashedAlpha"sv,
			  "programs.forward.DepthOnly_GameSlot0_AlphaTest"sv,
			  "programs.forward.DepthOnly_GameSlot0_HashedAlpha"sv },
			{ "programs.forward.GameSlot1"sv,
			  "programs.forward.GameSlot1_AlphaTest"sv,
			  "programs.forward.GameSlot1_HashedAlpha"sv,
			  "programs.forward.DepthOnly_GameSlot1_AlphaTest"sv,
			  "programs.forward.DepthOnly_GameSlot1_HashedAlpha"sv },
			{ "programs.forward.GameSlot2"sv,
			  "programs.forward.GameSlot2_AlphaTest"sv,
			  "programs.forward.GameSlot2_HashedAlpha"sv,
			  "programs.forward.DepthOnly_GameSlot2_AlphaTest"sv,
			  "programs.forward.DepthOnly_GameSlot2_HashedAlpha"sv },
			{ "programs.forward.GameSlot3"sv,
			  "programs.forward.GameSlot3_AlphaTest"sv,
			  "programs.forward.GameSlot3_HashedAlpha"sv,
			  "programs.forward.DepthOnly_GameSlot3_AlphaTest"sv,
			  "programs.forward.DepthOnly_GameSlot3_HashedAlpha"sv },
		} };
	}

	std::string_view
	BucketPixelSrc(const BucketDesc& desc)
	{
		gassert(desc.layer != LayerType::kBlend, "A transparent bucket owns no pixel program");

		const bool cutout = desc.layer == LayerType::kMask;
		const bool hashed = desc.layer == LayerType::kHashed;

		if (const auto slot = GameSlot(desc.material))
		{
			const GameSlotSrcs& srcs = c_GameSlotSrcs[*slot];
			return cutout ? srcs.cutout : hashed ? srcs.hashed : srcs.opaque;
		}

		switch (desc.material)
		{
		case MaterialType::kPBR:
			return cutout ? "programs.forward.PBR_AlphaTest"sv :
			       hashed ? "programs.forward.PBR_HashedAlpha"sv :
			                "programs.forward.PBR"sv;
		case MaterialType::kLoosePbr:
			return cutout ? "programs.forward.PBR_Loose_AlphaTest"sv :
			       hashed ? "programs.forward.PBR_Loose_HashedAlpha"sv :
			                "programs.forward.PBR_Loose"sv;

		// Neither shades a base color, so there is no alpha for a coverage layer to read.
		case MaterialType::kNull:
			return "programs.forward.Null"sv;
		case MaterialType::kAssert:
			return "programs.forward.Assert"sv;

		case MaterialType::kGameStart:
		case MaterialType::kInvalid:
		case MaterialType::kCount:
			break;
		}
		gfatal("A bucket's material kind has no pixel program");
	}

	std::string_view
	BucketGeometrySrc(const BucketDesc& desc)
	{
		gassert(desc.layer != LayerType::kBlend, "A transparent bucket owns no geometry program");
		return desc.geom == GeomType::kSkinnedMesh ? "programs.forward.SkinnedMesh"sv :
		                                             "programs.forward.StaticMesh"sv;
	}

	std::string_view
	BucketCoveragePixelSrc(const BucketDesc& desc)
	{
		gassert(
			desc.geom == GeomType::kStaticMesh &&
				(desc.layer == LayerType::kMask || desc.layer == LayerType::kHashed),
			"Coverage twins exist for static cutout and hashed buckets alone");

		const bool cutout = desc.layer == LayerType::kMask;

		if (const auto slot = GameSlot(desc.material))
		{
			const GameSlotSrcs& srcs = c_GameSlotSrcs[*slot];
			return cutout ? srcs.cutoutDepth : srcs.hashedDepth;
		}

		switch (desc.material)
		{
		case MaterialType::kPBR:
			return cutout ? "programs.forward.DepthOnly_PBR_AlphaTest"sv :
			                "programs.forward.DepthOnly_PBR_HashedAlpha"sv;
		case MaterialType::kLoosePbr:
			return cutout ? "programs.forward.DepthOnly_PBR_Loose_AlphaTest"sv :
			                "programs.forward.DepthOnly_PBR_Loose_HashedAlpha"sv;
		case MaterialType::kNull:
		case MaterialType::kAssert:
		case MaterialType::kGameStart:
		case MaterialType::kInvalid:
		case MaterialType::kCount:
			break;
		}
		gfatal("A bucket's material kind has no coverage twin");
	}

	RasterCullMode
	BucketCullMode(const BucketDesc& desc) noexcept
	{
		return desc.material == MaterialType::kNull || desc.material == MaterialType::kAssert ?
		           RasterCullMode::kBack :
		           RasterCullMode::kNone;
	}
}
