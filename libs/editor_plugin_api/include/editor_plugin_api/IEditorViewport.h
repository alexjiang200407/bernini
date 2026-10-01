#pragma once

#include <QWidget>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/Camera.h>
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

	struct ViewportDesc
	{
		uint32_t initialInstances       = 16;
		bool     taaEnabled             = true;
		float    renderScale            = 1.0f;
		float    taaReconstructionWidth = 0.4f;
		float    taaSharpness           = 1.0f;

		// Off by default with bgl's identity settings; the host clamps and warns like the render scale.
		bool                    bloomEnabled = false;
		bgl::BloomSettings      bloom;
		bool                    colorGradeEnabled = false;
		bgl::ColorGradeSettings colorGrade;
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
		SetBloomEnabled(this Self&& self, bool value) noexcept
		{
			self.bloomEnabled = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBloom(this Self&& self, bgl::BloomSettings value) noexcept
		{
			self.bloom = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetColorGradeEnabled(this Self&& self, bool value) noexcept
		{
			self.colorGradeEnabled = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetColorGrade(this Self&& self, bgl::ColorGradeSettings value) noexcept
		{
			self.colorGrade = value;
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
	};
}
