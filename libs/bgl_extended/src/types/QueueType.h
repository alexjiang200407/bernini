#pragma once

#include <cstdint>
namespace bgl
{
	enum class QueueType : uint8_t
	{
		kGraphics,
		kCompute,
		kCopy,
	};
}
