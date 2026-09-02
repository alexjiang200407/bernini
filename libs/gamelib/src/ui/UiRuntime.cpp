#include <gamelib/ui/UiRuntime.h>

#include "ui/UiFileInterface.h"
#include "ui/UiSystemInterface.h"

#include <RmlUi/Core.h>
#include <core/err/util.h>
#include <tracy/Tracy.hpp>

namespace game
{
	namespace
	{
		// Rml::Initialise and the interfaces behind it are process-global, so a second runtime would
		// install its own over the first's and free them under it on the way out.
		std::atomic<bool> g_Live{ false };
	}

	struct UiRuntime::Interfaces
	{
		UiSystemInterface system;
		UiFileInterface   files;

		explicit Interfaces(const core::file::IFileSystem& mount) noexcept : files(mount) {}

		Interfaces(const Interfaces&)     = delete;
		Interfaces(Interfaces&&) noexcept = delete;

		Interfaces&
		operator=(const Interfaces&) = delete;

		Interfaces&
		operator=(Interfaces&&) noexcept = delete;
	};

	UiRuntime::UiRuntime(const assetlib::AssetStore& store, Rml::RenderInterface& renderer)
	{
		core::throw_runtime_error_if(
			g_Live.exchange(true),
			"game::UiRuntime: one per process -- RmlUi's lifetime and interfaces are global");

		m_Interfaces = std::make_unique<Interfaces>(store.GetFiles());

		Rml::SetSystemInterface(&m_Interfaces->system);
		Rml::SetFileInterface(&m_Interfaces->files);
		Rml::SetRenderInterface(&renderer);

		if (!Rml::Initialise())
		{
			Rml::SetRenderInterface(nullptr);
			Rml::SetFileInterface(nullptr);
			Rml::SetSystemInterface(nullptr);
			m_Interfaces.reset();
			g_Live.store(false);
			throw std::runtime_error("game::UiRuntime: Rml::Initialise failed");
		}

		logger::info("UI runtime up on RmlUi {}", Rml::GetVersion());
	}

	UiRuntime::~UiRuntime() noexcept
	{
		// Shutdown frees every context and document, so it runs before the interfaces they read.
		Rml::Shutdown();

		Rml::SetRenderInterface(nullptr);
		Rml::SetFileInterface(nullptr);
		Rml::SetSystemInterface(nullptr);

		m_Interfaces.reset();
		g_Live.store(false);
	}

	UiContextPtr
	UiRuntime::CreateContext(std::string name, uint32_t width, uint32_t height)
	{
		core::throw_runtime_error_if(
			name.empty(),
			"game::UiRuntime::CreateContext: a context needs a name");

		core::throw_runtime_error_if(
			width == 0 || height == 0,
			"game::UiRuntime::CreateContext: '{}' needs a non-empty size",
			name);

		core::throw_runtime_error_if(
			Rml::GetContext(name) != nullptr,
			"game::UiRuntime::CreateContext: '{}' is already a context in this process",
			name);

		Rml::Context* context = Rml::CreateContext(
			name,
			Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));

		core::throw_runtime_error_if(
			context == nullptr,
			"game::UiRuntime::CreateContext: RmlUi refused a context named '{}'",
			name);

		return UiContextPtr(new UiContext(*context, std::move(name)));
	}

	void
	UiRuntime::LoadFontFace(std::string_view key, bool fallbackFace)
	{
		ZoneScopedN("game LoadFontFace");
		ZoneTextF("%.*s", static_cast<int>(key.size()), key.data());

		core::throw_runtime_error_if(
			!Rml::LoadFontFace(std::string(key), fallbackFace),
			"game::UiRuntime::LoadFontFace: '{}' is not a font face this build can read",
			key);
	}

	void
	UiRuntime::AdvanceTime(double seconds) noexcept
	{
		m_Interfaces->system.Advance(seconds);
	}

	double
	UiRuntime::GetElapsedTime() const noexcept
	{
		return m_Interfaces->system.Elapsed();
	}

	UiContext::UiContext(Rml::Context& context, std::string name) noexcept :
		m_Context(context), m_Name(std::move(name))
	{}

	UiContext::~UiContext() noexcept
	{
		// False when the runtime went first, which the header forbids -- and by then Get() has been
		// handing out a dangling reference anyway, so there is nothing for a check here to save.
		static_cast<void>(Rml::RemoveContext(m_Name));
	}

	Rml::Context&
	UiContext::Get() noexcept
	{
		return m_Context;
	}

	Rml::ElementDocument*
	UiContext::LoadDocument(std::string_view key)
	{
		ZoneScopedN("game LoadDocument");
		ZoneTextF("%.*s", static_cast<int>(key.size()), key.data());

		Rml::ElementDocument* document = m_Context.LoadDocument(std::string(key));

		core::throw_runtime_error_if(
			document == nullptr,
			"game::UiContext::LoadDocument: '{}' is absent or does not parse; the log above says "
			"which",
			key);

		return document;
	}

	void
	UiContext::SetDimensions(uint32_t width, uint32_t height)
	{
		core::throw_runtime_error_if(
			width == 0 || height == 0,
			"game::UiContext::SetDimensions: a context needs a non-empty size");

		m_Context.SetDimensions(Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
	}
}
