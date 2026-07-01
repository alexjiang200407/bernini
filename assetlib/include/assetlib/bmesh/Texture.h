#pragma once

namespace assetlib::bmesh
{
	/**
	 * A self-describing DDS image extracted from a source asset. Textures are detached from the mesh
	 * (no node or submesh references them) and are not part of the serialized `.bmesh` container; they
	 * are carried purely as extracted assets and emitted as standalone `.dds` files.
	 */
	struct Texture
	{
		std::string            name;  // source image name, for identification only
		uint32_t               width;
		uint32_t               height;
		uint32_t               mipLevels;
		uint32_t               dxgiFormat;  // DXGI_FORMAT as a raw value; no DXGI type leaks out
		std::vector<std::byte> dds;         // complete .dds file bytes
	};
}
