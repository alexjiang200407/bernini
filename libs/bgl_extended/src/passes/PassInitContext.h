#pragma once
#include <bgpu/resource/ResourceManager.h>

namespace bgpu
{
	class IDevice;
	class PipelineBatch;
}

namespace bgl
{
	class DrawBucketTable;

	/**
	 * What a pass is handed when it requests its kernels: at Init, and again at each demand build
	 * for the passes that build per draw bucket. Borrowed for the call -- a pass that needs a member
	 * past it keeps its own copy (the resource manager's ref, the table's address), never the
	 * context. Pointers rather than references so it stays a plain aggregate, as DrawData is.
	 */
	struct PassInitContext
	{
		bgpu::IDevice*           device          = nullptr;
		bgpu::PipelineBatch*     pipelines       = nullptr;
		bgpu::ResourceManagerRef resourceManager = nullptr;
		const DrawBucketTable*   drawBucketTable = nullptr;
	};
}
