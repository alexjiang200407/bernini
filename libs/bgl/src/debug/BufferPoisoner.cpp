#include "debug/BufferPoisoner.h"
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <core/err/util.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace bgl
{
	void
	BufferPoisoner::Init(bgpu::ResourceManagerRef resourceManager)
	{
		core::ensure(
			resourceManager != nullptr,
			"BufferPoisoner::Init requires a resource manager");

		m_ResourceManager = std::move(resourceManager);
		m_PatternUploaded = false;
		m_Pattern         = m_ResourceManager->CreateStructBuffer(
			bgpu::StructBufferDesc()
				.SetElement<uint32_t>()
				.SetElementCount(c_PatternWords)
				.SetDebugName("Poison Pattern"));

		if (m_Pattern.IsNull())
		{
			core::throw_runtime_error(
				"BufferPoisoner::Init: the pattern buffer could not be created");
		}
	}

	void
	BufferPoisoner::Release(bool deferred) noexcept
	{
		if (!m_Pattern.IsNull())
		{
			m_ResourceManager->DestroyBuffer(m_Pattern, deferred);
			m_Pattern = bgpu::BufferHandle{};
		}

		m_ResourceManager.Reset();
		m_PatternUploaded = false;
	}

	void
	BufferPoisoner::Poison(bgpu::ICommandList* cmdList, bgpu::BufferHandle buffer) noexcept
	{
		core::ensure(cmdList != nullptr, "BufferPoisoner::Poison requires a command list");
		core::ensure(!m_Pattern.IsNull(), "BufferPoisoner::Poison before Init");
		core::ensure(
			m_ResourceManager->ValidBufferHandle(buffer),
			"BufferPoisoner::Poison on an invalid buffer handle");

		EnsurePattern(cmdList);

		constexpr uint64_t c_PatternBytes = uint64_t{ c_PatternWords } * sizeof(uint32_t);

		const uint64_t byteSize = m_ResourceManager->GetBufferDesc(buffer).byteSize;
		for (uint64_t offset = 0; offset < byteSize; offset += c_PatternBytes)
		{
			cmdList->CopyBuffer(
				buffer,
				m_Pattern,
				offset,
				0,
				std::min(c_PatternBytes, byteSize - offset));
		}
	}

	void
	BufferPoisoner::EnsurePattern(bgpu::ICommandList* cmdList) noexcept
	{
		if (m_PatternUploaded)
		{
			return;
		}

		const auto words = std::vector<uint32_t>(c_PatternWords, c_PoisonWord);
		cmdList->WriteBuffer(m_Pattern, words.data(), words.size() * sizeof(uint32_t));

		cmdList->Barrier(
			m_Pattern,
			bgpu::BufferBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

		m_PatternUploaded = true;
	}
}
