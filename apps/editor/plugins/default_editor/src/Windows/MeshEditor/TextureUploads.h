#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <bgl/types/TextureAssetHandle.h>
#include <memory>
#include <mutex>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace editor
{
	class IEditorHost;
}

/**
 * The Mesh Editor's texture uploads: each file a Texture node names is decoded once, on a worker,
 * uploaded through the host's AssetManager, shared by every node naming it, and released when the
 * last of them lets go.
 *
 * A node never waits for one: it asks, and the upload arrives later through Settled. That is what
 * keeps opening a mesh -- one graph per submesh, dozens of maps -- off the GUI thread. Every method
 * is the GUI thread's.
 */
class TextureUploads : public QObject
{
	Q_OBJECT

public:
	explicit TextureUploads(editor::IEditorHost& host, QObject* parent = nullptr);

	/** Abandons the decodes not yet started, waits for the rest, and releases every upload held. */
	~TextureUploads() override;

	TextureUploads(const TextureUploads&) = delete;

	TextureUploads&
	operator=(const TextureUploads&) = delete;

	/** Takes a reference to `path`'s upload, starting its decode when nothing holds it yet. */
	void
	Acquire(const QString& path);

	/**
	 * Drops a reference Acquire took. The last one retires the upload rather than releasing it: a
	 * material compiled from it may still be drawn until the panel recompiles, so it is freed by
	 * ReleaseRetired. Taken again before then, it is the same upload, unless the file has changed.
	 */
	void
	Release(const QString& path);

	/** Releases every upload retired since, off this thread. After the compiles that dropped them. */
	void
	ReleaseRetired();

	/** `path`'s upload, invalid while it decodes, after it failed, and when nothing holds it. */
	[[nodiscard]] bgl::TextureAssetHandle
	Handle(const QString& path) const;

	/** Whether `path` is held and its upload has neither arrived nor failed. */
	[[nodiscard]] bool
	IsLoading(const QString& path) const;

	/** Whether no decode, upload or release is in flight. */
	[[nodiscard]] bool
	IsIdle() const noexcept;

Q_SIGNALS:
	/** `path`'s upload arrived, or failed to: Handle says which. */
	void
	Settled(const QString& path);

	/** An upload was retired, and waits for ReleaseRetired. */
	void
	Retired();

private:
	struct Load;

	struct Entry
	{
		int                     refs = 0;
		std::shared_ptr<Load>   load;  // set while a worker has it
		bgl::TextureAssetHandle handle;
		qint64                  stamp   = 0;  // the file's, when its decode started
		bool                    failed  = false;
		bool                    retired = false;
	};

	void
	Start(const QString& path, Entry& entry);

	void
	Finish(const QString& path, const std::shared_ptr<Load>& load);

	void
	ReleaseUpload(bgl::TextureAssetHandle handle);

	editor::IEditorHost&  m_Host;
	QHash<QString, Entry> m_Entries;
	std::atomic<int>      m_Running = 0;

	// One upload on the render thread at a time, so a compile the GUI thread waits on queues behind
	// at most one of them rather than one per decode thread.
	std::mutex m_UploadMutex;

	// Two pools, because a decode nobody waits for is dropped on the way out and a release is not.
	QThreadPool m_Decodes;
	QThreadPool m_Releases;
};
