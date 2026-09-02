#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletKernel.h"
#include "pipeline/MeshletPipeline.h"

namespace bgl
{
	class IDevice;

	/**
	 * Kernels requested first and built together.
	 *
	 * A pass names the member each of its kernels lands in and the pipeline it wants there; `Build`
	 * then compiles the whole set across worker threads and fills every member before it returns.
	 * On a cold shader cache that build is the whole of a start-up wait, and the links are
	 * independent, so they spread over cores where one thread would take them in turn.
	 *
	 * @pre a kernel handed to Add outlives Build. A pass member does; a local in a scope that ends
	 *      first does not.
	 */
	class PipelineBatch final
	{
	public:
		// Every worker that misses the shader cache stands up a Slang global session of its own,
		// about 200 MB each, and the renderer's 27 links stop getting faster past six workers
		// (measured: six and twelve within 0.1 s of each other, at 1.5 GB against 2.8 GB peak).
		static constexpr uint32_t c_MaxBuildThreads = 6;

		explicit PipelineBatch(IDevice* device) noexcept;

		PipelineBatch(const PipelineBatch&) = delete;

		PipelineBatch&
		operator=(const PipelineBatch&) = delete;

		void
		Add(MeshletKernel& kernel, MeshletPipelineDesc desc);

		void
		Add(ComputeKernel& kernel, ComputePipelineDesc desc);

		/**
		 * Builds everything added since the last call on up to `threads` threads (0: as many as
		 * c_MaxBuildThreads allows, never more than there are kernels) and assigns each kernel.
		 * Returns once every one is built. A shader that fails to compile is fatal, as it is on the
		 * calling thread.
		 */
		void
		Build(uint32_t threads = 0);

		[[nodiscard]] size_t
		Pending() const noexcept
		{
			return m_MeshletRequests.size() + m_ComputeRequests.size();
		}

	private:
		template <typename Kernel, typename Desc>
		struct Request
		{
			Kernel* kernel = nullptr;
			Desc    desc;
		};

		IDevice* m_Device = nullptr;

		std::vector<Request<MeshletKernel, MeshletPipelineDesc>> m_MeshletRequests;
		std::vector<Request<ComputeKernel, ComputePipelineDesc>> m_ComputeRequests;
	};
}
