#pragma once
#include <bgl/IInstanceWriter.h>
#include <bgl/types/InstanceWriterDesc.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/ResourceManager.h>
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <string>

namespace bgl
{
	class InstanceWriter final : public core::RefCounter<IInstanceWriter>
	{
	public:
		// The generated program's thread-group width: a block of capacity N dispatches
		// DispatchGroups(N) groups of this many slots, which c_MaxMeshInstanceBlockCapacity keeps
		// inside one dispatch dimension.
		static constexpr uint32_t c_GroupSize = 64;

		InstanceWriter(
			InstanceWriterDesc            desc,
			bgpu::ComputeKernel           kernel,
			const bgpu::IResourceManager* owner) noexcept;

		InstanceWriter(InstanceWriter&&) noexcept      = delete;
		InstanceWriter(const InstanceWriter&) noexcept = delete;

		InstanceWriter&
		operator=(InstanceWriter&&) noexcept = delete;

		InstanceWriter&
		operator=(const InstanceWriter&) noexcept = delete;

		~InstanceWriter() noexcept override = default;

		[[nodiscard]] const InstanceWriterDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		[[nodiscard]] const bgpu::ComputeKernel&
		GetKernel() const noexcept
		{
			return m_Kernel;
		}

		// The renderer that compiled it, named by its resource manager, which every view it made
		// shares.
		[[nodiscard]] const bgpu::IResourceManager*
		GetOwner() const noexcept
		{
			return m_Owner;
		}

		[[nodiscard]] static uint32_t
		DispatchGroups(uint32_t capacity) noexcept;

	private:
		InstanceWriterDesc            m_Desc;
		bgpu::ComputeKernel           m_Kernel;
		const bgpu::IResourceManager* m_Owner = nullptr;
	};

	/** The name the program generated for `desc` is registered under. */
	[[nodiscard]] std::string
	InstanceWriterProgramName(const InstanceWriterDesc& desc);

	/**
	 * The compute program that runs `desc.type`'s Write once per slot of a block.
	 *
	 * @pre IsInstanceWriterDescValid(desc): both names are spliced into the text unquoted.
	 */
	[[nodiscard]] std::string
	InstanceWriterProgramSource(const InstanceWriterDesc& desc);

	/** Whether `desc.module` is a dotted import name and `desc.type` an identifier. */
	[[nodiscard]] bool
	IsInstanceWriterDescValid(const InstanceWriterDesc& desc) noexcept;
}
