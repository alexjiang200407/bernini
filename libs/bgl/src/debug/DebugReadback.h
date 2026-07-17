#pragma once
#include "debug/DebugBuffer.h"
#include "idl/DebugRecord.h"
#include "idl/ErrorCode.h"

#if defined(BERNINI_GPU_DEBUG)
namespace bgl
{
	/**
	 * An errcode's name for the crash message, without the enum's `k` prefix. A code the enum does
	 * not carry is reported as its number rather than dropped -- a stale shader is exactly when this
	 * is being read.
	 *
	 * Deliberately a switch with no default, so adding an ErrorCode without a name fails the build.
	 *
	 * Not noexcept: the longer names do not fit a small-string buffer, so this allocates.
	 */
	[[nodiscard]] inline std::string
	ErrorCodeName(uint32_t errcode)
	{
		switch (static_cast<idl::ErrorCode>(errcode))
		{
		case idl::ErrorCode::kUnknown:
			return "Unknown";
		case idl::ErrorCode::kInvalidVertexLayout:
			return "InvalidVertexLayout";
		case idl::ErrorCode::kInvalidSubmeshIndex:
			return "InvalidSubmeshIndex";
		case idl::ErrorCode::kInvalidMeshletIndex:
			return "InvalidMeshletIndex";
		case idl::ErrorCode::kMeshletVertexOverflow:
			return "MeshletVertexOverflow";
		case idl::ErrorCode::kMeshletPrimitiveOverflow:
			return "MeshletPrimitiveOverflow";
		case idl::ErrorCode::kInvalidVertexIndex:
			return "InvalidVertexIndex";
		case idl::ErrorCode::kInvalidSubmeshInstance:
			return "InvalidSubmeshInstance";
		case idl::ErrorCode::kInvalidPsoType:
			return "InvalidPsoType";
		}

		return std::to_string(errcode);
	}

	// One decoded frame of GPU assertions read back from a DebugBuffer.
	struct DebugReport
	{
		// Total dbg_raise() calls this frame. May exceed records.size() if the buffer
		// overflowed -- excess records are dropped, not decoded.
		uint32_t count    = 0;
		bool     overflow = false;

		// Decoded records, capped at the buffer's capacity.
		std::vector<idl::DebugRecord> records;
	};

	/**
	 * Decodes a mapped DebugBuffer readback (see DebugBuffer for the word layout).
	 * Returns nullopt when no assertion fired this frame. Pure: no GPU work and no
	 * crash side effect, so the crash path can be unit-tested without terminating
	 * the process. `capacity` is the record capacity of the source buffer.
	 */
	[[nodiscard]] inline std::optional<DebugReport>
	InspectDebugReadback(const void* mapped, uint32_t capacity) noexcept
	{
		const auto* words = static_cast<const uint32_t*>(mapped);

		const uint32_t count    = words[DebugBuffer::kCounterWord];
		const uint32_t overflow = words[DebugBuffer::kOverflowWord];

		if (count == 0 && overflow == 0)
		{
			return std::nullopt;
		}

		DebugReport report;
		report.count    = count;
		report.overflow = overflow != 0;

		const uint32_t valid = std::min(count, capacity);
		report.records.reserve(valid);
		for (uint32_t i = 0; i < valid; ++i)
		{
			const uint32_t base = DebugBuffer::kHeaderWords + i * DebugBuffer::kRecordWords;

			idl::DebugRecord rec{};
			std::memcpy(&rec, words + base, sizeof(idl::DebugRecord));
			report.records.push_back(rec);
		}

		return report;
	}
}
#endif  // BERNINI_GPU_DEBUG
