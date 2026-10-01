#pragma once
#include <bgpu/buffer/GrowableGpuBuffer.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/uniforms/DescriptorHandle.h>
#include <core/err/util.h>
#include <core/type_traits.h>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace bgpu
{
	struct UploadBufferDesc
	{
		// Where the arena starts, not where it ends: it grows on demand and is bounded only by
		// device memory.
		uint32_t    initialCount = 64;
		std::string debugName;

		// The handle shaders are given is a UAV rather than an SRV. For a list the CPU writes that is
		// bound where a shader declares a ComputeBuffer it only reads: the descriptor's type must
		// match the declaration, or the read is undefined.
		bool unorderedAccessView = false;

		UploadBufferDesc&
		SetInitialCount(uint32_t value) noexcept
		{
			initialCount = value;
			return *this;
		}

		UploadBufferDesc&
		SetDebugName(std::string value) noexcept
		{
			debugName = std::move(value);
			return *this;
		}

		UploadBufferDesc&
		SetUnorderedAccessView(bool value) noexcept
		{
			unorderedAccessView = value;
			return *this;
		}
	};

	/**
	 * A CPU-authored list mirrored into a GPU structured buffer that shaders only read -- the
	 * complement of ComputeBuffer, whose contents the GPU fills.
	 *
	 * The CPU side is the storage, replaced wholesale by Assign and uploaded by the next Update.
	 * There is no per-element identity and no incremental edit: a list whose elements have stable
	 * handles or local edits belongs in PackedBuffer or EntryBuffer instead.
	 */
	template <core::type_traits::trivially_copyable T>
	class UploadBuffer
	{
	public:
		/**
		 * @throws std::runtime_error if the device cannot allocate the initial resource.
		 */
		UploadBuffer(ResourceManagerRef resourceManager, UploadBufferDesc desc) :
			m_Desc(std::move(desc)), m_Storage(
										 std::move(resourceManager),
										 m_Desc.debugName,
										 sizeof(T),
										 m_Desc.initialCount,
										 m_Desc.unorderedAccessView)
		{
			m_Values.reserve(m_Desc.initialCount);
		}

		UploadBuffer(const UploadBuffer&)     = delete;
		UploadBuffer(UploadBuffer&&) noexcept = default;

		UploadBuffer&
		operator=(const UploadBuffer&) = delete;

		UploadBuffer&
		operator=(UploadBuffer&&) noexcept = default;

		/**
		 * Replaces the contents; the next Update uploads them. An assign equal to what the buffer
		 * already holds is a no-op, so a caller may re-derive its list without forcing uploads.
		 *
		 * @throws std::runtime_error if the device cannot allocate a large enough resource; the
		 *         buffer keeps its previous contents.
		 */
		void
		Assign(std::span<const T> values)
		{
			// Empty short-circuits before the memcmp: two empty spans may both be null, which
			// memcmp's nonnull contract forbids.
			const auto equal = [&] {
				return m_Values.size() == values.size() &&
				       (values.empty() ||
				        std::memcmp(m_Values.data(), values.data(), values.size() * sizeof(T)) ==
				            0);
			};

			if (equal())
			{
				return;
			}

			if (values.size() > m_Storage.GetCapacity())
			{
				// No forward copy: the upload below rewrites the whole resource anyway.
				m_Storage.Grow(
					NextGpuBufferCapacity(
						m_Storage.GetCapacity(),
						static_cast<uint32_t>(values.size()),
						sizeof(T)),
					false);
			}

			m_Values.assign(values.begin(), values.end());
			m_Dirty = true;
		}

		[[nodiscard]] std::span<const T>
		Values() const noexcept
		{
			return m_Values;
		}

		[[nodiscard]] uint32_t
		Size() const noexcept
		{
			return static_cast<uint32_t>(m_Values.size());
		}

		// Retires storage a growth superseded and uploads a changed list.
		void
		Update(ICommandList* cmdList)
		{
			core::ensure(cmdList != nullptr, "Update requires a valid ICommandList");

			m_Storage.FlushGrowth(cmdList);

			if (!m_Dirty)
			{
				return;
			}

			if (!m_Values.empty())
			{
				cmdList->WriteBuffer(
					m_Storage.GetHandle(),
					m_Values.data(),
					m_Values.size() * sizeof(T));
			}

			m_Dirty = false;
		}

		// Re-read every frame: growth mints a new handle and retires the old one (see
		// GrowableGpuBuffer), so a cached descriptor index goes stale.
		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_Storage.GetHandle();
		}

		[[nodiscard]] DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			return DescriptorHandle(m_Storage.GetHandle().bindlessIndex);
		}

	private:
		UploadBufferDesc  m_Desc;
		GrowableGpuBuffer m_Storage;
		std::vector<T>    m_Values;
		bool              m_Dirty = false;
	};
}
