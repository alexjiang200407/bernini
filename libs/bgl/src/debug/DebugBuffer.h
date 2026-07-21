#pragma once
#include "idl/DebugRecord.h"
#include "scene/ComputeBuffer.h"

#if defined(BERNINI_GPU_DEBUG)
namespace bgl
{
	/**
	 * CPU side of the GPU-assertion buffer written by dbg_raise()
	 * (bgl/shaders/src/debug/dbg.slang). It is a uint UAV laid out as a small
	 * header followed by fixed-size records:
	 *
	 *   [0] record counter (atomic append cursor)
	 *   [1] overflow flag
	 *   [2] record capacity
	 *   [3] reserved
	 *   [kHeaderWords + i * kRecordWords ..] record i = { errcode, payload.xyz }
	 *
	 * Debug-build only: the whole type is compiled out of Release via
	 * BERNINI_GPU_DEBUG, which is a global Debug-config define.
	 */
	class DebugBuffer
	{
	public:
		// Word layout -- MUST stay in sync with dbg.slang.
		static constexpr uint32_t kHeaderWords = 4;
		static constexpr uint32_t kRecordWords = 4;

		static_assert(
			kRecordWords * sizeof(uint32_t) == sizeof(idl::DebugRecord),
			"dbg.slang writes kRecordWords words per record and the readback reads a DebugRecord "
			"over them; a field added to one and not the other decodes as garbage");

		static constexpr uint32_t kCounterWord  = 0;
		static constexpr uint32_t kOverflowWord = 1;
		static constexpr uint32_t kCapacityWord = 2;

		DebugBuffer() noexcept = default;

		void
		Init(uint32_t recordCapacity, ResourceManagerRef resourceManager) noexcept
		{
			m_Capacity = recordCapacity;

			auto desc = ComputeBufferDesc()
			                .SetElement<uint32_t>()
			                .SetMaxCount(kHeaderWords + recordCapacity * kRecordWords)
			                .SetDebugName("GPU Debug Buffer");

			m_Buffer.Init(std::move(desc), std::move(resourceManager));
		}

		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Buffer.IsInitialized();
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_Buffer.GetBufferHandle();
		}

		[[nodiscard]] uint32_t
		GetCapacity() const noexcept
		{
			return m_Capacity;
		}

		[[nodiscard]] uint64_t
		ByteSize() const noexcept
		{
			return m_Buffer.ByteSize();
		}

		/**
		 * Records the per-frame header reset (counter = 0, overflow = 0, capacity)
		 * onto the command list. The buffer must be in copy-dest state. Records past
		 * the header are left stale; a zero counter makes them invisible to readback.
		 */
		void
		Reset(ICommandList* cmd) const noexcept
		{
			gassert(cmd != nullptr, "Command list cannot be null");
			gassert(IsInitialized(), "DebugBuffer is uninitialized; call Init() first");

			const std::array<uint32_t, kHeaderWords> header = { 0u, 0u, m_Capacity, 0u };
			cmd->WriteBuffer(m_Buffer.GetBufferHandle(), header.data(), sizeof(header));
		}

		void
		Release(bool deferred = true) noexcept
		{
			m_Buffer.Release(deferred);
		}

	private:
		ComputeBuffer m_Buffer;
		uint32_t      m_Capacity = 0;
	};
}
#endif  // BERNINI_GPU_DEBUG
