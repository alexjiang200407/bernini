#pragma once

#include <cstdint>
namespace bgpu
{
	enum class QueueType : uint8_t
	{
		kGraphics,
		kCompute,
		kCopy,
	};
}
