#include "scene/PaletteArena.h"
#include "scene/GrowableGpuBuffer.h"

namespace bgl
{
	namespace
	{
		// One instance of a modest rig, so a view that places no skinned instance pays a minimum and
		// the first that does grows once.
		constexpr uint32_t c_InitialFloat4s = 256;
	}

	void
	PaletteArena::Init(ResourceManagerRef resourceManager)
	{
		m_ResourceManager = std::move(resourceManager);

		auto desc = ComputeBufferDesc();
		desc.SetElement<glm::vec4>()
			.SetInitialCount(c_InitialFloat4s)
			.SetDebugName("Bone Palette Arena");

		m_Storage.Init(std::move(desc), m_ResourceManager);
		m_Offsets.grow(c_InitialFloat4s);
	}

	core::multi_slot_handle
	PaletteArena::Allocate(uint32_t float4Count)
	{
		gassert(IsInitialized(), "PaletteArena is uninitialized; call Init() first");
		gassert(float4Count > 0, "PaletteArena::Allocate requires a positive count");

		// allocate_slots throws when nothing fits rather than returning null, and "does not fit" is
		// the ordinary case that triggers a growth here, not an error.
		const auto tryAllocate = [this](uint32_t count) noexcept {
			try
			{
				return m_Offsets.allocate_slots(count);
			}
			catch (const std::runtime_error&)
			{
				return core::multi_slot_handle();
			}
		};

		auto handle = tryAllocate(float4Count);
		if (handle.is_null())
		{
			const uint32_t grown =
				NextGpuBufferCapacity(Capacity(), Capacity() + float4Count, sizeof(glm::vec4));

			// GPU side first: it is the one that can fail, and it leaves nothing behind when it does,
			// so the allocator and the buffer cannot end up disagreeing on capacity.
			m_Storage.Resize(grown);
			m_Offsets.grow(grown);

			handle = tryAllocate(float4Count);
			core::throw_runtime_error_if(
				handle.is_null(),
				"PaletteArena: {} float4s do not fit even after growing to {}",
				float4Count,
				Capacity());
		}

		return handle;
	}

	void
	PaletteArena::Free(core::multi_slot_handle handle) noexcept
	{
		gassert(IsInitialized(), "PaletteArena is uninitialized; call Init() first");
		m_Offsets.erase(handle);
	}
}
