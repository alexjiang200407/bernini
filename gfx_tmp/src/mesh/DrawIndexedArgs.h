#pragma once

namespace gfx
{
	struct DrawIndexedArgs
	{
		uint32_t indexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startIndexLocation;
		int32_t  baseVertexLocation;
		uint32_t startInstanceLocation;
	};
}
