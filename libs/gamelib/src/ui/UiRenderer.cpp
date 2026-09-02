#include <gamelib/ui/UiRenderer.h>

#include "ui/UiSystemInterface.h"

#include <RmlUi/Core.h>
#include <core/err/util.h>
#include <tracy/Tracy.hpp>

namespace game
{
	namespace
	{
		constexpr std::string_view c_TargetScheme = "target://";

		// Straight alpha: an _SRGB view would otherwise weight a half-covered edge by 0.5^2.2
		// rather than 0.5 (ADR-12).
		[[nodiscard]] assetlib::ImageData
		UnpremultipliedImage(std::span<const Rml::byte> source, Rml::Vector2i size)
		{
			const auto width  = static_cast<uint32_t>(size.x);
			const auto height = static_cast<uint32_t>(size.y);
			const auto texels = static_cast<size_t>(width) * height;

			auto img     = assetlib::ImageData();
			img.width    = width;
			img.height   = height;
			img.vkFormat = assetlib::VkFormat::R8G8B8A8_SRGB;
			img.pixels   = core::fixed_buffer<std::byte>(texels * 4);
			img.subresources.push_back({ 0, static_cast<uint32_t>(width) * 4, texels * 4 });

			for (size_t t = 0; t < texels; ++t)
			{
				const uint8_t alpha = static_cast<uint8_t>(source[t * 4 + 3]);

				for (size_t c = 0; c < 3; ++c)
				{
					const auto premultiplied = static_cast<uint32_t>(source[t * 4 + c]);

					// Alpha zero carries no colour; anything divided out of it is noise.
					const uint32_t straight =
						alpha == 0 ?
							0 :
							std::min<uint32_t>(255, (premultiplied * 255 + alpha / 2) / alpha);

					img.pixels[t * 4 + c] = static_cast<std::byte>(straight);
				}

				img.pixels[t * 4 + 3] = static_cast<std::byte>(alpha);
			}

			return img;
		}

		[[nodiscard]] uint32_t
		PackColor(Rml::ColourbPremultiplied colour) noexcept
		{
			return static_cast<uint32_t>(colour.red) | (static_cast<uint32_t>(colour.green) << 8) |
			       (static_cast<uint32_t>(colour.blue) << 16) |
			       (static_cast<uint32_t>(colour.alpha) << 24);
		}
	}

	/**
	 * The RmlUi half. A handle RmlUi holds is an index into a vector here rather than a bgl handle
	 * cast to an integer: bgl's handles are two words, and RmlUi's are one.
	 */
	class UiRenderer::Impl final : public Rml::RenderInterface
	{
	public:
		Impl(bgl::IGraphics& graphics, const assetlib::AssetStore& store) :
			m_Store(store), m_Overlay(graphics.CreateOverlay())
		{
			core::throw_runtime_error_if(
				m_Overlay == nullptr,
				"game::UiRenderer: the graphics did not create an overlay");
		}

		~Impl() noexcept override = default;

		Impl(const Impl&)     = delete;
		Impl(Impl&&) noexcept = delete;

		Impl&
		operator=(const Impl&) = delete;

		Impl&
		operator=(Impl&&) noexcept = delete;

		Rml::CompiledGeometryHandle
		CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
			override
		{
			auto converted = std::vector<bgl::OverlayVertex>(vertices.size());
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				converted[i].position = { vertices[i].position.x, vertices[i].position.y };
				converted[i].uv       = { vertices[i].tex_coord.x, vertices[i].tex_coord.y };
				converted[i].color    = PackColor(vertices[i].colour);
			}

			auto asUnsigned = std::vector<uint32_t>(indices.size());
			std::ranges::transform(indices, asUnsigned.begin(), [](const int index) {
				return static_cast<uint32_t>(index);
			});

			try
			{
				return m_Geometry.Store(m_Overlay->CreateGeometry(converted, asUnsigned));
			}
			catch (const std::exception& e)
			{
				logger::error("UI geometry could not be compiled: {}", e.what());
				return 0;
			}
		}

