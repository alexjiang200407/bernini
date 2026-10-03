#pragma once
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/ResourceManager.h>
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <string>

namespace bgl
{
	class MeshInstanceWriter final : public core::RefCounter<IMeshInstanceWriter>
	{
	public:
		// The generated program's thread-group width: a block of capacity N dispatches
		// DispatchGroups(N) groups of this many slots, which c_MaxMeshInstanceBlockCapacity keeps
		// inside one dispatch dimension.
		static constexpr uint32_t c_GroupSize = 64;

		MeshInstanceWriter(
			MeshInstanceWriterDesc        desc,
			bgpu::ComputeKernel           kernel,
			const bgpu::IResourceManager* owner) noexcept;

		MeshInstanceWriter(MeshInstanceWriter&&) noexcept      = delete;
		MeshInstanceWriter(const MeshInstanceWriter&) noexcept = delete;

		MeshInstanceWriter&
		operator=(MeshInstanceWriter&&) noexcept = delete;

		MeshInstanceWriter&
		operator=(const MeshInstanceWriter&) noexcept = delete;

		~MeshInstanceWriter() noexcept override = default;

		[[nodiscard]] const MeshInstanceWriterDesc&
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
		MeshInstanceWriterDesc        m_Desc;
		bgpu::ComputeKernel           m_Kernel;
		const bgpu::IResourceManager* m_Owner = nullptr;
	};

	/** The name the program generated for `desc` is registered under. */
	[[nodiscard]] std::string
	MeshInstanceWriterProgramName(const MeshInstanceWriterDesc& desc);

	/**
	 * The compute program that runs `desc.slangTypeName`'s Write once per slot of a block.
	 *
	 * @pre IsMeshInstanceWriterDescValid(desc): both names are spliced into the text unquoted.
	 */
	[[nodiscard]] std::string
	MeshInstanceWriterProgramSource(const MeshInstanceWriterDesc& desc);

	/**
	 * Whether `desc.slangModuleName` is a dotted import name, `desc.slangTypeName` an identifier and
	 * `desc.geomType` a kind of block a writer places.
	 */
	[[nodiscard]] bool
	IsMeshInstanceWriterDescValid(const MeshInstanceWriterDesc& desc) noexcept;
}
