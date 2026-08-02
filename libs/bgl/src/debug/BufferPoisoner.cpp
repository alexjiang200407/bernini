#include "debug/BufferPoisoner.h"

#include <core/err/util.h>

namespace bgl
{
	void
	BufferPoisoner::Init(ResourceManagerRef resourceManager)
	{
		gassert(resourceManager != nullptr, "BufferPoisoner::Init requires a resource manager");

		m_ResourceManager = std::move(resourceManager);
		m_PatternUploaded = false;
		m_Pattern         = m_ResourceManager->CreateStructBuffer(
			StructBufferDesc()
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
			m_Pattern = BufferHandle{};
		}

		m_ResourceManager.Reset();
		m_PatternUploaded = false;
	}

	void
	BufferPoisoner::Poison(ICommandList* cmdList, BufferHandle buffer) noexcept
	{
		gassert(cmdList != nullptr, "BufferPoisoner::Poison requires a command list");
		gassert(!m_Pattern.IsNull(), "BufferPoisoner::Poison before Init");
		gassert(
			m_ResourceManager->ValidBufferHandle(buffer),
			"BufferPoisoner::Poison on an invalid buffer handle");

		EnsurePattern(cmdList);

		constexpr uint64_t c_PatternBytes = uint64_t{ c_PatternWords } * sizeof(uint32_t);

		const uint64_t byteSize = m_ResourceManager->GetBufferByteSize(buffer);
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
	BufferPoisoner::EnsurePattern(ICommandList* cmdList) noexcept
	{
		if (m_PatternUploaded)
		{
			return;
		}

		const auto words = std::vector<uint32_t>(c_PatternWords, c_PoisonWord);
		cmdList->WriteBuffer(m_Pattern, words.data(), words.size() * sizeof(uint32_t));

		cmdList->Barrier(
			m_Pattern,
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kCopy)
				.AddAccessBefore(BarrierAccessFlag::kCopyDest)
				.AddSyncAfter(BarrierSyncFlag::kCopy)
				.AddAccessAfter(BarrierAccessFlag::kCopySource));

		m_PatternUploaded = true;
	}
}
