#pragma once

namespace bgl::test
{
	/** Decodes one IEEE 754 binary16, as an RG16_FLOAT texture reads back. */
	[[nodiscard]] inline float
	HalfToFloat(uint16_t bits)
	{
		const uint32_t sign     = static_cast<uint32_t>(bits & 0x8000u) << 16;
		const uint32_t exponent = (bits >> 10) & 0x1Fu;
		const uint32_t mantissa = bits & 0x3FFu;

		if (exponent == 0)
		{
			if (mantissa == 0)
			{
				return std::bit_cast<float>(sign);
			}

			// Subnormal: renormalize by shifting the mantissa up until its leading bit falls out.
			uint32_t e = 1;
			uint32_t m = mantissa;
			while ((m & 0x400u) == 0)
			{
				m <<= 1;
				++e;
			}
			m &= 0x3FFu;
			return std::bit_cast<float>(sign | ((127 - 15 - e + 1) << 23) | (m << 13));
		}

		if (exponent == 0x1Fu)
		{
			return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
		}

		return std::bit_cast<float>(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));
	}
}
