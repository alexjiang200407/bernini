#pragma once
#include "types/Color.h"
#include "types/Format.h"

namespace bgl
{
	struct DepthStencilClearValue
	{
		float   depth   = 1.0f;
		uint8_t stencil = 0;
	};

	struct ClearValue
	{
		Format format = Format::UNKNOWN;

		std::variant<Color, DepthStencilClearValue> value = Color(0.0f, 0.0f, 0.0f, 1.0f);

		bool
		IsColor() const noexcept
		{
			return std::holds_alternative<Color>(value);
		}

		bool
		IsDepthStencil() const noexcept
		{
			return std::holds_alternative<DepthStencilClearValue>(value);
		}

		const Color&
		GetColor() const
		{
			return std::get<Color>(value);
		}

		const DepthStencilClearValue&
		GetDepthStencil() const
		{
			return std::get<DepthStencilClearValue>(value);
		}

		ClearValue&
		SetColor(Color color)
		{
			value = std::move(color);
			return *this;
		}

		ClearValue&
		SetDepthStencil(float depth, uint8_t stencil)
		{
			value = DepthStencilClearValue{ depth, stencil };
			return *this;
		}
	};
}
