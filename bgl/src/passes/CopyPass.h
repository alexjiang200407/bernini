#pragma once
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"

namespace bgl
{
	// Records GPU copies into a set of destination buffers.
	class CopyPass
	{
	public:
		void
		AttachToFrameGraph(
			FrameGraph&                       fg,
			std::string                       name,
			std::span<const std::string>      dstBuffers,
			std::function<void(PassContext&)> copy)
		{
			PassDesc desc;
			desc.SetName(std::move(name));

			for (const std::string& buffer : dstBuffers)
			{
				desc.AddBuffer(
					BufferArg{ buffer, BarrierSyncFlag::kCopy, BarrierAccessFlag::kCopyDest });
			}

			desc.SetExec(std::move(copy));

			fg.AddPass(std::move(desc));
		}
	};
}
