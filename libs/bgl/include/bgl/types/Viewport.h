#pragma once

namespace bgl
{
	struct Viewport
	{
		float minX, maxX;
		float minY, maxY;
		float minZ, maxZ;

		Viewport() : minX(0.f), maxX(0.f), minY(0.f), maxY(0.f), minZ(0.f), maxZ(1.f) {}

		Viewport(float width, float height) :
			minX(0.f), maxX(width), minY(0.f), maxY(height), minZ(0.f), maxZ(1.f)
		{}

		Viewport(float xMin, float xMax, float yMin, float yMax, float zMin, float zMax) :
			minX(xMin), maxX(xMax), minY(yMin), maxY(yMax), minZ(zMin), maxZ(zMax)
		{}
	};
}
