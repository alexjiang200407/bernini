#pragma once
#include "resource/ResourceManager.h"

namespace bgl
{
	class DrawBucketTable;
	class IDevice;
	class PipelineBatch;

	/**
	 * What a pass is handed when it requests its kernels: at Init, and again at each demand build
	 * for the passes that build per draw bucket. Borrowed for the call -- a pass that needs a member
	 * past it keeps its own copy (the resource manager's ref, the table's address), never the
	 * context.
	 */
	struct PassInitContext
	{
		IDevice*                  device;
		PipelineBatch&            pipelines;
		const ResourceManagerRef& resourceManager;
		const DrawBucketTable&    drawBucketTable;
	};
}
