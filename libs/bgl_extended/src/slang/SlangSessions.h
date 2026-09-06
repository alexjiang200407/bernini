#pragma once

#include <filesystem>
#include <mutex>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
namespace bgl
{
	/**
	 * A module given as text rather than found on a search path. Loaded into every session under
	 * `name` before anything else compiles, so an `import` of that name resolves to it, and a file
	 * of the same name on a search path is shadowed. `name` is spelled as an import spells it --
	 * `game.slot0` -- and never as a path.
	 */
	struct SlangSourceModule
	{
		std::string name;
		std::string source;
	};

	/**
	 * The module name as Slang's loader keys it: `/`-separated, not `.`-separated. `loadModule`
	 * appends `.slang` to it and opens that, and an `import` of a dotted name looks a loaded module
	 * up under the same form -- so the one spelling every caller and every shader uses is converted
	 * here, at the only point that reaches the loader.
	 */
	[[nodiscard]] std::string
	SlangModulePath(std::string_view moduleName);

	/** What every compile on one backend shares: the code it generates and where sources are. */
	struct SlangSessionDesc
	{
		SlangCompileTarget             target = SLANG_TARGET_UNKNOWN;
		std::vector<std::string>       searchPaths;
		std::vector<SlangSourceModule> sourceModules;
	};

	/**
	 * The engine's staged tree and the suite's, then `clientDir` when it is not empty. One list for
	 * the session and the cache salt, so a module either resolves and keys correctly or does neither.
	 */
	[[nodiscard]] std::vector<std::string>
	ShaderSearchPaths(const std::filesystem::path& clientDir);

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

		[[nodiscard]] const std::vector<std::string>&
		GetSearchPaths() const noexcept
		{
			return m_Desc.searchPaths;
		}

		/**
		 * Adds a module every session from now on loads from source before it compiles anything.
		 *
		 * Drops every existing session the way ReleaseAll does, since a session that has already
		 * resolved the name to a file keeps that answer; the next compile on each thread recreates
		 * its session with the module in place.
		 *
		 * @pre ReleaseAll's, and no compile in flight on any thread: a session being created reads
		 *      the list this appends to.
		 */
		void
		AddSourceModule(SlangSourceModule sourceModule) noexcept;

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
