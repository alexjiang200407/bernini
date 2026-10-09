#pragma once
#include <bgpu/ProgramCache.h>
#include <bgpu/api.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <filesystem>
#include <slang.h>
#include <string>
#include <string_view>
#include <vector>

namespace bgpu
{
	enum class LogLevel
	{
		kTrace = 0,
		kDebug,
		kInfo,
		kWarn,
		kError,
		kCritical,
		kOff,
	};

	struct GpuContextDesc
	{
		// D3D12's API validation layer or Vulkan's, and the callback that routes its messages into
		// the log. Metal's validators are environment variables the runtime reads before this
		// process gets a say (docs/gfx_debug.md), so there the flag only labels the run.
		bool enableDebugLayer = false;

		// GPU-based validation on top of the debug layer, GPU-assisted validation on Vulkan: every
		// shader is patched, so device creation is several times slower and no driver pipeline
		// library is written.
		bool enableGPUValidationLayer = false;

		// Loads the PIX GPU capturer before the device exists, which is the only time it can be.
		// D3D12 only.
		bool enablePixDebug = false;

		// A debug-layer warning or error ends the process instead of being logged, and so does an
		// object that outlives the context.
		bool strictError = false;

		LogLevel logLevel = LogLevel::kError;

		// A directory of the client's own Slang modules, searched after the engine's staged tree, so
		// a program can import one by name. Every file under it is in every owner's shader-cache
		// salt. Empty means none.
		std::filesystem::path clientShaderDir;

		// Where every owner's compiled programs persist across runs (GpuContext::GetProgramCache),
		// relative to the working directory. Empty disables the cache for every owner.
		std::filesystem::path shaderCacheDir;

		// Asks the driver to hold the GPU at its maximum clock however idle a frame leaves it, so a
		// vsync'd window's light frames do not drop it to a clock a heavy frame then overruns at.
		// A game turns it on; a tool leaves it off. GpuContext::GetMaximumPerformance says what came
		// of it (docs/bgpu.md § Maximum performance).
		bool preferMaximumPerformance = false;

		bool
		operator==(const GpuContextDesc&) const = default;
	};

	/** What came of GpuContextDesc::preferMaximumPerformance. */
	enum class MaximumPerformance
	{
		// The desc did not ask: the driver manages the clock.
		kNotRequested,

		// The desc asked and this backend, GPU or driver has no way to: the driver manages the
		// clock, and bgpu.log says why.
		kUnavailable,

		// The driver accepted the request. On Vulkan it rides on each swapchain, so a context that
		// presents nothing holds no clock.
		kRequested,
	};

	/**
	 * A module given as text rather than found on a search path. Loaded into every session under
	 * `name` before anything else compiles, so an `import` of that name resolves to it, and a file
	 * of the same name on a search path is shadowed. `name` is spelled as an import spells it --
	 * `game.slot0`, `programs.forward.GameSlot0` -- and never as a path.
	 */
	struct SlangSourceModule
	{
		std::string name;
		std::string source;

		// False for a program nothing imports: loaded on its first LoadModule, not into every session.
		bool imported = true;
	};

	/**
	 * The one GPU device of a process and the compiler for it, shared by every library that runs
	 * work on that device: the renderer, and a client that runs its own compute beside the frame.
	 *
	 * The application creates it and hands it to each owner. It holds the strong reference to the
	 * native device, so the device dies with the context's last holder -- which is not when its
	 * work is done: each owner drains the queues it created before dropping its reference, and
	 * nothing here flushes "the device", because no owner has all of its queues.
	 *
	 * The Slang sessions are per thread, created on a thread's first compile and dropped by
	 * ReleaseSlangSessions, whose precondition is shared by every owner. An owner reaches the
	 * native device through its own IDevice's GetNativeObject.
	 */
	class GpuContext : public core::Ref
	{
	public:
		GpuContext(const GpuContext&) noexcept = delete;
		GpuContext(GpuContext&&) noexcept      = delete;

		GpuContext&
		operator=(const GpuContext&) noexcept = delete;

		GpuContext&
		operator=(GpuContext&&) noexcept = delete;

		[[nodiscard]] virtual const GpuContextDesc&
		GetDesc() const noexcept = 0;

