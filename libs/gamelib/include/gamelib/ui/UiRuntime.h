#pragma once
#include <assetlib/AssetStore.h>

// The library reaches a client through this header alone, and never through a bernini one: a game
// that drives a context includes <RmlUi/Core.h> itself. See docs/ui_runtime.md, Replacing RmlUi.
namespace Rml
{
	class Context;
	class ElementDocument;
	class RenderInterface;
}

// The one C type this header names, spelled as Lua and RmlUi's plugin both spell it.
typedef struct lua_State lua_State;

namespace game
{
	/**
	 * One RmlUi context, sized in pixels, and the documents loaded into it.
	 *
	 * The context is handed out as `Rml::Context&` rather than wrapped: a game registers its data
	 * models and event listeners with RmlUi's own API, which is the corpus the library was chosen
	 * for. What gamelib owns is lifetime, the interfaces underneath, and the mount a document is
	 * read from.
	 */
	class UiContext final
	{
	public:
		~UiContext() noexcept;

		UiContext(const UiContext&)     = delete;
		UiContext(UiContext&&) noexcept = delete;

		UiContext&
		operator=(const UiContext&) = delete;

		UiContext&
		operator=(UiContext&&) noexcept = delete;

		/**
		 * The context itself. Update, render, input and data models are RmlUi's API, called here.
		 *
		 * @return a reference that stays valid for this UiContext's lifetime.
		 */
		[[nodiscard]] Rml::Context&
		Get() noexcept;

		/**
		 * Loads a document by mount key -- `Authored/UI/menu.rml` -- through the project's file
		 * system, so a loose tree and an archive read alike. The document is *not* shown; a caller
		 * decides when, with `ElementDocument::Show`.
		 *
		 * @throws std::runtime_error if the key is absent or the document does not parse.
		 * @return the document, owned by the context.
		 */
		Rml::ElementDocument*
		LoadDocument(std::string_view key);

		/** The output size the context lays out against, in pixels. */
		void
		SetDimensions(uint32_t width, uint32_t height);

	private:
		friend class UiRuntime;

		UiContext(Rml::Context& context, std::string name) noexcept;

		Rml::Context& m_Context;
		std::string   m_Name;
	};

	// Owned by the caller; the runtime it came from must outlive it.
	using UiContextPtr = std::unique_ptr<UiContext>;

	/** What a runtime is built with beyond its two required collaborators. */
	struct UiRuntimeOptions
	{
		/**
		 * Whether a document may script itself: `<script>` blocks, inline `onclick`, and data
		 * models declared from Lua tables. Off by default -- a game that binds everything from C++
		 * never creates a VM.
		 *
		 * With it on, RmlUi's `body` tag builds a `LuaDocument` rather than a plain
		 * `ElementDocument`, which is what gives a document its own scope.
		 *
		 * **A scripted document is trusted code.** The plugin opens Lua's standard libraries, `io`,
		 * `os` and `package` among them, so a `<script>` reaches the host directly -- ADR-8's mount
		 * confinement bounds which files the *document loader* resolves, and does not extend to
		 * what a script does once it runs. Turn this on for documents the project ships, not for
		 * ones it receives.
		 */
		bool scripting = false;

		/**
		 * The state RmlUi's bindings are added to, or null for one the plugin makes and closes
		 * itself. Ignored unless `scripting`.
		 *
		 * A state you pass is yours to close, and only *after* the runtime is destroyed -- RmlUi
		 * unregisters from it during `Rml::Shutdown`. It must also arrive with its standard
		 * libraries already opened: RmlUi opens them only for a state it created itself, and a
		 * document that hits a missing one raises outside any protected call, which aborts the
		 * process rather than failing the load.
		 *
		 * This is the seam the engine-wide VM arrives through: when one exists it is handed in
		 * here and the UI stops keeping its own -- and a document's `<script>` then reaches
		 * whatever that state has bound into it.
		 */
		lua_State* luaState = nullptr;
	};

	/**
	 * RmlUi's process-global lifetime, and the two interfaces that bind it to bernini: the clock and
	 * log, and the file system every document, stylesheet and font is read through.
	 *
	 * **One per process**, asserted -- `Rml::Initialise` is global, and so are the interfaces a
	 * second instance would install over the first. The runtime outlives every context it creates.
	 *
	 * The render interface is the caller's: a headless test passes a stub, and a drawing client
	 * passes the one over `bgl::IOverlay`. It must outlive this runtime.
	 */
	class UiRuntime final
	{
	public:
		/**
		 * @param store   the project the documents are read from; must outlive this runtime.
		 * @param renderer what RmlUi draws through; must outlive this runtime.
		 * @throws std::runtime_error if RmlUi fails to initialise.
		 */
		UiRuntime(
			const assetlib::AssetStore& store,
			Rml::RenderInterface&       renderer,
			const UiRuntimeOptions&     options = {});
		~UiRuntime() noexcept;

		UiRuntime(const UiRuntime&)     = delete;
		UiRuntime(UiRuntime&&) noexcept = delete;

		UiRuntime&
		operator=(const UiRuntime&) = delete;

		UiRuntime&
		operator=(UiRuntime&&) noexcept = delete;

		/**
		 * @param name unique in this process.
		 * @throws std::runtime_error if the name is taken or the context cannot be created.
		 */
		[[nodiscard]] UiContextPtr
		CreateContext(std::string name, uint32_t width, uint32_t height);

		/**
		 * Registers a font face by mount key -- `Authored/Fonts/Lato-Regular.ttf`. A document that
		 * lays out text needs at least one, and RmlUi holds them process-wide rather than per
		 * context.
		 *
		 * @throws std::runtime_error if the key is absent or the face does not load.
		 */
		void
		LoadFontFace(std::string_view key, bool fallbackFace = false);

		/**
		 * Advances the clock `SystemInterface::GetElapsedTime` reports, which is what drives
		 * transitions, animations and any `Rml::Timer`. The client owns the frame's dt; nothing here
		 * reads a wall clock, so a headless test steps time exactly (ADR-9).
		 */
		void
		AdvanceTime(double seconds) noexcept;

		[[nodiscard]] double
		GetElapsedTime() const noexcept;

		/** Whether documents may script themselves -- `UiRuntimeOptions::scripting`, as built. */
		[[nodiscard]] bool
		ScriptingEnabled() const noexcept;

	private:
		struct Interfaces;

		std::unique_ptr<Interfaces> m_Interfaces;
		bool                        m_Scripting = false;
	};
}
