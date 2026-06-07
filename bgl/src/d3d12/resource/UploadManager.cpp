#include "resource/UploadManager.h"

namespace bgl
{
	UploadManager::UploadManager(
		wrl::ComPtr<ID3D12Device> device,
		Queue*                    pQueue,
		size_t                    defaultChunkSize,
		uint64_t                  memoryLimit,
		bool                      isScratchBuffer) :
		m_Device(std::move(device)), m_Queue(pQueue), m_DefaultChunkSize(defaultChunkSize),
		m_MemoryLimit(memoryLimit), m_IsScratchBuffer(isScratchBuffer)
	{}
}
