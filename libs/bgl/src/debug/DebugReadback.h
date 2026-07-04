#pragma once
#include "debug/DebugBuffer.h"
#include "idl/DebugRecord.h"

#if defined(BERNINI_GPU_DEBUG)
namespace bgl
{
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
