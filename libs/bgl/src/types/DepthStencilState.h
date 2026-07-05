#pragma once

namespace bgl
{
	enum class StencilOp : uint8_t
	{
		Keep              = 1,
		Zero              = 2,
		Replace           = 3,
		IncrementAndClamp = 4,
		DecrementAndClamp = 5,
		Invert            = 6,
		IncrementAndWrap  = 7,
		DecrementAndWrap  = 8
	};

	enum class ComparisonFunc : uint8_t
	{
		Never          = 1,
		Less           = 2,
		Equal          = 3,
		LessOrEqual    = 4,
		Greater        = 5,
		NotEqual       = 6,
		GreaterOrEqual = 7,
		Always         = 8
	};

	struct DepthStencilState
	{
		struct StencilOpDesc
		{
			StencilOp      failOp      = StencilOp::Keep;
			StencilOp      depthFailOp = StencilOp::Keep;
			StencilOp      passOp      = StencilOp::Keep;
			ComparisonFunc stencilFunc = ComparisonFunc::Always;

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
		ComparisonFunc depthFunc         = ComparisonFunc::Less;
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
