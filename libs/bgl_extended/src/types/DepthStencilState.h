#pragma once

namespace bgl
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

			constexpr StencilOpDesc&
			SetFailOp(StencilOp value)
			{
				failOp = value;
				return *this;
			}
			constexpr StencilOpDesc&
			SetDepthFailOp(StencilOp value)
			{
				depthFailOp = value;
				return *this;
			}
			constexpr StencilOpDesc&
			SetPassOp(StencilOp value)
			{
				passOp = value;
				return *this;
			}
			constexpr StencilOpDesc&
			SetStencilFunc(ComparisonFunc value)
			{
				stencilFunc = value;
				return *this;
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

		constexpr DepthStencilState&
		SetDepthTestEnable(bool value)
		{
			depthTestEnable = value;
			return *this;
		}

		constexpr DepthStencilState&
		EnableDepthTest()
		{
			depthTestEnable = true;
			return *this;
		}

		constexpr DepthStencilState&
		DisableDepthTest()
		{
			depthTestEnable = false;
			return *this;
		}

		constexpr DepthStencilState&
		SetDepthWriteEnable(bool value)
		{
			depthWriteEnable = value;
			return *this;
		}

		constexpr DepthStencilState&
		EnableDepthWrite()
		{
			depthWriteEnable = true;
			return *this;
		}
		constexpr DepthStencilState&
		DisableDepthWrite()
		{
			depthWriteEnable = false;
			return *this;
		}

		constexpr DepthStencilState&
		SetDepthFunc(ComparisonFunc value)
		{
			depthFunc = value;
			return *this;
		}

		constexpr DepthStencilState&
		SetStencilEnable(bool value)
		{
			stencilEnable = value;
			return *this;
		}

		constexpr DepthStencilState&
		EnableStencil()
		{
			stencilEnable = true;
			return *this;
		}

		constexpr DepthStencilState&
		DisableStencil()
		{
			stencilEnable = false;
			return *this;
		}

		constexpr DepthStencilState&
		SetStencilReadMask(uint8_t value)
		{
			stencilReadMask = value;
			return *this;
		}

		constexpr DepthStencilState&
		SetStencilWriteMask(uint8_t value)
		{
			stencilWriteMask = value;
			return *this;
		}

		constexpr DepthStencilState&
		SetStencilRefValue(uint8_t value)
		{
			stencilRefValue = value;
			return *this;
		}

		constexpr DepthStencilState&
		SetFrontFaceStencil(const StencilOpDesc& value)
		{
			frontFaceStencil = value;
			return *this;
		}

		constexpr DepthStencilState&
		SetBackFaceStencil(const StencilOpDesc& value)
		{
			backFaceStencil = value;
			return *this;
		}

		constexpr DepthStencilState&
		SetDynamicStencilRef(bool value)
		{
			dynamicStencilRef = value;
			return *this;
		}
	};
}
