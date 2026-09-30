#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <bgpu/uniforms/Uniforms.h>
#include <bgpu/uniforms/UniformsBase.h>
#include <core/err/util.h>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		template <typename Pipeline>
		UniformLayoutEntry
		EntryOf(Pipeline const* pipeline, std::string_view cbufferName)
		{
			core::ensure(pipeline != nullptr, "Pipeline pointer cannot be null");
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
		UniformsBase(std::move(entry.layout), entry.size), m_RootParamIndex(entry.rootParamIndex)
	{}
}
