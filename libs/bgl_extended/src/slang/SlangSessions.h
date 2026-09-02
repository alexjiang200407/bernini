#pragma once

namespace bgl
{
	/** What every compile on one backend shares: the code it generates and where sources are. */
	struct SlangSessionDesc
	{
		SlangCompileTarget           target = SLANG_TARGET_UNKNOWN;
		std::span<const char* const> searchPaths;
	};

	/**
	 * The Slang sessions a device compiles through: one per thread that compiles.
	 *
	 * A global session and everything created from it are not thread-safe, but distinct global
	 * sessions may run in parallel (slang.h, IGlobalSession). So the first compile on a thread
	 * creates that thread's own global session and session, and a session is only ever used by
	 * the thread it was created for. A global session loads Slang's core module, a few hundred
	 * megabytes that stay resident -- which is why none exists until a compile reaches it, and why
	 * the renderer drops them all as soon as its pipelines are built.
	 */
	class SlangSessions final
	{
	public:
		explicit SlangSessions(SlangSessionDesc desc) noexcept;

		SlangSessions(const SlangSessions&) = delete;

		SlangSessions&
		operator=(const SlangSessions&) = delete;

		/**
		 * The calling thread's session, created on first call.
		 *
		 * @post the session belongs to the calling thread until ReleaseAll; using it from another
		 *       thread is a data race inside Slang. Entries are keyed by thread id, so a thread that
		 *       starts after another ended may inherit that one's sessions -- which is still one
		 *       live thread per session, the only thing Slang asks for.
		 */
		[[nodiscard]] slang::ISession*
		ForThisThread() noexcept;

		/**
		 * Releases every thread's sessions and the modules they parsed. A later compile recreates
		 * them, so this only reclaims memory -- it does not disable compilation.
		 *
		 * @pre no slang::IModule or IComponentType obtained from any session is still held: they
		 *      keep the session alive, and any raw pointer to one dangles once it is dropped.
		 */
		void
		ReleaseAll() noexcept;

	private:
		struct ThreadSessions
		{
			// The global session is declared first so it is destroyed after the session it made.
			Slang::ComPtr<slang::IGlobalSession> global;
			Slang::ComPtr<slang::ISession>       session;
		};

		SlangSessionDesc m_Desc;

		std::mutex                                          m_Mutex;
		std::unordered_map<std::thread::id, ThreadSessions> m_ByThread;
	};
}
