#include "Windows/MeshEditor/TextureUploads.h"

#include <QDebug>
#include <QString>
#include <QThread>

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/image_io.h>
#include <atomic>
#include <bgl/types/TextureAssetHandle.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_sdk/asset_paths.h>
#include <exception>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <memory>
#include <mutex>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <string>
#include <utility>
#include <vector>

struct TextureUploads::Load
{
	std::string       key;
	bool              insideDataRoot = true;
	std::atomic<bool> wanted         = true;

	// Written by the worker before it posts Finish, read by the GUI thread after.
	bgl::TextureAssetHandle handle;
	std::string             error;
	bool                    skipped = false;
};

TextureUploads::TextureUploads(editor::IEditorHost& host, QObject* parent) :
	QObject(parent), m_Host(host)
{
	// The viewport wins: the decodes leave cores for the render and GUI threads.
	m_Decodes.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 2, 1, 4));
	m_Releases.setMaxThreadCount(1);
}

TextureUploads::~TextureUploads()
{
	for (const Entry& entry : m_Entries)
		if (entry.load != nullptr)
			entry.load->wanted = false;

	m_Decodes.clear();
	m_Decodes.waitForDone();
	m_Releases.waitForDone();

	// A Finish still queued for this object is dropped with it, so what it would have released is
	// released here.
	auto held = std::vector<bgl::TextureAssetHandle>();
	for (const Entry& entry : m_Entries)
	{
		if (entry.handle.textureSlot)
			held.push_back(entry.handle);
		if (entry.load != nullptr && entry.load->handle.textureSlot)
			held.push_back(entry.load->handle);
	}
	if (held.empty())
		return;

	try
	{
		m_Host.InvokeRender([&](editor::RenderContext& context) {
			for (const bgl::TextureAssetHandle& handle : held)
				context.assets.ReleaseTexture(handle);
		});
	}
	catch (const std::exception& e)
	{
		qWarning("TextureUploads: could not release the textures still held: %s", e.what());
	}
}

void
TextureUploads::Acquire(const QString& path)
{
	Entry& entry = m_Entries[path];
	if (++entry.refs > 1)
		return;

	if (entry.retired)
	{
		entry.retired = false;
		if (entry.stamp == editor::FileStamp(path))
			return;

		// Rewritten while nothing held it: the upload is of bytes the file no longer has.
		ReleaseUpload(entry.handle);
		entry.handle = {};
	}

	if (entry.load != nullptr)
		entry.load->wanted = true;
	else if (!entry.handle.textureSlot && !entry.failed)
		Start(path, entry);
}

void
TextureUploads::Release(const QString& path)
{
	const auto it = m_Entries.find(path);
	if (it == m_Entries.end() || --it->refs > 0)
		return;

	// Finish decides what becomes of an upload still in flight.
	if (it->load != nullptr)
	{
		it->load->wanted = false;
		return;
	}

	if (!it->handle.textureSlot)
	{
		m_Entries.erase(it);
		return;
	}

	it->retired = true;
	Q_EMIT Retired();
}

void
TextureUploads::ReleaseRetired()
{
	for (auto it = m_Entries.begin(); it != m_Entries.end();)
	{
		if (it->retired && it->refs == 0)
		{
			ReleaseUpload(it->handle);
			it = m_Entries.erase(it);
		}
		else
		{
			++it;
		}
	}
}

bgl::TextureAssetHandle
TextureUploads::Handle(const QString& path) const
{
	const auto it = m_Entries.find(path);
	return it != m_Entries.end() ? it->handle : bgl::TextureAssetHandle();
}

bool
TextureUploads::IsLoading(const QString& path) const
{
	const auto it = m_Entries.find(path);
	return it != m_Entries.end() && it->load != nullptr;
}

bool
TextureUploads::IsIdle() const noexcept
{
	if (m_Running != 0)
		return false;

	for (const Entry& entry : m_Entries)
		if (entry.load != nullptr)
			return false;
	return true;
}

void
TextureUploads::Start(const QString& path, Entry& entry)
{
	auto load = std::make_shared<Load>();

	const auto file = std::filesystem::path(path.toStdWString());
	try
	{
		load->key = m_Host.GetStore().KeyFor(file);
	}
	catch (const std::exception&)
	{
		// A map from outside the project. The prefetch below is what the manager uploads, so it
		// never reads by this key; it only names the upload.
		load->key            = file.generic_string();
		load->insideDataRoot = false;
	}

	entry.load  = load;
	entry.stamp = editor::FileStamp(path);
	++m_Running;

	m_Decodes.start([this, path, load, &host = m_Host] {
		try
		{
			if (load->wanted)
			{
				auto image = load->insideDataRoot ?
				                 host.GetStore().LoadTexture(load->key) :
				                 assetlib::loadKTX2(std::filesystem::path(path.toStdWString()));

				if (load->wanted)
				{
					auto prefetch = game::TexturePrefetch();
					prefetch.emplace(load->key, std::move(image));
					const std::lock_guard<std::mutex> uploading(m_UploadMutex);
					host.InvokeRender([&](editor::RenderContext& context) {
						load->handle = context.assets.AcquireTexture(load->key, &prefetch);
					});
				}
				else
				{
					load->skipped = true;
				}
			}
			else
			{
				load->skipped = true;
			}
		}
		catch (const std::exception& e)
		{
			load->error = e.what();
		}

		--m_Running;
		QMetaObject::invokeMethod(
			this,
			[this, path, load] { Finish(path, load); },
			Qt::QueuedConnection);
	});
}

void
TextureUploads::Finish(const QString& path, const std::shared_ptr<Load>& load)
{
	const auto it = m_Entries.find(path);
	if (it == m_Entries.end() || it->load != load)
	{
		if (load->handle.textureSlot)
			ReleaseUpload(load->handle);
		return;
	}

	it->load.reset();

	if (it->refs == 0)
	{
		if (load->handle.textureSlot)
			ReleaseUpload(load->handle);
		m_Entries.erase(it);
		return;
	}

	// Dropped and taken again while the worker was deciding to skip it.
	if (load->skipped)
	{
		Start(path, *it);
		return;
	}

	if (!load->error.empty())
	{
		qWarning("TextureUploads: failed to load '%s': %s", qPrintable(path), load->error.c_str());
		it->failed = true;
	}
	it->handle = load->handle;

	Q_EMIT Settled(path);
}

void
TextureUploads::ReleaseUpload(bgl::TextureAssetHandle handle)
{
	++m_Running;
	m_Releases.start([this, handle, &host = m_Host] {
		try
		{
			host.InvokeRender(
				[&](editor::RenderContext& context) { context.assets.ReleaseTexture(handle); });
		}
		catch (const std::exception& e)
		{
			qWarning("TextureUploads: could not release a texture: %s", e.what());
		}
		--m_Running;
	});
}
