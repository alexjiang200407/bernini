#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "scene/GrowableGpuBuffer.h"
#include "uniforms/DescriptorHandle.h"
#include <bgl_common/gassert.h>
#include <core/type_traits.h>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace bgl
{
	struct UploadBufferDesc
	{
		// Where the arena starts, not where it ends: it grows on demand and is bounded only by
		// device memory.
		uint32_t    initialCount = 64;
		std::string debugName;
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
		UploadBuffer() noexcept = default;
		UploadBuffer(UploadBufferDesc desc, ResourceManagerRef resourceManager)
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		UploadBuffer(const UploadBuffer&)     = delete;
		UploadBuffer(UploadBuffer&&) noexcept = default;

		UploadBuffer&
		operator=(const UploadBuffer&) = delete;

		UploadBuffer&
		operator=(UploadBuffer&&) noexcept = default;

		/**
		 * @throws std::runtime_error if the device cannot allocate the initial resource.
		 */
		void
		Init(UploadBufferDesc desc, ResourceManagerRef resourceManager)
		{
			gassert(desc.initialCount > 0, "UploadBuffer must have a positive initial count");
			gassert(resourceManager != nullptr, "UploadBuffer requires a valid ResourceManager");

			m_Desc = std::move(desc);

			m_Storage.Init(
				std::move(resourceManager),
				m_Desc.debugName,
				sizeof(T),
				m_Desc.initialCount,
				false);

			m_Values.reserve(m_Desc.initialCount);
		}

		// True once Init() has created the GPU buffer and before Release().
		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Storage.IsInitialized();
		}

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
			gassert(IsInitialized(), "UploadBuffer is uninitialized; call Init() first");

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
			gassert(IsInitialized(), "UploadBuffer is uninitialized; call Init() first");
			gassert(cmdList != nullptr, "Update requires a valid ICommandList");

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
			gassert(IsInitialized(), "UploadBuffer is uninitialized; call Init() first");
			return m_Storage.GetHandle();
		}

		[[nodiscard]] DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			gassert(IsInitialized(), "UploadBuffer is uninitialized; call Init() first");
			return DescriptorHandle(m_Storage.GetHandle().bindlessIndex);
		}

		void
		Release(bool deferred = true) noexcept
		{
			if (IsInitialized())
			{
				m_Storage.Release(deferred);
				m_Values.clear();
				m_Dirty = false;
			}
		}

	private:
		UploadBufferDesc  m_Desc;
		GrowableGpuBuffer m_Storage;
		std::vector<T>    m_Values;
		bool              m_Dirty = false;
	};
}
