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

	// What a viewport ends in; the editor reads it from a viewport's `postProcess` section of
	// config.json.
	struct PostProcessConfig
	{
		EffectConfig<bgl::BloomSettings>      bloom;
		EffectConfig<bgl::ColorGradeSettings> grade{ false, DefaultViewportGrade() };
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

		// Every effect is off by default; the host clamps and warns like the render scale.
		PostProcessConfig postProcess;

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
		SetPostProcess(this Self&& self, PostProcessConfig value) noexcept
		{
			self.postProcess = value;
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
		 * Whether what the viewport shows is toon-shaded -- a mesh with a toon material, say. Such a
		 * viewport draws ViewportDesc::toonBackdrop instead of its sky; its post-process is every
		 * viewport's.
		 */
		virtual void
		SetShowsToonContent(bool toon) = 0;
	};
}
