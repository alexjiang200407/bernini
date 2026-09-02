#include "pipeline/PipelineBatch.h"
#include "device/Device.h"

#include <core/parallel_for.h>

namespace bgl
{
	PipelineBatch::PipelineBatch(IDevice* device) noexcept : m_Device(device)
	{
		gassert(device != nullptr, "Device pointer is null");
	}

	void
	PipelineBatch::Add(MeshletKernel& kernel, MeshletPipelineDesc desc)
	{
		m_MeshletRequests.emplace_back(&kernel, std::move(desc));
	}

	void
	PipelineBatch::Add(ComputeKernel& kernel, ComputePipelineDesc desc)
	{
		m_ComputeRequests.emplace_back(&kernel, std::move(desc));
	}

	void
	PipelineBatch::Build(uint32_t threads)
	{
		const size_t count = Pending();
		if (count == 0)
			return;

		const auto started = std::chrono::steady_clock::now();

		const uint32_t hardware = std::max(1u, std::thread::hardware_concurrency());
		const uint32_t workers  = threads != 0 ? threads : std::min(hardware, c_MaxBuildThreads);

		core::parallel_for(count, workers, "bgl pipeline build", [&](size_t index) {
			if (index < m_MeshletRequests.size())
			{
				auto& [kernel, desc] = m_MeshletRequests[index];
				*kernel              = m_Device->CreateMeshletKernel(desc);
			}
			else
			{
				auto& [kernel, desc] = m_ComputeRequests[index - m_MeshletRequests.size()];
				*kernel              = m_Device->CreateComputeKernel(desc);
			}
		});

		m_MeshletRequests.clear();
		m_ComputeRequests.clear();

		const auto elapsed = std::chrono::steady_clock::now() - started;
		logger::info(
			"Built {} pipelines in {} ms",
			count,
			std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
	}
}
