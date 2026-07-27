#include "device/Device_wgpu.h"
#include "idl/CullStats.h"
#include "idl/CullView.h"
#include "idl/DebugRecord.h"
#include "idl/DispatchArgs.h"
#include "idl/InstanceVisibility.h"
#include "idl/LoosePbrMaterial.h"
#include "idl/Mesh.h"
#include "idl/Meshlet.h"
#include "idl/MeshletInstance.h"
#include "idl/PbrMaterial.h"
#include "idl/Submesh.h"
#include "idl/VertexLayout.h"
#include "slang/SlangErrorChecker.h"
#include "types/SubmeshInstance.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

namespace
{
	const char* const c_SearchPaths[] = { "./shaders/src", "./shaders/tests" };

	// Reflects StrideProbe for one target and returns each buffer's element stride.
	core::str::unordered_str_map<size_t>
	StridesFor(slang::IGlobalSession* global, SlangCompileTarget target, bool wgsl)
	{
		auto targetDesc   = slang::TargetDesc{};
		targetDesc.format = target;

		auto macros = std::vector<slang::PreprocessorMacroDesc>();
		if (wgsl)
			macros.push_back({ "BGL_WGSL", "1" });

		auto sessionDesc                   = slang::SessionDesc{};
		sessionDesc.targetCount            = 1;
		sessionDesc.targets                = &targetDesc;
		sessionDesc.searchPaths            = c_SearchPaths;
		sessionDesc.searchPathCount        = 2;
		sessionDesc.preprocessorMacros     = macros.data();
		sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());

		auto session = Slang::ComPtr<slang::ISession>();
		global->createSession(sessionDesc, session.writeRef());
		REQUIRE(session != nullptr);

		auto            diagnostics = Slang::ComPtr<slang::IBlob>();
		slang::IModule* module      = session->loadModule("StrideProbe", diagnostics.writeRef());
		REQUIRE(module != nullptr);

		auto entry = Slang::ComPtr<slang::IEntryPoint>();
		module->findEntryPointByName("main", entry.writeRef());

		slang::IComponentType* parts[] = { module, entry.get() };
		auto                   program = Slang::ComPtr<slang::IComponentType>();
		session->createCompositeComponentType(parts, 2, program.writeRef(), nullptr);

		auto linked = Slang::ComPtr<slang::IComponentType>();
		program->link(linked.writeRef(), nullptr);

		slang::ProgramLayout* layout = linked->getLayout();

		auto strides = core::str::unordered_str_map<size_t>();
		for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
		{
			slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
			slang::TypeLayoutReflection* element   = param->getTypeLayout()->getElementTypeLayout();
			if (element != nullptr)
				strides[param->getName()] = element->getStride();
		}

		return strides;
	}
}

// Every IDL struct a shader loads must have the same stride on the CPU and on the GPU, or element N
// of the buffer is read from the wrong offset -- silently, since nothing round-trips a second
// element. WGSL rounds a struct's size up to its alignment and the C++ mirror does not, so the two
// can disagree; DXIL is checked alongside because a fix that satisfies only one target breaks the
// other, and the pad then has to live in the shared Slang source rather than in either mirror.
TEST_CASE("Every IDL struct has the same stride on both sides", "[wgpu][idl]")
{
	auto global = Slang::ComPtr<slang::IGlobalSession>();
	slang::createGlobalSession(global.writeRef());
	REQUIRE(global != nullptr);

	const auto wgsl = StridesFor(global, SLANG_WGSL, /*wgsl*/ true);
	const auto dxil = StridesFor(global, SLANG_DXIL, /*wgsl*/ false);

	const std::pair<const char*, size_t> cpuSizes[] = {
		{ "gMesh", sizeof(idl::Mesh) },
		{ "gMeshlet", sizeof(idl::Meshlet) },
		{ "gSubmesh", sizeof(idl::Submesh) },
		{ "gSubmeshInstance", sizeof(SubmeshInstance) },
		{ "gMeshletInstance", sizeof(idl::MeshletInstance) },
		{ "gVertexLayout", sizeof(idl::VertexLayout) },
		{ "gCullView", sizeof(idl::CullView) },
		{ "gCullStats", sizeof(idl::CullStats) },
		{ "gInstanceVisibility", sizeof(idl::InstanceVisibility) },
		{ "gDispatchArgs", sizeof(idl::DispatchArgs) },
		{ "gPbrMaterial", sizeof(idl::PbrMaterial) },
		{ "gLoosePbrMaterial", sizeof(idl::LoosePbrMaterial) },
		{ "gDebugRecord", sizeof(idl::DebugRecord) },
	};

	// PbrMaterial and LoosePbrMaterial round up under WGSL but not on the CPU or DXIL. Nothing
	// portable loads them yet -- the material atlas is W4 -- so they are listed rather than fixed
	// here, and W4 must pad them in the IDL source before a WGSL shader reads a material array.
	const auto knownWgslDivergent = [](std::string_view name) {
		return name == "gPbrMaterial" || name == "gLoosePbrMaterial";
	};

	for (const auto& [name, cpu] : cpuSizes)
	{
		if (const auto d = dxil.find(name); d != dxil.end())
		{
			INFO(name << ": cpu " << cpu << " vs dxil " << d->second);
			CHECK(d->second == cpu);
		}

		const auto w = wgsl.find(name);
		if (w == wgsl.end() || knownWgslDivergent(name))
			continue;

		INFO(name << ": cpu " << cpu << " vs wgsl " << w->second);
		CHECK(w->second == cpu);
	}
}
