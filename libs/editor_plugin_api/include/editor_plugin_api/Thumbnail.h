#pragma once

#include <QImage>
#include <bgl/Camera.h>
#include <core/glm.h>
#include <optional>
#include <string>
#include <variant>

namespace editor
{
	enum class ThumbnailPrimitive
	{
		kSphere,
	};

	struct ThumbnailScene
	{
		std::variant<std::string, ThumbnailPrimitive> geometry;
		std::optional<std::string>                    material;
		std::optional<bgl::Camera>                    camera;
	};

	/** monostate means unavailable; scene keys address the project store, and no camera means auto-frame. */
	using Thumbnail = std::variant<std::monostate, QImage, ThumbnailScene>;
}
