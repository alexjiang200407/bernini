#pragma once

namespace bgl
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

		constexpr RasterState&
		SetFillMode(RasterFillMode value)
		{
			fillMode = value;
			return *this;
		}

		constexpr RasterState&
		SetFillSolid()
		{
			fillMode = RasterFillMode::kSolid;
			return *this;
		}

		constexpr RasterState&
		SetFillWireframe()
		{
			fillMode = RasterFillMode::kWireframe;
			return *this;
		}

		constexpr RasterState&
		SetCullMode(RasterCullMode value)
		{
			cullMode = value;
			return *this;
		}

		constexpr RasterState&
		SetCullBack()
		{
			cullMode = RasterCullMode::kBack;
			return *this;
		}

		constexpr RasterState&
		SetCullFront()
		{
			cullMode = RasterCullMode::kFront;
			return *this;
		}

		constexpr RasterState&
		SetCullNone()
		{
			cullMode = RasterCullMode::kNone;
			return *this;
		}

		constexpr RasterState&
		SetFrontCounterClockwise(bool value)
		{
			frontCounterClockwise = value;
			return *this;
		}

		constexpr RasterState&
		SetDepthClipEnable(bool value)
		{
			depthClipEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableDepthClip()
		{
			depthClipEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableDepthClip()
		{
			depthClipEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetScissorEnable(bool value)
		{
			scissorEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableScissor()
		{
			scissorEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableScissor()
		{
			scissorEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetMultisampleEnable(bool value)
		{
			multisampleEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableMultisample()
		{
			multisampleEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableMultisample()
		{
			multisampleEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetAntialiasedLineEnable(bool value)
		{
			antialiasedLineEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableAntialiasedLine()
		{
			antialiasedLineEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableAntialiasedLine()
		{
			antialiasedLineEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetDepthBias(int value)
		{
			depthBias = value;
			return *this;
		}

		constexpr RasterState&
		SetDepthBiasClamp(float value)
		{
			depthBiasClamp = value;
			return *this;
		}

		constexpr RasterState&
		SetSlopeScaleDepthBias(float value)
		{
			slopeScaledDepthBias = value;
			return *this;
		}

		constexpr RasterState&
		SetForcedSampleCount(uint8_t value)
		{
			forcedSampleCount = value;
			return *this;
		}

		constexpr RasterState&
		SetProgrammableSamplePositionsEnable(bool value)
		{
			programmableSamplePositionsEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableProgrammableSamplePositions()
		{
			programmableSamplePositionsEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableProgrammableSamplePositions()
		{
			programmableSamplePositionsEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetConservativeRasterEnable(bool value)
		{
			conservativeRasterEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableConservativeRaster()
		{
			conservativeRasterEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableConservativeRaster()
		{
			conservativeRasterEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetQuadFillEnable(bool value)
		{
			quadFillEnable = value;
			return *this;
		}

		constexpr RasterState&
		EnableQuadFill()
		{
			quadFillEnable = true;
			return *this;
		}

		constexpr RasterState&
		DisableQuadFill()
		{
			quadFillEnable = false;
			return *this;
		}

		constexpr RasterState&
		SetSamplePositions(const char* x, const char* y, int count)
		{
			for (int i = 0; i < count; i++)
			{
				samplePositionsX[i] = x[i];
				samplePositionsY[i] = y[i];
			}
			return *this;
		}
	};
}
