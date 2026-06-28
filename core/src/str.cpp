#include <core/str/str.h>

namespace core::str
{
	size_t
	toUtf32(std::span<char16_t> utf16, std::span<char32_t> utf32)
	{
		size_t retCnt = 0;
		size_t i      = 0;
		while (i < utf16.size() && retCnt < utf32.size())
		{
			char32_t codepoint;
			char16_t current = utf16[i];

			if (current >= 0xD800 && current <= 0xDBFF && (i + 1) < utf16.size())
			{
				// high surrogate + low surrogate
				char16_t high = current;
				char16_t low  = utf16[i + 1];
				codepoint     = static_cast<char32_t>((high - 0xD800) << 10) +
				                static_cast<char32_t>(low - 0xDC00) + 0x10000;
				i += 2;
			}
			else
			{
				codepoint = current;
				++i;
			}

			utf32[retCnt++] = codepoint;
		}

		return retCnt;
	}

}
