#pragma once

namespace bgl
{
	class ReadbackBuffer;

	struct ReadbackBufferDesc
	{
		uint64_t    byteSize = 0;
		std::string debugName;
	};

	struct ReadbackBufferHandle
	{
		uint32_t idx        = 0xFFFFFFFF;
		uint32_t generation = 0;

		[[nodiscard]] bool
		IsNull() const
		{
			return idx == 0xFFFFFFFF;
		}
	};

	struct TextureReadbackLayout
	{
		uint64_t offset       = 0;
		uint64_t rowPitch     = 0;  // padded bytes per row (256-aligned)
		uint64_t rowSizeBytes = 0;  // tight bytes per row (no padding)
		uint32_t rowCount     = 0;
		uint64_t totalBytes   = 0;
	};
}
