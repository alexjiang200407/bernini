#include "types/PSO.h"

#define MAP_PSO_ENTRY(Layer, Geom, Mat) \
	{ #Layer "_" #Geom "_" #Mat, PSO::k##Layer##_##Geom##_##Mat }

namespace gfx
{
	PSO
	computePSO(LayerType layer, GeometryType geomType, MaterialType matType)
	{
		static const std::unordered_map<std::string, PSO> g_PsoLookupMap = {
			MAP_PSO_ENTRY(Opaque, Static, PBR),
			MAP_PSO_ENTRY(Transparent, Static, PBR),
			MAP_PSO_ENTRY(AlphaTest, Static, PBR),
		};

		auto lookupKey = std::format(
			"{}_{}_{}",
			getLayerTypeName(layer),
			getGeomTypeName(geomType),
			getMatTypeName(matType));

		auto it = g_PsoLookupMap.find(lookupKey);
		if (it != g_PsoLookupMap.end())
		{
			return it->second;
		}

		return PSO::kInvalid;
	}
}
