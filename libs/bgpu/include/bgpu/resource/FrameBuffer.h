#pragma once
#include <bgpu/constants/constants.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/Rtv.h>
#include <core/containers/static_vector.h>
#include <utility>

namespace bgpu
{
	struct FrameBuffer
	{
		core::static_vector<RtvHandle, c_MaxRenderTargets> colorAttachments;
		DsvHandle                                          depthAttachment;

		template <typename Self>
		Self&&
		AddColorAttachment(this Self&& self, RtvHandle handle)
		{
			self.colorAttachments.push_back(std::move(handle));
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepthAttachment(this Self&& self, DsvHandle handle)
		{
			self.depthAttachment = handle;
			return std::forward<Self>(self);
		}
	};
}
