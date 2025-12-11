#include "drawable/IDrawable.h"

namespace gfx
{
	IDrawable::IDrawable(ShaderMatrix pos) : m_modelTransform{ pos } {}
}
