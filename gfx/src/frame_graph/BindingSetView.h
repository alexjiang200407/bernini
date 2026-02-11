#pragma once

namespace gfx
{
	class BindingSetView
	{
	public:
		BindingSetView() = default;
		BindingSetView(nvrhi::CommandListHandle cmdList, nvrhi::BindingSetHandle bindingSetIn);

		const BindingSetView&
		AttachBindingSetTo(nvrhi::ComputeState& computeState) const
		{
			computeState.addBindingSet(m_bindingSet);
			return *this;
		}

		const BindingSetView&
		AttachBindingSetTo(nvrhi::GraphicsState& gfxState) const
		{
			gfxState.addBindingSet(m_bindingSet);
			return *this;
		}

		const BindingSetView&
		AttachBindingSetTo(nvrhi::MeshletState& meshState) const
		{
			meshState.addBindingSet(m_bindingSet);
			return *this;
		}

		void
		TrackResources(nvrhi::CommandListHandle cmdList) const;

	private:
		static bool
		IsBuffer(nvrhi::ResourceType type);

		static bool
		IsTexture(nvrhi::ResourceType type);

	private:
		std::vector<nvrhi::ResourceStates> m_bindingState;
		nvrhi::BindingSetHandle            m_bindingSet;
	};
}
