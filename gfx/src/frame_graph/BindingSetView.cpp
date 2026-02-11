#include "frame_graph/BindingSetView.h"

namespace gfx
{
	BindingSetView::BindingSetView(
		nvrhi::CommandListHandle cmdList,
		nvrhi::BindingSetHandle  bindingSetIn)
	{
		m_bindingSet = bindingSetIn;
		auto desc    = m_bindingSet->getDesc();

		assert(desc != nullptr);

		m_bindingState.reserve(desc->bindings.size());

		for (const auto& binding : desc->bindings)
		{
			if (!binding.resourceHandle)
			{
				m_bindingState.push_back(nvrhi::ResourceStates::Common);
				continue;
			}

			if (IsTexture(binding.type))
			{
				auto* texture = static_cast<nvrhi::ITexture*>(binding.resourceHandle);
				if (!texture->getDesc().keepInitialState)
				{
					auto state = cmdList->getTextureSubresourceState(
						texture,
						binding.subresources.baseArraySlice,
						binding.subresources.baseMipLevel);
					m_bindingState.push_back(state);
				}
			}
			else if (IsBuffer(binding.type))
			{
				auto* buffer = static_cast<nvrhi::IBuffer*>(binding.resourceHandle);
				if (!buffer->getDesc().keepInitialState)
				{
					auto state = cmdList->getBufferState(buffer);
					m_bindingState.push_back(state);
				}
			}
		}
	}

	void
	BindingSetView::TrackResources(nvrhi::CommandListHandle cmdList) const
	{
		auto desc = m_bindingSet->getDesc();
		if (!desc)
			return;

		size_t stateIndex = 0;
		for (const auto& binding : desc->bindings)
		{
			if (!binding.resourceHandle)
			{
				continue;
			}

			if (IsTexture(binding.type))
			{
				auto* texture = static_cast<nvrhi::ITexture*>(binding.resourceHandle);
				if (!texture->getDesc().keepInitialState)
				{
					auto stateToTrack = m_bindingState[stateIndex];
					cmdList->beginTrackingTextureState(texture, binding.subresources, stateToTrack);
					++stateIndex;
				}
			}
			else if (IsBuffer(binding.type))
			{
				auto* buffer = static_cast<nvrhi::IBuffer*>(binding.resourceHandle);
				if (!buffer->getDesc().keepInitialState)
				{
					auto stateToTrack = m_bindingState[stateIndex];
					cmdList->beginTrackingBufferState(buffer, stateToTrack);
					++stateIndex;
				}
			}
		}
	}

	bool
	BindingSetView::IsBuffer(nvrhi::ResourceType type)
	{
		switch (type)
		{
		case nvrhi::ResourceType::StructuredBuffer_UAV:
		case nvrhi::ResourceType::StructuredBuffer_SRV:
		case nvrhi::ResourceType::PushConstants:
		case nvrhi::ResourceType::ConstantBuffer:
		case nvrhi::ResourceType::RawBuffer_SRV:
		case nvrhi::ResourceType::RawBuffer_UAV:
		case nvrhi::ResourceType::TypedBuffer_SRV:
		case nvrhi::ResourceType::TypedBuffer_UAV:
		case nvrhi::ResourceType::VolatileConstantBuffer:
			return true;
		}

		return false;
	}

	bool
	BindingSetView::IsTexture(nvrhi::ResourceType type)
	{
		switch (type)
		{
		case nvrhi::ResourceType::Texture_SRV:
		case nvrhi::ResourceType::Texture_UAV:
			return true;
		}

		return false;
	}
}
