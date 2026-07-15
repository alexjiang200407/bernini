#pragma once
#include "types/Color.h"

namespace bgl
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

		SamplerDesc&
		SetBorderColor(const Color& color)
		{
			borderColor = color;
			return *this;
		}

		SamplerDesc&
		SetMaxAnisotropy(float value)
		{
			maxAnisotropy = value;
			return *this;
		}

		SamplerDesc&
		SetMipBias(float value)
		{
			mipBias = value;
			return *this;
		}

		SamplerDesc&
		SetMinFilter(bool enable)
		{
			minFilter = enable;
			return *this;
		}

		SamplerDesc&
		SetMagFilter(bool enable)
		{
			magFilter = enable;
			return *this;
		}

		SamplerDesc&
		SetMipFilter(bool enable)
		{
			mipFilter = enable;
			return *this;
		}

		SamplerDesc&
		SetAllFilters(bool enable)
		{
			minFilter = magFilter = mipFilter = enable;
			return *this;
		}

		SamplerDesc&
		SetAddressU(SamplerAddressMode mode)
		{
			addressU = mode;
			return *this;
		}

		SamplerDesc&
		SetAddressV(SamplerAddressMode mode)
		{
			addressV = mode;
			return *this;
		}

		SamplerDesc&
		SetAddressW(SamplerAddressMode mode)
		{
			addressW = mode;
			return *this;
		}

		SamplerDesc&
		SetAllAddressModes(SamplerAddressMode mode)
		{
			addressU = addressV = addressW = mode;
			return *this;
		}

		SamplerDesc&
		SetReductionType(SamplerReductionType type)
		{
			reductionType = type;
			return *this;
		}
	};
}
