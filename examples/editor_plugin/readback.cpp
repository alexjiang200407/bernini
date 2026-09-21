#include "runtime.h"

#include <assetlib/AssetKindRegistry.h>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <assetlib/pak.h>
#include <exception>
#include <iostream>
#include <memory>

int
main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: sample_readback <archive.bpak> <document-key>\n";
		return 1;
	}
	try
	{
		auto plugin   = sample::CreateAssetPlugin();
		auto registry = std::make_shared<assetlib::AssetKindRegistry>();
		plugin->RegisterKinds(*registry);
		const assetlib::AssetStore store(
			{},
			std::make_shared<assetlib::PakFile>(argv[1]),
			registry);
		const auto  bytes = store.GetFiles().Read(argv[2]);
		const auto* kind  = registry->FindById("sample.document");
		for (const auto& reference : kind->ReadReferences(bytes))
		{
			if (!store.Exists(reference.target))
				return 2;
			std::cout << reference.target << '\n';
		}
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
