#include "Windows/MaterialEditor/nodes/BlendedMaterialOutputNode.h"

BlendedMaterialOutputNode::BlendedMaterialOutputNode() :
	MaterialOutputNode(
		ChannelData::c_MaxChannels)  // base color is RGBA: the alpha drives the blend
{}
