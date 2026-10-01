#pragma once
#include <bgpu/types/Color.h>
#include <bgpu/types/Format.h>
#include <cstdint>
#include <utility>
#include <variant>

namespace bgpu
{
	struct DepthStencilClearValue
	{
		float   depth   = 1.0f;
		uint8_t stencil = 0;
	};

	struct ClearValue
	{
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

		template <typename Self>
		Self&&
		SetColor(this Self&& self, Color color)
		{
			self.value = std::move(color);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepthStencil(this Self&& self, float depth, uint8_t stencil)
		{
			self.value = DepthStencilClearValue{ depth, stencil };
			return std::forward<Self>(self);
		}
	};
}
