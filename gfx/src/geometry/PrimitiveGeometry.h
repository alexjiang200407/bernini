#include "geometry/DynamicVertexBuffer.h"
#include "geometry/IGeometry.h"

namespace gfx
{
	template <typename T, typename V>
	concept PrimitiveGeometryCRTP = requires {
		{ T::GetVertices() } -> std::convertible_to<std::vector<V>>;
		{ T::GetIndices() } -> std::convertible_to<std::vector<uint16_t>>;
	};

	template <typename V, PrimitiveGeometryCRTP<V> T>
	class PrimitiveGeometry : public IGeometry
	{
	public:
		PrimitiveGeometry()
		{
			{
				auto vertexBufferDesc = nvrhi::BufferDesc{};
				vertexBufferDesc.setByteSize(m_vertices.size() * sizeof(V))
					.setIsVertexBuffer(true)
					.setInitialState(nvrhi::ResourceStates::CopyDest)
					.setKeepInitialState(false)
					.setDebugName("Primitive Vertex Buffer");

				m_vertexBuf = device->createBuffer(vertexBufferDesc);
			}

			{
				auto indexBufferDesc = nvrhi::BufferDesc{};
				indexBufferDesc.setByteSize(m_indices.size() * sizeof(uint16_t))
					.setIsIndexBuffer(true)
					.setInitialState(nvrhi::ResourceStates::CopyDest)
					.setKeepInitialState(false)
					.setDebugName("Primitive Index Buffer");

				m_indexBuf = device->createBuffer(vertexBufferDesc);
			}
		}

	private:
		static const inline std::vector<uint16_t> m_indices  = T::GetIndices();
		static const inline std::vector<V>        m_vertices = T::GetVertices();
	};
}
