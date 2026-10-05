#pragma once

#include <QWidget>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/BackdropGradient.h>
#include <bgl/types/BloomSettings.h>
#include <bgl/types/Camera.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/FilmGrainSettings.h>
#include <bgl/types/ToonGradeSettings.h>
#include <cstdint>
#include <functional>
#include <gamelib/AssetManager.h>
#include <utility>

namespace editor
{
	struct RenderContext
	{
		bgl::IGraphics&     graphics;
		bgl::IScene&        scene;
		game::AssetManager& assets;
	};

	using RenderWork         = std::function<void(RenderContext&)>;
	using ViewportRenderWork = std::function<void(RenderContext&, const bgl::SceneViewRef&)>;

	// One effect of a viewport's post-process: its settings whether or not it is on, so the host's
	// Render menu can switch it without losing them.
	template <typename Settings>
	struct EffectConfig
	{
		bool     enabled = false;
		Settings settings;
	};

	// bgl's defaults are the identity, which leaves the Render menu's grade toggle nothing to show. A
	// viewport that names no grade takes this mild one instead: warmer, a touch more saturated and
	// contrasty, darker corners.
	[[nodiscard]] inline bgl::ColorGradeSettings
	DefaultViewportGrade() noexcept
	{
		auto settings               = bgl::ColorGradeSettings();
		settings.temperature        = 10.0f;
		settings.saturation         = 1.15f;
		settings.contrast           = 1.1f;
		settings.vignette.intensity = 0.25f;
		return settings;
	}

	// The same mild grade for a toon viewport, in the toon grade's terms.
	[[nodiscard]] inline bgl::ToonGradeSettings
	DefaultViewportToonGrade() noexcept
	{
		auto settings               = bgl::ToonGradeSettings();
		settings.temperature        = 10.0f;
		settings.saturation         = 1.15f;
		settings.contrast           = 1.1f;
		settings.vignette.intensity = 0.25f;
		return settings;
	}

	// What a viewport ends in under the filmic post-process; the editor reads it from a viewport's
	// `filmic` section of config.json.
	struct FilmicConfig
	{
		EffectConfig<bgl::BloomSettings>      bloom;
		EffectConfig<bgl::ColorGradeSettings> grade{ false, DefaultViewportGrade() };
		EffectConfig<bgl::FilmGrainSettings>  grain;
		EffectConfig<bgl::ColorSplitSettings> split;
	};

	// What a viewport ends in under the toon post-process; config.json's `toon` section.
	struct ToonConfig
	{
		EffectConfig<bgl::BloomSettings>      bloom;
		EffectConfig<bgl::ToonGradeSettings>  grade{ false, DefaultViewportToonGrade() };
		EffectConfig<bgl::FilmGrainSettings>  grain;
		EffectConfig<bgl::ColorSplitSettings> split;
	};

	struct ViewportDesc
	{
		uint32_t initialInstances       = 16;
		bool     taaEnabled             = true;
		float    renderScale            = 1.0f;
		float    taaReconstructionWidth = 0.4f;
		float    taaSharpness           = 1.0f;

		// Which post-process type the viewport ends in is the host's (the project's, or toon for
		// toon content, or the user's pick); these are what each type has. Every effect is off by
		// default; the host clamps and warns like the render scale.
		FilmicConfig filmic;
		ToonConfig   toon;

		// Drawn in place of the sky while the viewport shows toon content (SetShowsToonContent).
		bgl::BackdropGradient toonBackdrop;

		template <typename Self>
		Self&&
		SetInitialInstances(this Self&& self, uint32_t value) noexcept
		{
			self.initialInstances = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTaaEnabled(this Self&& self, bool value) noexcept
		{
			self.taaEnabled = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRenderScale(this Self&& self, float value) noexcept
		{
			self.renderScale = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTaaReconstructionWidth(this Self&& self, float value) noexcept
		{
			self.taaReconstructionWidth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTaaSharpness(this Self&& self, float value) noexcept
		{
			self.taaSharpness = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetFilmic(this Self&& self, FilmicConfig value) noexcept
		{
			self.filmic = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetToon(this Self&& self, ToonConfig value) noexcept
		{
			self.toon = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetToonBackdrop(this Self&& self, bgl::BackdropGradient value) noexcept
		{
			self.toonBackdrop = value;
			return std::forward<Self>(self);
		}
	};

	/** GUI-thread widget; destruction drains its render work before releasing its view. */
	class IEditorViewport : public QWidget
	{
	public:
		using QWidget::QWidget;

		/**
		 * Synchronous render work; never retain context or wait on the GUI thread. Worker callers
		 * must join before viewport teardown.
		 */
		virtual void
		Invoke(const ViewportRenderWork& work) = 0;

		virtual void
		SetCamera(const bgl::Camera& camera) = 0;

		virtual void
		SetTime(float seconds) = 0;

		virtual void
		SetRenderingEnabled(bool enabled) = 0;

		/**
		 * The rows the geometry passes render at now: the window's physical height after the
		 * render scale, which the host may change at any time. What a level of detail's pixel
		 * thresholds are measured against. 0 before the viewport has a target.
		 */
		[[nodiscard]] virtual uint32_t
		GetRenderHeight() const noexcept = 0;

		/**
		 * Whether what the viewport shows is toon-shaded -- a mesh with a toon material, say. A toon
		 * look is authored to be seen without a filmic curve, so such a viewport ends in the toon
		 * post-process (bgl::ToonPostProcess, from ViewportDesc::toon) rather than the project's, until
		 * the user picks one for the viewports; then the user's pick holds. Whatever the pick, it draws
		 * ViewportDesc::toonBackdrop instead of its sky: the background follows what is shown.
		 */
		virtual void
		SetShowsToonContent(bool toon) = 0;
	};
}
