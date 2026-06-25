#pragma once

namespace bgl
{
	class UploadManager final
	{
	private:
		class BufferChunk
		{
		public:
			static constexpr uint64_t c_SizeAlignment = 4096;

			wrl::ComPtr<ID3D12Resource> buffer;
			uint64_t                    version      = 0;
			uint64_t                    bufferSize   = 0;
			uint64_t                    writePointer = 0;
			void*                       cpuVA        = nullptr;
			D3D12_GPU_VIRTUAL_ADDRESS   gpuVA        = 0;
			uint32_t                    identifier   = 0;

			~BufferChunk();
		};

	public:
		UploadManager(
			wrl::ComPtr<ID3D12Device> device,
			size_t                    defaultChunkSize,
			uint64_t                  memoryLimit,
			bool                      isScratchBuffer);

		bool
		SuballocateBuffer(
			uint64_t                     lastCompletedValue,
			uint64_t                     size,
			ID3D12GraphicsCommandList*   pCommandList,
			wrl::ComPtr<ID3D12Resource>& pBuffer,
			size_t*                      pOffset,
			void**                       pCpuVA,
			D3D12_GPU_VIRTUAL_ADDRESS*   pGpuVA,
			uint64_t                     currentVersion,
			uint32_t                     alignment = 256);

		void
		SubmitChunks(uint64_t currentVersion, uint64_t submittedVersion);

	private:
		[[nodiscard]] std::shared_ptr<BufferChunk>
		CreateChunk(size_t size) const;

	private:
		wrl::ComPtr<ID3D12Device> m_Device;
		size_t                    m_DefaultChunkSize = 0;
		uint64_t                  m_MemoryLimit      = 0;
		uint64_t                  m_AllocatedMemory  = 0;
		bool                      m_IsScratchBuffer  = false;

		std::list<std::shared_ptr<BufferChunk>> m_ChunkPool;
		std::shared_ptr<BufferChunk>            m_CurrentChunk;
	};
}
