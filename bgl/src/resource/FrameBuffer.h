#pragma once
#include "constants/constants.h"
#include "resource/Rtv.h"
#include <core/containers/static_vector.h>

namespace bgl
{
	struct FrameBuffer
	{
		core::static_vector<RtvHandle, c_MaxRenderTargets> colorAttachments;
		RtvHandle                                          depthAttachment;

		FrameBuffer&
		AddColorAttachment(RtvHandle handle)
		{
			colorAttachments.push_back(handle);
			return *this;
		}
	};
}
