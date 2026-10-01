#pragma once

#include <cstdint>
#include <utility>
namespace bgpu
{
	enum class RasterFillMode : uint8_t
	{
		kSolid,
		kWireframe,
	};

	enum class RasterCullMode : uint8_t
	{
		kBack,
		kFront,
		kNone
	};

	struct RasterState
	{
		RasterFillMode fillMode              = RasterFillMode::kSolid;
		RasterCullMode cullMode              = RasterCullMode::kBack;
		bool           frontCounterClockwise = false;
		bool           depthClipEnable       = false;
		bool           scissorEnable         = false;
		bool           multisampleEnable     = false;
		bool           antialiasedLineEnable = false;
		int            depthBias             = 0;
		float          depthBiasClamp        = 0.f;
		float          slopeScaledDepthBias  = 0.f;

		uint8_t forcedSampleCount                 = 0;
		bool    programmableSamplePositionsEnable = false;
		bool    conservativeRasterEnable          = false;
		bool    quadFillEnable                    = false;
		char    samplePositionsX[16]{};
		char    samplePositionsY[16]{};

		template <typename Self>
		constexpr Self&&
		SetFillMode(this Self&& self, RasterFillMode value)
		{
			self.fillMode = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetFillSolid(this Self&& self)
		{
			self.fillMode = RasterFillMode::kSolid;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetFillWireframe(this Self&& self)
		{
			self.fillMode = RasterFillMode::kWireframe;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetCullMode(this Self&& self, RasterCullMode value)
		{
			self.cullMode = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetCullBack(this Self&& self)
		{
			self.cullMode = RasterCullMode::kBack;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetCullFront(this Self&& self)
		{
			self.cullMode = RasterCullMode::kFront;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetCullNone(this Self&& self)
		{
			self.cullMode = RasterCullMode::kNone;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetFrontCounterClockwise(this Self&& self, bool value)
		{
			self.frontCounterClockwise = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetDepthClipEnable(this Self&& self, bool value)
		{
			self.depthClipEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableDepthClip(this Self&& self)
		{
			self.depthClipEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableDepthClip(this Self&& self)
		{
			self.depthClipEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetScissorEnable(this Self&& self, bool value)
		{
			self.scissorEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableScissor(this Self&& self)
		{
			self.scissorEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableScissor(this Self&& self)
		{
			self.scissorEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetMultisampleEnable(this Self&& self, bool value)
		{
			self.multisampleEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableMultisample(this Self&& self)
		{
			self.multisampleEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableMultisample(this Self&& self)
		{
			self.multisampleEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetAntialiasedLineEnable(this Self&& self, bool value)
		{
			self.antialiasedLineEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableAntialiasedLine(this Self&& self)
		{
			self.antialiasedLineEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableAntialiasedLine(this Self&& self)
		{
			self.antialiasedLineEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetDepthBias(this Self&& self, int value)
		{
			self.depthBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetDepthBiasClamp(this Self&& self, float value)
		{
			self.depthBiasClamp = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetSlopeScaleDepthBias(this Self&& self, float value)
		{
			self.slopeScaledDepthBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetForcedSampleCount(this Self&& self, uint8_t value)
		{
			self.forcedSampleCount = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetProgrammableSamplePositionsEnable(this Self&& self, bool value)
		{
			self.programmableSamplePositionsEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableProgrammableSamplePositions(this Self&& self)
		{
			self.programmableSamplePositionsEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableProgrammableSamplePositions(this Self&& self)
		{
			self.programmableSamplePositionsEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetConservativeRasterEnable(this Self&& self, bool value)
		{
			self.conservativeRasterEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableConservativeRaster(this Self&& self)
		{
			self.conservativeRasterEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableConservativeRaster(this Self&& self)
		{
			self.conservativeRasterEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetQuadFillEnable(this Self&& self, bool value)
		{
			self.quadFillEnable = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		EnableQuadFill(this Self&& self)
		{
			self.quadFillEnable = true;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		DisableQuadFill(this Self&& self)
		{
			self.quadFillEnable = false;
			return std::forward<Self>(self);
		}

		template <typename Self>
		constexpr Self&&
		SetSamplePositions(this Self&& self, const char* x, const char* y, int count)
		{
			for (int i = 0; i < count; i++)
			{
				self.samplePositionsX[i] = x[i];
				self.samplePositionsY[i] = y[i];
			}
			return std::forward<Self>(self);
		}
	};
}
