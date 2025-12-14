#include "buffer/DynamicVertexBuffer.h"
#include "geometry/IGeometry.h"

namespace gfx
{
	template <typename T>
	concept PrimitiveGeometryCRTP = requires(nvrhi::DeviceHandle device) {
		{ T::GetVertexBuffer(device) } -> std::same_as<DynamicVertexBuffer>;
		{ T::GetIndices() } -> std::same_as<std::span<const uint16_t>>;
	};

	template <typename T>
	class PrimitiveGeometry : public IGeometry
	{
	protected:
		PrimitiveGeometry(nvrhi::DeviceHandle device, std::string_view vertexShaderPath) :
			IGeometry{ device, vertexShaderPath }
		{
			static_assert(_concept_check());

			if (m_refCount.fetch_add(1, std::memory_order_acq_rel) == 0)
			{
				m_vertexBuf     = std::move(T::GetVertexBuffer(device));
				auto indices    = T::GetIndices();
				m_indexCount    = static_cast<uint32_t>(std::size(indices));
				auto indexBufSz = sizeof(uint16_t) * m_indexCount;
				m_indexBuffer   = device->createBuffer(
                    nvrhi::BufferDesc{}
                        .setByteSize(indexBufSz)
                        .setIsIndexBuffer(true)
                        .setInitialState(nvrhi::ResourceStates::CopyDest)
                        .setKeepInitialState(false)
                        .setDebugName("Primitive Geometry Index Buffer"));

				nvrhi::CommandListHandle uploadCmdList = device->createCommandList();
				uploadCmdList->open();
				m_vertexBuf.Update(uploadCmdList);
				uploadCmdList->writeBuffer(m_indexBuffer, indices.data(), indexBufSz);
				uploadCmdList->close();
				device->executeCommandList(uploadCmdList);
			}

			m_vertexLayout = m_vertexBuf.GenerateVertexLayout(device, m_vertexShader);
		}

		~PrimitiveGeometry() noexcept
		{
			if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				m_vertexBuf.Release();
				m_indexBuffer.Reset();
				m_indexCount = 0u;
			}
		}

	public:
		nvrhi::InputLayoutHandle
		GetVertexLayout() const noexcept
		{
			return m_vertexLayout;
		}

		nvrhi::BufferHandle
		GetVertexBuffer() const noexcept override
		{
			return m_vertexBuf;
		}

		nvrhi::BufferHandle
		GetIndexBuffer() const noexcept override
		{
			return m_indexBuffer;
		}

		uint32_t
		GetIndexCount() const noexcept override
		{
			return m_indexCount;
		}

	private:
		static constexpr bool
		_concept_check()
		{
			return PrimitiveGeometryCRTP<T>;
		}

	private:
		nvrhi::InputLayoutHandle          m_vertexLayout;
		static inline DynamicVertexBuffer m_vertexBuf;
		static inline nvrhi::BufferHandle m_indexBuffer;
		static inline std::atomic_size_t  m_refCount   = 0;
		static inline uint32_t            m_indexCount = 0u;
	};
}
