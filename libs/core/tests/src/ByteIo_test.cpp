#include <catch2/catch_test_macros.hpp>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

TEST_CASE("A string and a blob read back as written, between the pods around them", "[io]")
{
	const std::vector<std::byte> blob = { std::byte{ 0 }, std::byte{ 0xFF }, std::byte{ 7 } };

	auto writer = core::io::ByteWriter();
	writer.WritePod<uint32_t>(0xA11CE);
	writer.WriteString("programs.forward.Opaque");
	writer.WriteString("");
	writer.WriteBlob(blob);
	writer.WritePod<uint16_t>(42);
	const std::vector<std::byte> bytes = writer.Take();

	auto reader = core::io::ByteReader(bytes);
	CHECK(reader.ReadPod<uint32_t>() == 0xA11CE);
	CHECK(reader.ReadString() == "programs.forward.Opaque");
	CHECK(reader.ReadString().empty());
	CHECK(reader.ReadBlob() == blob);
	CHECK(reader.ReadPod<uint16_t>() == 42);
	CHECK(reader.Remaining() == 0);
}

// A length prefix read from a torn file can claim anything; the reader must refuse it rather than
// read past the end of the stream.
TEST_CASE("A string whose length runs past the end of the stream throws", "[io]")
{
	auto writer = core::io::ByteWriter();
	writer.WriteString("truncated");
	std::vector<std::byte> bytes = writer.Take();
	bytes.resize(bytes.size() - 1);

	auto reader = core::io::ByteReader(bytes);
	CHECK_THROWS_AS(reader.ReadString(), std::runtime_error);

	auto blobReader = core::io::ByteReader(bytes);
	CHECK_THROWS_AS(blobReader.ReadBlob(), std::runtime_error);
}
