#pragma once
#include "DiskProgramCache.h"
#include "SlangSessions.h"
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <memory>
#include <slang.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	/**
	 * The half of a context that is the same on every backend: the desc, the compiler, and the rule
	 * that one context is live per process at a time.
	 *
	 * One, because D3D12 hands back the same device for an adapter however often it is asked, and
	 * enabling the debug layer once that device exists removes it -- so a second context could
	 * never be independent of the first, and its info-queue callback would double every message.
	 * Metal would allow two; the rule holds there too, so a program that runs on Metal runs on D3D12.
	 */
	class ContextBase : public core::RefCounter<GpuContext>
	{
	public:
		ContextBase(const ContextBase&) = delete;
		ContextBase(ContextBase&&)      = delete;
		ContextBase&
		operator=(const ContextBase&) = delete;
		ContextBase&
		operator=(ContextBase&&) = delete;

		/** @throws std::runtime_error if a context is already live in this process. */
		ContextBase(GpuContextDesc desc, SlangCompileTarget target) :
			m_Desc(std::move(desc)),
			m_Slang(
				SlangSessionDesc{ .target      = target,
		                          .searchPaths = ShaderSearchPaths(m_Desc.clientShaderDir) })
		{
			if (!m_Desc.shaderCacheDir.empty())
				m_ProgramCache = std::make_unique<DiskProgramCache>(m_Desc.shaderCacheDir, m_Slang);
		}

		const GpuContextDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		const std::vector<std::string>&
		GetShaderSearchPaths() const noexcept override
		{
			return m_Slang.GetSearchPaths();
		}

		void
		AddSourceModule(SlangSourceModule sourceModule) noexcept override
		{
			m_Slang.AddSourceModule(std::move(sourceModule));
		}

		uint64_t
		GetSourceSalt() const noexcept override
		{
			return m_Slang.GetSourceSalt();
		}

		const ProgramCache*
		GetProgramCache() const noexcept override
		{
			return m_ProgramCache.get();
		}

		slang::IModule*
		LoadModule(std::string_view moduleName) noexcept override
		{
			return m_Slang.LoadModule(moduleName);
		}

		slang::IModule*
		LoadScalarLayoutModule(std::string_view moduleName, std::string& diagnostic) override
		{
			return m_Slang.LoadScalarLayoutModule(moduleName, diagnostic);
		}

		void
		ReleaseSlangSessions() noexcept override
		{
			m_Slang.ReleaseAll();
		}

	private:
		// Declared first, so the claim is made before anything below touches the device.
		struct ProcessSlot
		{
			ProcessSlot();
			~ProcessSlot() noexcept;
			ProcessSlot(const ProcessSlot&) = delete;
			ProcessSlot(ProcessSlot&&)      = delete;
			ProcessSlot&
			operator=(const ProcessSlot&) = delete;
			ProcessSlot&
			operator=(ProcessSlot&&) = delete;
		};

		ProcessSlot    m_Slot;
		GpuContextDesc m_Desc;
		SlangSessions  m_Slang;

		// After the sessions it keys against, so it is destroyed before them.
		std::unique_ptr<DiskProgramCache> m_ProgramCache;
	};
}
