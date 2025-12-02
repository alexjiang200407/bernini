#pragma once
#include <rhi/IResource.h>
#include <rhi/ResourceHandle.h>

namespace rhi
{
	class IDevice : public IResource
	{
	public:
		using Handle = ResourceHandle<IDevice>;

	public:
		~IDevice() = default;

		[[nodiscard]]
		static Handle
		Create();
	};
}
