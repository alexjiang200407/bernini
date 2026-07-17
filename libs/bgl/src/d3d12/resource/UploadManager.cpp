#include "resource/UploadManager.h"
#include "cmd/Version.h"
#include "types/QueueType.h"
#include <core/math.h>

namespace bgl
{
	UploadManager::BufferChunk::~BufferChunk()
	{
		if (buffer && cpuVA)
		{
			buffer->Unmap(0, nullptr);
			cpuVA = nullptr;
		}
	}

	UploadManager::UploadManager(wrl::ComPtr<ID3D12Device> device, size_t defaultChunkSize) :
		m_Device(std::move(device)), m_DefaultChunkSize(defaultChunkSize)
	{
		gassert(m_Device.Get() != nullptr, "Device cannot be null");
	}

	bool
	UploadManager::SuballocateBuffer(
		uint64_t                     lastCompletedInstance,
		uint64_t                     size,
		wrl::ComPtr<ID3D12Resource>& pBuffer,
		size_t*                      pOffset,
		void**                       pCpuVA,
		D3D12_GPU_VIRTUAL_ADDRESS*   pGpuVA,
		uint64_t                     currentVersion,
		uint32_t                     alignment)
	{
		std::shared_ptr<BufferChunk> chunkToRetire;

		// Try to allocate from the current chunk first
		if (m_CurrentChunk != nullptr)
		{
			uint64_t alignedOffset = core::align(m_CurrentChunk->writePointer, (uint64_t)alignment);
			uint64_t endOfDataInChunk = alignedOffset + size;

			if (endOfDataInChunk <= m_CurrentChunk->bufferSize)
			{
				// The buffer can fit into the current chunk - great, we're done
				m_CurrentChunk->writePointer = endOfDataInChunk;

				pBuffer = m_CurrentChunk->buffer;
				if (pOffset)
					*pOffset = alignedOffset;
				if (pCpuVA && m_CurrentChunk->cpuVA)
					*pCpuVA = (char*)m_CurrentChunk->cpuVA + alignedOffset;
				if (pGpuVA && m_CurrentChunk->gpuVA)
					*pGpuVA = m_CurrentChunk->gpuVA + alignedOffset;

				return true;
			}

			chunkToRetire = m_CurrentChunk;
			m_CurrentChunk.reset();
		}

		// Try to find a chunk in the pool that's no longer used and is large enough to allocate our buffer
		for (auto it = m_ChunkPool.begin(); it != m_ChunkPool.end(); ++it)
		{
			std::shared_ptr<BufferChunk> chunk = *it;

			if (VersionGetSubmitted(chunk->version) &&
			    VersionGetInstance(chunk->version) <= lastCompletedInstance)
			{
				chunk->version = 0;
			}

			if (chunk->version == 0 && chunk->bufferSize >= size)
			{
				m_ChunkPool.erase(it);
				m_CurrentChunk = chunk;
				break;
			}
		}

		if (chunkToRetire)
		{
			m_ChunkPool.push_back(chunkToRetire);
		}

		if (!m_CurrentChunk)
		{
			const uint64_t sizeToAllocate =
				core::align(std::max(size, m_DefaultChunkSize), BufferChunk::c_SizeAlignment);

			m_CurrentChunk = CreateChunk(sizeToAllocate);
			if (!m_CurrentChunk)
				return false;
		}

		m_CurrentChunk->version      = currentVersion;
		m_CurrentChunk->writePointer = size;

		pBuffer = m_CurrentChunk->buffer;
		if (pOffset)
			*pOffset = 0;
		if (pCpuVA)
			*pCpuVA = m_CurrentChunk->cpuVA;
		if (pGpuVA)
			*pGpuVA = m_CurrentChunk->gpuVA;

		return true;
	}

	void
	UploadManager::SubmitChunks(uint64_t currentVersion, uint64_t submittedVersion)
	{
		if (m_CurrentChunk)
		{
			m_ChunkPool.push_back(m_CurrentChunk);
			m_CurrentChunk.reset();
		}

		for (const auto& chunk : m_ChunkPool)
		{
			if (chunk->version == currentVersion)
				chunk->version = submittedVersion;
		}
	}

	std::shared_ptr<UploadManager::BufferChunk>
	UploadManager::CreateChunk(size_t size) const
	{
		auto chunk = std::make_shared<BufferChunk>();

		size = core::align(size, BufferChunk::c_SizeAlignment);

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type                  = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width               = size;
		bufferDesc.Height              = 1;
		bufferDesc.DepthOrArraySize    = 1;
		bufferDesc.MipLevels           = 1;
		bufferDesc.SampleDesc.Count    = 1;
		bufferDesc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT hr = m_Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&chunk->buffer));

		if (FAILED(hr))
			return nullptr;

		hr = chunk->buffer->Map(0, nullptr, &chunk->cpuVA);

		if (FAILED(hr))
			return nullptr;

		chunk->bufferSize = size;
		chunk->gpuVA      = chunk->buffer->GetGPUVirtualAddress();
		chunk->identifier = uint32_t(m_ChunkPool.size());

		std::wstringstream wss;
		wss << L"Upload Buffer " << chunk->identifier;
		chunk->buffer->SetName(wss.str().c_str());

		return chunk;
	}

}
