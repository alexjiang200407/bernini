#pragma once
#include "constants/constants.h"

namespace bgl
{
	enum class BlendFactor : uint8_t
	{
		kZero             = 1,
		kOne              = 2,
		kSrcColor         = 3,
		kInvSrcColor      = 4,
		kSrcAlpha         = 5,
		kInvSrcAlpha      = 6,
		kDstAlpha         = 7,
		kInvDstAlpha      = 8,
		kDstColor         = 9,
		kInvDstColor      = 10,
		kSrcAlphaSaturate = 11,
		kConstantColor    = 14,
		kInvConstantColor = 15,
		kSrc1Color        = 16,
		kInvSrc1Color     = 17,
		kSrc1Alpha        = 18,
		kInvSrc1Alpha     = 19,
	};

	enum class BlendOp : uint8_t
	{
		kAdd             = 1,
		kSubtract        = 2,
		kReverseSubtract = 3,
		kMin             = 4,
		kMax             = 5
	};

	enum class ColorMask : uint8_t
	{
		kRed   = 1,
		kGreen = 2,
		kBlue  = 4,
		kAlpha = 8,
		kAll   = 0xF
	};

	struct BlendState
	{
		struct RenderTarget
		{
			bool        blendEnable    = false;
			BlendFactor srcBlend       = BlendFactor::kOne;
			BlendFactor destBlend      = BlendFactor::kZero;
			BlendOp     blendOp        = BlendOp::kAdd;
			BlendFactor srcBlendAlpha  = BlendFactor::kOne;
			BlendFactor destBlendAlpha = BlendFactor::kZero;
			BlendOp     blendOpAlpha   = BlendOp::kAdd;
			ColorMask   colorWriteMask = ColorMask::kAll;

			constexpr RenderTarget&
			SetBlendEnable(bool enable)
			{
				blendEnable = enable;
				return *this;
			}

			constexpr RenderTarget&
			EnableBlend()
			{
				blendEnable = true;
				return *this;
			}

			constexpr RenderTarget&
			DisableBlend()
			{
				blendEnable = false;
				return *this;
			}

			constexpr RenderTarget&
			SetSrcBlend(BlendFactor value)
			{
				srcBlend = value;
				return *this;
			}

			constexpr RenderTarget&
			SetDestBlend(BlendFactor value)
			{
				destBlend = value;
				return *this;
			}

			constexpr RenderTarget&
			SetBlendOp(BlendOp value)
			{
				blendOp = value;
				return *this;
			}

			constexpr RenderTarget&
			SetSrcBlendAlpha(BlendFactor value)
			{
				srcBlendAlpha = value;
				return *this;
			}

			constexpr RenderTarget&
			SetDestBlendAlpha(BlendFactor value)
			{
				destBlendAlpha = value;
				return *this;
			}

			constexpr RenderTarget&
			SetBlendOpAlpha(BlendOp value)
			{
				blendOpAlpha = value;
				return *this;
			}

			constexpr RenderTarget&
			SetColorWriteMask(ColorMask value)
			{
				colorWriteMask = value;
				return *this;
			}

			constexpr bool
			operator==(const RenderTarget& other) const
			{
				return blendEnable == other.blendEnable && srcBlend == other.srcBlend &&
				       destBlend == other.destBlend && blendOp == other.blendOp &&
				       srcBlendAlpha == other.srcBlendAlpha &&
				       destBlendAlpha == other.destBlendAlpha &&
				       blendOpAlpha == other.blendOpAlpha && colorWriteMask == other.colorWriteMask;
			}

			constexpr bool
			operator!=(const RenderTarget& other) const
			{
				return !(*this == other);
			}
		};

		RenderTarget targets[c_MaxRenderTargets]{};
		bool         alphaToCoverageEnable = false;

		constexpr BlendState&
		SetRenderTarget(uint32_t index, const RenderTarget& target)
		{
			targets[index] = target;
			return *this;
		}

		constexpr BlendState&
		SetAlphaToCoverageEnable(bool enable)
		{
			alphaToCoverageEnable = enable;
			return *this;
		}

		constexpr BlendState&
		EnableAlphaToCoverage()
		{
			alphaToCoverageEnable = true;
			return *this;
		}

		constexpr BlendState&
		DisableAlphaToCoverage()
		{
			alphaToCoverageEnable = false;
			return *this;
		}

		constexpr bool
		operator==(const BlendState& other) const
		{
			if (alphaToCoverageEnable != other.alphaToCoverageEnable)
				return false;

			for (uint32_t i = 0; i < c_MaxRenderTargets; ++i)
			{
				if (targets[i] != other.targets[i])
					return false;
			}

			return true;
		}

		constexpr bool
		operator!=(const BlendState& other) const
		{
			return !(*this == other);
		}
	};
}
