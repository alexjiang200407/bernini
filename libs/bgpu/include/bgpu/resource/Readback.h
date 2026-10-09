#pragma once
#include <core/containers/slot_handle.h>
#include <cstdint>
#include <string>

namespace bgpu
{
	class ReadbackBuffer;

	struct ReadbackBufferDesc
	{
		uint64_t    byteSize = 0;
		std::string debugName;
	};

	struct ReadbackBufferHandle
	{
		core::slot_handle slot;

		[[nodiscard]] bool
		IsNull() const
		{
			return slot.is_null();
		}
	};

	struct TextureReadbackLayout
	{
		uint64_t offset       = 0;
		uint64_t rowPitch     = 0;  // stride between rows: the backend's, never below rowSizeBytes
		uint64_t rowSizeBytes = 0;  // tight bytes per row (no padding)
		uint32_t rowCount     = 0;
		uint64_t totalBytes   = 0;
	};
}
