#pragma once
#include <bgpu/types/Color.h>
#include <core/containers/slot_handle.h>
#include <cstdint>
#include <utility>

namespace bgpu
{
	class Sampler;
	class ResourceManager;

	enum class SamplerAddressMode : uint8_t
	{
		kClamp,
		kWrap,
		kBorder,
		kMirror,
		kMirrorOnce
	};

	enum class SamplerReductionType : uint8_t
	{
		kStandard,
		kComparison,
		kMinimum,
		kMaximum
	};

	struct SamplerHandle
	{
		uint32_t idx        = 0xFFFFFFFF;
		uint32_t generation = 0;

		// What a shader must find in a constant buffer to reach this resource. The backend that
		// created the handle decides it: a descriptor-heap index on D3D12, the pool slot Metal's
		// dispatch rewrite looks the resource up by. Null until a resource manager hands one out.
		uint32_t bindlessIndex = core::slot_handle::invalid_index;

		[[nodiscard]] bool
		IsNull() const
		{
			return idx == 0xFFFFFFFF;
		}
	};

	struct SamplerDesc
	{
		Color borderColor   = 1.f;
		float maxAnisotropy = 1.f;
		float mipBias       = 0.f;

		bool                 minFilter     = true;
		bool                 magFilter     = true;
		bool                 mipFilter     = true;
		SamplerAddressMode   addressU      = SamplerAddressMode::kClamp;
		SamplerAddressMode   addressV      = SamplerAddressMode::kClamp;
		SamplerAddressMode   addressW      = SamplerAddressMode::kClamp;
		SamplerReductionType reductionType = SamplerReductionType::kStandard;

		template <typename Self>
		Self&&
		SetBorderColor(this Self&& self, const Color& color)
		{
			self.borderColor = color;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxAnisotropy(this Self&& self, float value)
		{
			self.maxAnisotropy = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMipBias(this Self&& self, float value)
		{
			self.mipBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinFilter(this Self&& self, bool enable)
		{
			self.minFilter = enable;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMagFilter(this Self&& self, bool enable)
		{
			self.magFilter = enable;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMipFilter(this Self&& self, bool enable)
		{
			self.mipFilter = enable;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAllFilters(this Self&& self, bool enable)
		{
			self.minFilter = self.magFilter = self.mipFilter = enable;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAddressU(this Self&& self, SamplerAddressMode mode)
		{
			self.addressU = mode;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAddressV(this Self&& self, SamplerAddressMode mode)
		{
			self.addressV = mode;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAddressW(this Self&& self, SamplerAddressMode mode)
		{
			self.addressW = mode;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAllAddressModes(this Self&& self, SamplerAddressMode mode)
		{
			self.addressU = self.addressV = self.addressW = mode;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetReductionType(this Self&& self, SamplerReductionType type)
		{
			self.reductionType = type;
			return std::forward<Self>(self);
		}
	};
}
