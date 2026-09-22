#pragma once

#include <QWidget>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
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

		// Off by default with bgl's identity settings; the host clamps and warns like the render scale.
		bool                    bloomEnabled = false;
		bgl::BloomSettings      bloom;
		bool                    colorGradeEnabled = false;
		bgl::ColorGradeSettings colorGrade;
		ViewportDesc&
		SetInitialInstances(uint32_t value) & noexcept
		{
			initialInstances = value;
			return *this;
		}

		ViewportDesc&&
		SetInitialInstances(uint32_t value) && noexcept
		{
			SetInitialInstances(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetTaaEnabled(bool value) & noexcept
		{
			taaEnabled = value;
			return *this;
		}

		ViewportDesc&&
		SetTaaEnabled(bool value) && noexcept
		{
			SetTaaEnabled(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetRenderScale(float value) & noexcept
		{
			renderScale = value;
			return *this;
		}

		ViewportDesc&&
		SetRenderScale(float value) && noexcept
		{
			SetRenderScale(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetTaaReconstructionWidth(float value) & noexcept
		{
			taaReconstructionWidth = value;
			return *this;
		}

		ViewportDesc&&
		SetTaaReconstructionWidth(float value) && noexcept
		{
			SetTaaReconstructionWidth(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetBloomEnabled(bool value) & noexcept
		{
			bloomEnabled = value;
			return *this;
		}

		ViewportDesc&&
		SetBloomEnabled(bool value) && noexcept
		{
			SetBloomEnabled(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetBloom(bgl::BloomSettings value) & noexcept
		{
			bloom = value;
			return *this;
		}

		ViewportDesc&&
		SetBloom(bgl::BloomSettings value) && noexcept
		{
			SetBloom(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetColorGradeEnabled(bool value) & noexcept
		{
			colorGradeEnabled = value;
			return *this;
		}

		ViewportDesc&&
		SetColorGradeEnabled(bool value) && noexcept
		{
			SetColorGradeEnabled(value);
			return std::move(*this);
		}

		ViewportDesc&
		SetColorGrade(bgl::ColorGradeSettings value) & noexcept
		{
			colorGrade = value;
			return *this;
		}

		ViewportDesc&&
		SetColorGrade(bgl::ColorGradeSettings value) && noexcept
		{
			SetColorGrade(value);
			return std::move(*this);
		}
	};

	/** GUI-thread widget; destruction drains its render work before releasing its view. */
	class IEditorViewport : public QWidget
	{
	public:
		using QWidget::QWidget;

		/** Synchronous render work; never retain context or wait on the GUI thread. */
		// Worker callers must join before viewport teardown.
		virtual void
		Invoke(const ViewportRenderWork& work) = 0;

		virtual void
		SetCamera(const bgl::Camera& camera) = 0;

		virtual void
		SetTime(float seconds) = 0;

		virtual void
		SetRenderingEnabled(bool enabled) = 0;
	};
}