		void
		RenderGeometry(
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f               translation,
			Rml::TextureHandle          texture) override
		{
			const bgl::OverlayGeometryHandle* compiled = m_Geometry.Find(geometry);
			if (compiled == nullptr)
			{
				return;
			}

			auto draw        = bgl::OverlayDraw();
			draw.geometry    = *compiled;
			draw.translation = { translation.x, translation.y };

			if (const bgl::OverlayTextureHandle* found = m_Textures.Find(texture); found != nullptr)
			{
				draw.texture = *found;
			}

			if (m_ScissorEnabled)
			{
				draw.scissor = m_Scissor;
			}

			if (m_HasTransform)
			{
				draw.transform = m_Transform;
			}

			m_Draws.push_back(draw);
		}

		void
		ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
		{
			// RmlUi releases from a noexcept destructor, so an escape here is std::terminate
			// rather than a logged failure -- unlike every other override in this class.
			try
			{
				if (bgl::OverlayGeometryHandle* compiled = m_Geometry.Find(geometry);
				    compiled != nullptr)
				{
					m_Overlay->ReleaseGeometry(*compiled);
					m_Geometry.Release(geometry);
				}
			}
			catch (const std::exception& e)
			{
				logger::error("UI geometry could not be released: {}", e.what());
			}
		}

		Rml::TextureHandle
		LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
		{
			ZoneScopedN("game LoadTexture");
			ZoneTextF("%.*s", static_cast<int>(source.size()), source.data());

			try
			{
				if (source.starts_with(c_TargetScheme))
				{
					return LoadTargetTexture(dimensions, source);
				}

				core::throw_runtime_error_if(
					IsSchemeSource(source),
					"'{}' carries a scheme this runtime does not resolve; only '{}' is",
					source,
					c_TargetScheme);

				assetlib::ImageData img = m_Store.LoadTexture(source);
				dimensions =
					Rml::Vector2i(static_cast<int>(img.width), static_cast<int>(img.height));

				return m_Textures.Store(m_Overlay->CreateTexture(std::move(img), source));
			}
			catch (const std::exception& e)
			{
				logger::error("UI texture '{}' could not be loaded: {}", source, e.what());
				return 0;
			}
		}

