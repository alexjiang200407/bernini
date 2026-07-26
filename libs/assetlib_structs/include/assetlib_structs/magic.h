#pragma once

namespace assetlib
{
	/**
	 * The four-byte tag every container this library writes opens with.
	 *
	 * One place, because these are matched by readers that do not otherwise know about each other:
	 * `sniff` in the CLI decides which of two containers it holds by comparing against two of them,
	 * and each `*_io.cpp` compares against its own. Spelled out per file, a new container could
	 * silently reuse a tag.
	 */
	namespace magic
	{
		constexpr uint32_t c_BMesh     = 0x48534D42u;  // 'B','M','S','H' little-endian
		constexpr uint32_t c_BMaterial = 0x54414D42u;  // 'B','M','A','T'
		constexpr uint32_t c_BEnv      = 0x564E4542u;  // 'B','E','N','V'
	}
}
