#pragma once
#include <catch2/catch_test_macros.hpp>

#include <core/file/file.h>

namespace assetlib::test
{
	// Offsets into cache_io's frozen Header -- frozen forever, which is what lets a test address
	// them by number.
	inline constexpr size_t c_TokenOffset      = 8;   // Header::bakeToken
	inline constexpr size_t c_SourceHashOffset = 32;  // Header::sourceHash

	/** Flips one byte of a cache entry's frozen header, staling or foreign-keying the entry. */
	inline void
	TamperHeaderByte(const std::filesystem::path& path, size_t offset)
	{
		auto bytes = core::file::read_file_bytes(path.string());
		REQUIRE(bytes.size() > offset);
		bytes[offset] ^= std::byte{ 1 };
		core::file::write_atomic(path, std::span<const std::byte>(bytes));
	}
}
