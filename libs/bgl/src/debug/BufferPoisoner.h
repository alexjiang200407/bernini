#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	/**
	 * Overwrites a buffer with a value no correct producer leaves behind, so a consumer that reads
	 * an element its producer never wrote gets an obviously wrong one instead of whatever the
	 * previous frame happened to leave there.
	 *
	 * The FrameGraph drives this for the buffer args a pass declares with AddPoisonedBufferArg.
	 */
	class IBufferPoisoner
	{
	public:
		virtual ~IBufferPoisoner() = default;

		/**
		 * Fills every byte of `buffer` with the poison word.
		 *
		 * @pre `cmdList` is open and `buffer` is in a copy-dest state.
		 */
		virtual void
		Poison(ICommandList* cmdList, BufferHandle buffer) noexcept = 0;
	};

	// A signalling NaN read as a float, and an element index no buffer this engine allocates could
	// hold read as a uint. 0xDEADBEEF on its own is a finite float (-6.3e18) that a solver consumes
	// without complaint, which is the one interpretation poisoning most needs to break.
	inline constexpr uint32_t c_PoisonWord = 0x7FBADBADu;

	/**
	 * Poisons by copying from a GPU-resident chunk of the repeated pattern, tiled over the target.
	 * The copy keeps the fill off the CPU: an upload would stage the whole buffer through host
	 * memory every frame, for every poisoned buffer.
	 */
	class BufferPoisoner final : public IBufferPoisoner
	{
	public:
		BufferPoisoner() noexcept = default;

		~BufferPoisoner() noexcept override = default;

		/**
		 * @throws std::runtime_error if the pattern buffer cannot be allocated.
		 */
		void
		Init(ResourceManagerRef resourceManager);

		void
		Release(bool deferred = true) noexcept;

		void
		Poison(ICommandList* cmdList, BufferHandle buffer) noexcept override;

	private:
		// Uploaded on the first Poison, which is the first point there is a command list to record
		// it on.
		void
		EnsurePattern(ICommandList* cmdList) noexcept;

		// One chunk, tiled: a buffer large enough for the biggest poisoned buffer would be sized by
		// whatever the scene happens to hold, and the copies cost the same either way.
		static constexpr uint32_t c_PatternWords = 16 * 1024;

		ResourceManagerRef m_ResourceManager;
		BufferHandle       m_Pattern;
		bool               m_PatternUploaded = false;
	};
}
