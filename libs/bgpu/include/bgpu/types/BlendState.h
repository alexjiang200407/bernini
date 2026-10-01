#pragma once
#include <bgpu/constants/constants.h>
#include <cstdint>
#include <utility>

namespace bgpu
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

			template <typename Self>
			constexpr Self&&
			SetBlendEnable(this Self&& self, bool enable)
			{
				self.blendEnable = enable;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			EnableBlend(this Self&& self)
			{
				self.blendEnable = true;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			DisableBlend(this Self&& self)
			{
				self.blendEnable = false;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetSrcBlend(this Self&& self, BlendFactor value)
			{
				self.srcBlend = value;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetDestBlend(this Self&& self, BlendFactor value)
			{
				self.destBlend = value;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetBlendOp(this Self&& self, BlendOp value)
			{
				self.blendOp = value;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetSrcBlendAlpha(this Self&& self, BlendFactor value)
			{
				self.srcBlendAlpha = value;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetDestBlendAlpha(this Self&& self, BlendFactor value)
			{
				self.destBlendAlpha = value;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetBlendOpAlpha(this Self&& self, BlendOp value)
			{
				self.blendOpAlpha = value;
				return std::forward<Self>(self);
			}

			template <typename Self>
			constexpr Self&&
			SetColorWriteMask(this Self&& self, ColorMask value)
			{
				self.colorWriteMask = value;
				return std::forward<Self>(self);
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

		template <typename Self>
		constexpr Self&&
		SetRenderTarget(this Self&& self, uint32_t index, const RenderTarget& target)
		{
			self.targets[index] = target;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetAlphaToCoverageEnable(this Self&& self, bool enable)
		{
			self.alphaToCoverageEnable = enable;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableAlphaToCoverage(this Self&& self)
		{
			self.alphaToCoverageEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableAlphaToCoverage(this Self&& self)
		{
			self.alphaToCoverageEnable = false;
			return std::forward<Self>(self);
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
