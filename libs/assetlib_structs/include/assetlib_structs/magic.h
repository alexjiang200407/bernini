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
		/** The tag's bytes as the little-endian uint32 a reader compares against. */
		constexpr uint32_t
		fourCC(const char (&tag)[5])
		{
			return static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) |
			       static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 8 |
			       static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 16 |
			       static_cast<uint32_t>(static_cast<uint8_t>(tag[3])) << 24;
		}

		constexpr uint32_t c_BMesh     = fourCC("BMSH");
		constexpr uint32_t c_BMaterial = fourCC("BMAT");
		constexpr uint32_t c_BEnv      = fourCC("BENV");
		constexpr uint32_t c_BSky      = fourCC("BSKY");
		constexpr uint32_t c_BEnvL     = fourCC("BENL");
		constexpr uint32_t c_BSkel     = fourCC("BSKL");
		constexpr uint32_t c_BAnim     = fourCC("BANM");
		constexpr uint32_t c_BVat      = fourCC("BVAT");

		constexpr uint32_t c_BPak = fourCC("BPAK");
	}
}
