#pragma once
#include "constants/constants.h"
#include "resource/Dsv.h"
#include "resource/Rtv.h"
#include <core/containers/static_vector.h>

namespace bgl
{
	struct FrameBuffer
	{
		core::static_vector<RtvHandle, c_MaxRenderTargets> colorAttachments;
		DsvHandle                                          depthAttachment;

		FrameBuffer&
		AddColorAttachment(RtvHandle handle)
		{
			colorAttachments.push_back(std::move(handle));
			return *this;
		}

		FrameBuffer&
		SetDepthAttachment(DsvHandle handle)
		{
			depthAttachment = handle;
			return *this;
		}
	};
}