		Rml::TextureHandle
		GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i size) override
		{
			ZoneScopedN("game GenerateTexture");
			ZoneTextF("%dx%d", size.x, size.y);

			if (size.x <= 0 || size.y <= 0 ||
			    source.size() < static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * 4)
			{
				logger::error(
					"UI texture generation was handed {}x{} and too few bytes",
					size.x,
					size.y);
				return 0;
			}

			try
			{
				return m_Textures.Store(
					m_Overlay->CreateTexture(UnpremultipliedImage(source, size), "UI Generated"));
			}
			catch (const std::exception& e)
			{
				logger::error("UI texture could not be generated: {}", e.what());
				return 0;
			}
		}

		void
		ReleaseTexture(Rml::TextureHandle texture) override
		{
			try
			{
				if (bgl::OverlayTextureHandle* found = m_Textures.Find(texture); found != nullptr)
				{
					m_Overlay->ReleaseTexture(*found);
					m_Textures.Release(texture);
				}
			}
			catch (const std::exception& e)
			{
				logger::error("UI texture could not be released: {}", e.what());
			}
		}

		void
		EnableScissorRegion(bool enable) override
		{
			m_ScissorEnabled = enable;
		}

		void
		SetScissorRegion(Rml::Rectanglei region) override
		{
			m_Scissor =
				bgl::OverlayRect{ region.Left(), region.Top(), region.Width(), region.Height() };
		}

		void
		SetTransform(const Rml::Matrix4f* transform) override
		{
			m_HasTransform = transform != nullptr;
			if (m_HasTransform)
			{
				// Column-major on both sides, so data() is already glm's layout. Asserted because
				// the majorness is a build-config switch (RMLUI_MATRIX_ROW_MAJOR) that would make
				// this a transpose without changing a line here.
				static_assert(std::is_same_v<Rml::Matrix4f, Rml::ColumnMajorMatrix4f>);
				std::memcpy(&m_Transform, transform->data(), sizeof(m_Transform));
			}
		}

		void
		Submit(bgl::IGraphics& graphics)
		{
			if (!m_Draws.empty())
			{
				graphics.DrawOverlay(bgl::OverlayJob{ m_Overlay, m_Draws });
			}

			m_Draws.clear();
		}

		void
		BeginRecording() noexcept
		{
			m_Draws.clear();
			m_ScissorEnabled = false;
			m_HasTransform   = false;
		}

		void
		RegisterTarget(std::string name, bgl::RenderTargetRef target)
		{
			m_Targets.insert_or_assign(std::move(name), std::move(target));
		}

	private:
		/**
		 * The bgl handles RmlUi holds, addressed by the one-based index it holds instead: bgl's are
		 * two words and RmlUi's are one, so the table is what bridges them. Zero is RmlUi's null.
		 *
		 * A released slot is reused rather than retired, because RmlUi recompiles per changed
		 * element and not once per document: a ticking readout releases and compiles one geometry
		 * every frame, which a table that only grew would leak for as long as the game runs.
		 */
		template <typename T>
		struct HandleTable
		{
			std::vector<T>      entries;
			std::vector<size_t> free;

			[[nodiscard]] uintptr_t
			Store(T handle)
			{
				if (free.empty())
				{
					entries.push_back(handle);
					return entries.size();
				}

				const size_t index = free.back();
				free.pop_back();
				entries[index] = handle;
				return index + 1;
			}

			// Null for a handle that is zero, out of range, or already released.
			[[nodiscard]] T*
			Find(uintptr_t handle) noexcept
			{
				if (handle == 0 || handle > entries.size())
				{
					return nullptr;
				}

				T& entry = entries[handle - 1];
				return entry.IsValid() ? &entry : nullptr;
			}

			// Frees the slot behind a handle Find accepted. A second release finds nothing, so an
			// index cannot enter the free list twice.
			void
			Release(uintptr_t handle)
			{
				entries[handle - 1] = {};
				free.push_back(handle - 1);
			}
		};

		Rml::TextureHandle
		LoadTargetTexture(Rml::Vector2i& dimensions, const Rml::String& source)
		{
			const std::string name = source.substr(c_TargetScheme.size());

			const auto it = m_Targets.find(name);
			core::throw_runtime_error_if(
				it == m_Targets.end(),
				"no render target is registered as '{}'",
				name);

			dimensions = Rml::Vector2i(
				static_cast<int>(it->second->GetWidth()),
				static_cast<int>(it->second->GetHeight()));

			return m_Textures.Store(m_Overlay->CreateTexture(it->second));
		}

		const assetlib::AssetStore& m_Store;
		bgl::OverlayRef             m_Overlay;

		HandleTable<bgl::OverlayGeometryHandle> m_Geometry;
		HandleTable<bgl::OverlayTextureHandle>  m_Textures;
		std::vector<bgl::OverlayDraw>           m_Draws;

		std::unordered_map<std::string, bgl::RenderTargetRef> m_Targets;

		bgl::OverlayRect m_Scissor;
		glm::mat4        m_Transform{ 1.0f };
		bool             m_ScissorEnabled = false;
		bool             m_HasTransform   = false;
	};

	UiRenderer::UiRenderer(bgl::IGraphics& graphics, const assetlib::AssetStore& store) :
		m_Impl(std::make_unique<Impl>(graphics, store))
	{}

	UiRenderer::~UiRenderer() noexcept = default;

	Rml::RenderInterface&
	UiRenderer::Interface() noexcept
	{
		return *m_Impl;
	}

	void
	UiRenderer::Render(bgl::IGraphics& graphics, UiContext& context)
	{
		m_Impl->BeginRecording();

		// Calls back into the interface rather than drawing; the draws are what it leaves behind.
		context.Get().Render();

		m_Impl->Submit(graphics);
	}

	void
	UiRenderer::RegisterTarget(std::string name, bgl::RenderTargetRef target)
	{
		core::throw_runtime_error_if(
			name.empty(),
			"game::UiRenderer::RegisterTarget: a target needs a name");

		core::throw_runtime_error_if(
			target == nullptr,
			"game::UiRenderer::RegisterTarget: '{}' needs a target",
			name);

		m_Impl->RegisterTarget(std::move(name), std::move(target));
	}
}
