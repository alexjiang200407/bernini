#pragma once

#include <cstdint>
#include <utility>
namespace bgpu
{
	enum class StencilOp : uint8_t
	{
		kKeep              = 1,
		kZero              = 2,
		kReplace           = 3,
		kIncrementAndClamp = 4,
		kDecrementAndClamp = 5,
		kInvert            = 6,
		kIncrementAndWrap  = 7,
		kDecrementAndWrap  = 8
	};

	enum class ComparisonFunc : uint8_t
	{
		kNever          = 1,
		kLess           = 2,
		kEqual          = 3,
		kLessOrEqual    = 4,
		kGreater        = 5,
		kNotEqual       = 6,
		kGreaterOrEqual = 7,
		kAlways         = 8
	};

	struct DepthStencilState
	{
		struct StencilOpDesc
		{
			StencilOp      failOp      = StencilOp::kKeep;
			StencilOp      depthFailOp = StencilOp::kKeep;
			StencilOp      passOp      = StencilOp::kKeep;
			ComparisonFunc stencilFunc = ComparisonFunc::kAlways;

			template <typename Self>
			constexpr Self&&
			SetFailOp(this Self&& self, StencilOp value)
			{
				self.failOp = value;
				return std::forward<Self>(self);
			}
			template <typename Self>
			constexpr Self&&
			SetDepthFailOp(this Self&& self, StencilOp value)
			{
				self.depthFailOp = value;
				return std::forward<Self>(self);
			}
			template <typename Self>
			constexpr Self&&
			SetPassOp(this Self&& self, StencilOp value)
			{
				self.passOp = value;
				return std::forward<Self>(self);
			}
			template <typename Self>
			constexpr Self&&
			SetStencilFunc(this Self&& self, ComparisonFunc value)
			{
				self.stencilFunc = value;
				return std::forward<Self>(self);
			}
		};

		bool           depthTestEnable   = false;
		bool           depthWriteEnable  = true;
		ComparisonFunc depthFunc         = ComparisonFunc::kLess;
		bool           stencilEnable     = false;
		uint8_t        stencilReadMask   = 0xff;
		uint8_t        stencilWriteMask  = 0xff;
		uint8_t        stencilRefValue   = 0;
		bool           dynamicStencilRef = false;
		StencilOpDesc  frontFaceStencil;
		StencilOpDesc  backFaceStencil;

		template <typename Self>
		constexpr Self&&
		SetDepthTestEnable(this Self&& self, bool value)
		{
			self.depthTestEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableDepthTest(this Self&& self)
		{
			self.depthTestEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableDepthTest(this Self&& self)
		{
			self.depthTestEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetDepthWriteEnable(this Self&& self, bool value)
		{
			self.depthWriteEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableDepthWrite(this Self&& self)
		{
			self.depthWriteEnable = true;
			return std::forward<Self>(self);
		}
		template <typename Self>
		constexpr Self&&
		DisableDepthWrite(this Self&& self)
		{
			self.depthWriteEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetDepthFunc(this Self&& self, ComparisonFunc value)
		{
			self.depthFunc = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetStencilEnable(this Self&& self, bool value)
		{
			self.stencilEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableStencil(this Self&& self)
		{
			self.stencilEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableStencil(this Self&& self)
		{
			self.stencilEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetStencilReadMask(this Self&& self, uint8_t value)
		{
			self.stencilReadMask = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetStencilWriteMask(this Self&& self, uint8_t value)
		{
			self.stencilWriteMask = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetStencilRefValue(this Self&& self, uint8_t value)
		{
			self.stencilRefValue = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetFrontFaceStencil(this Self&& self, const StencilOpDesc& value)
		{
			self.frontFaceStencil = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetBackFaceStencil(this Self&& self, const StencilOpDesc& value)
		{
			self.backFaceStencil = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetDynamicStencilRef(this Self&& self, bool value)
		{
			self.dynamicStencilRef = value;
			return std::forward<Self>(self);
		}
	};
}
