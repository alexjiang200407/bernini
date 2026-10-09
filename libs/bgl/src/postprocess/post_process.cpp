#include "postprocess/post_process.h"
#include <bgl/IGraphics.h>
#include <bgl/types/BloomSettings.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/FilmGrainSettings.h>
#include <bgl/types/PostProcess.h>
#include <bgl/types/VignetteSettings.h>
#include <cmath>
#include <core/glm.h>
#include <format>
#include <optional>
#include <string_view>

namespace bgl
{
	namespace
	{
		/** Checks one settings struct's fields, naming the struct in what it throws. */
		class Fields
		{
		public:
			explicit Fields(std::string_view owner) noexcept : m_Owner(owner) {}

			// Written as `!(lo <= v && v <= hi)` so a NaN fails it.
			void
			Within(std::string_view field, float value, float lo, float hi) const
			{
				if (!(lo <= value && value <= hi))
					Throw(field, std::format("within [{}, {}]", lo, hi));
			}

			void
			NonNegative(std::string_view field, float value) const
			{
				if (!(value >= 0.0f) || !std::isfinite(value))
					Throw(field, "non-negative and finite");
			}

			void
			Positive(std::string_view field, float value) const
			{
				if (!(value > 0.0f) || !std::isfinite(value))
					Throw(field, "positive and finite");
			}

			void
			Finite(std::string_view field, float value) const
			{
				if (!std::isfinite(value))
					Throw(field, "finite");
			}

			void
			Vignette(const VignetteSettings& vignette) const
			{
				Within("vignette.intensity", vignette.intensity, 0.0f, 1.0f);
				if (!(vignette.smoothness > 0.0f && vignette.smoothness <= 1.0f))
					Throw("vignette.smoothness", "within (0, 1]");
			}

		private:
			[[noreturn]] void
			Throw(std::string_view field, std::string_view rule) const
			{
				throw GraphicsError(std::format("{}::{} must be {}", m_Owner, field, rule));
			}

			std::string_view m_Owner;
		};

		void
		Validate(const BloomSettings& s)
		{
			const auto check = Fields("BloomSettings");
			check.NonNegative("intensity", s.intensity);
			check.NonNegative("threshold", s.threshold);
			check.Within("softKnee", s.softKnee, 0.0f, 1.0f);
			check.Within("scatter", s.scatter, 0.0f, 1.0f);
		}

		void
		Validate(const ColorGradeSettings& s)
		{
			const auto check = Fields("ColorGradeSettings");
			check.Within("temperature", s.temperature, -100.0f, 100.0f);
			check.Within("tint", s.tint, -100.0f, 100.0f);
			for (int c = 0; c < 3; ++c)
			{
				check.NonNegative("slope", s.slope[c]);
				check.Within("offset", s.offset[c], -1.0f, 1.0f);
				check.Positive("power", s.power[c]);
			}
			check.NonNegative("saturation", s.saturation);
			check.NonNegative("contrast", s.contrast);
			check.Vignette(s.vignette);
		}

		void
		Validate(const FilmGrainSettings& s)
		{
			const auto check = Fields("FilmGrainSettings");
			check.Within("intensity", s.intensity, 0.0f, 1.0f);
			check.Positive("size", s.size);
		}

		void
		Validate(const ColorSplitSettings& s)
		{
			const auto check = Fields("ColorSplitSettings");
			check.Finite("offset", s.offset.x);
			check.Finite("offset", s.offset.y);
			check.Finite("radial", s.radial);
		}

		template <typename T>
		void
		ValidateIfSet(const std::optional<T>& settings)
		{
			if (settings)
				Validate(*settings);
		}
	}

	void
	ValidatePostProcess(const PostProcess& postProcess)
	{
		ValidateIfSet(postProcess.bloom);
		ValidateIfSet(postProcess.grade);
		ValidateIfSet(postProcess.grain);
		ValidateIfSet(postProcess.split);
	}
}
