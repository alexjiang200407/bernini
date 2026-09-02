#include "uniforms/Uniforms.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletPipeline.h"

namespace bgl
{
	namespace
	{
		template <typename Pipeline>
		UniformLayoutEntry
		EntryOf(Pipeline const* pipeline, std::string_view cbufferName)
		{
			gassert(pipeline != nullptr, "Pipeline pointer cannot be null");
			return pipeline->GetUniformLayoutEntry(cbufferName);
		}
	}

	Uniforms::Uniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName) :
		Uniforms(EntryOf(pipeline, cbufferName))
	{}

	Uniforms::Uniforms(IComputePipeline const* pipeline, std::string_view cbufferName) :
		Uniforms(EntryOf(pipeline, cbufferName))
	{}

	Uniforms::Uniforms(UniformLayoutEntry entry) :
		UniformMirror(std::move(entry.layout), entry.size), m_RootParamIndex(entry.rootParamIndex)
	{}
}
