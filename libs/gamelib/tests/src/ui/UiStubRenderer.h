#pragma once
#include <RmlUi/Core/RenderInterface.h>

namespace game::test
{
	/**
	 * A `RenderInterface` that draws nothing and hands back distinct handles.
	 *
	 * Layout, styling, data models and input are all resolved before anything is drawn, so the
	 * cases about them need a render interface only because RmlUi requires one. The real one over
	 * `bgl::IOverlay` is a later task; nothing here depends on it.
	 */
	class UiStubRenderer final : public Rml::RenderInterface
	{
	public:
		UiStubRenderer() = default;

		// Rml::RenderInterface is NonCopyMoveable, so all four are deleted here -- declared rather
		// than left implicit, which MSVC warns about as an error.
		UiStubRenderer(const UiStubRenderer&)     = delete;
		UiStubRenderer(UiStubRenderer&&) noexcept = delete;

		UiStubRenderer&
		operator=(const UiStubRenderer&) = delete;

		UiStubRenderer&
		operator=(UiStubRenderer&&) noexcept = delete;

		Rml::CompiledGeometryHandle
		CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
		{
			return m_NextHandle++;
		}

		void
		RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
		{}

		void
		ReleaseGeometry(Rml::CompiledGeometryHandle) override
		{}

		Rml::TextureHandle
		LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override
		{
			dimensions = Rml::Vector2i(1, 1);
			return m_NextHandle++;
		}

		Rml::TextureHandle
		GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
		{
			return m_NextHandle++;
		}

		void
		ReleaseTexture(Rml::TextureHandle) override
		{}

		void
		EnableScissorRegion(bool) override
		{}

		void
		SetScissorRegion(Rml::Rectanglei) override
		{}

	private:
		// Zero is RmlUi's null handle, so the first one handed out is above it.
		uintptr_t m_NextHandle = 1;
	};
}