		/**
		 * Whether GPU validation is running by whatever route turns it on: the desc's flag, or on
		 * Metal the environment. A shader cache reads it to drop its driver-pipeline layer.
		 */
		[[nodiscard]] virtual bool
		GpuValidationActive() const noexcept = 0;

		/** What the driver was told of the desc's preferMaximumPerformance, settled at creation. */
		[[nodiscard]] virtual MaximumPerformance
		GetMaximumPerformance() const noexcept = 0;

		/**
		 * The engine's staged tree and the suite's, then the desc's client directory when it is not
		 * empty. One list for every session and every owner's cache salt, so a module either
		 * resolves and keys correctly or does neither.
		 */
		[[nodiscard]] virtual const std::vector<std::string>&
		GetShaderSearchPaths() const noexcept = 0;

		/**
		 * Adds a module every session from now on loads from source before it compiles anything. A
		 * name maps to one text per context: registering a name again replaces the text, and
		 * registering the same text again does nothing -- so a second owner binding what the first
		 * bound costs no session. A new or changed text drops every existing session the way
		 * ReleaseSlangSessions does, since a session that has already resolved the name keeps that
		 * answer.
		 *
		 * A changed text re-points every owner's later compiles, and every owner's cache key follows
		 * it through GetSourceSalt -- but a layout an owner reflected from the old text, a renderer's
		 * surface types say, does not. Change a text only before the owners that read it exist.
		 *
		 * @pre ReleaseSlangSessions's, on every owner, when the text is new or changed.
		 */
		virtual void
		AddSourceModule(SlangSourceModule sourceModule) noexcept = 0;

		/**
		 * The fold of every registered module, name and text, order-independent: what an owner's
		 * shader cache mixes into each key, so a key describes the modules the sessions hold now.
		 */
		[[nodiscard]] virtual uint64_t
		GetSourceSalt() const noexcept = 0;

		/**
		 * The store every owner keeps what it compiled in, keyed by what the sessions compile it
		 * from. Null when the desc's `shaderCacheDir` is empty.
		 */
		[[nodiscard]] virtual const ProgramCache*
		GetProgramCache() const noexcept = 0;

		/**
		 * The named module in the calling thread's session: a registered module nothing imports is
		 * loaded from its text on first request, anything else by name as an import would find it.
		 * Fatal on a diagnostic, like every other load of the engine's own shaders.
		 *
		 * @post the module belongs to the calling thread's session until ReleaseSlangSessions; using
		 *       it from another thread is a data race inside Slang.
		 */
		[[nodiscard]] virtual slang::IModule*
		LoadModule(std::string_view moduleName) noexcept = 0;

		/**
		 * The named module compiled for a DXIL target whatever this device draws with, so a layout
		 * read from it is the scalar one a raw load reads a record at on every backend. A registered
		 * module nothing imports is loaded from its text, as LoadModule loads it. Null, with the
		 * compiler's diagnostic, when the module does not compile.
		 *
		 * @post as LoadModule's: the module belongs to the calling thread's session.
		 */
		[[nodiscard]] virtual slang::IModule*
		LoadScalarLayoutModule(std::string_view moduleName, std::string& diagnostic) = 0;

		/**
		 * Drops every thread's Slang session -- a few hundred resident megabytes apiece once a
		 * cold-cache compile has stood one up. A later compile recreates what it needs.
		 *
		 * @pre no compile is in flight and no slang:: object is held, by any owner of this context.
		 */
		virtual void
		ReleaseSlangSessions() noexcept = 0;

	protected:
		GpuContext() noexcept = default;
	};

	using GpuContextRef = core::SharedRef<GpuContext>;

	/**
	 * Creates the process's device: the debug layer and its message routing where the desc asks,
	 * the PIX capturer, the native device, the log file, and the compiler for it.
	 *
	 * One context is live per process at a time -- D3D12 hands back one device per adapter, and
	 * enabling the debug layer once it exists removes it -- so the application creates it once and
	 * hands it to every owner. Another may follow once the last holder has dropped it.
	 *
	 * @throws UnsupportedSystem if the machine is below the engine's minimum requirements
	 *         (SystemRequirements.h); std::runtime_error if no device can be created, a context is
	 *         already live, or on Vulkan the debug layer was asked for and is not installed.
	 */
	BGPU_API GpuContextRef
	CreateGpuContext(const GpuContextDesc& desc);
}
