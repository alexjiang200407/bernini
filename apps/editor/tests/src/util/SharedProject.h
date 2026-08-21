#pragma once

#include "VersionControl/GitVersionControl.h"
#include "VersionControl/git_cli.h"

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

#include <QFile>

/**
 * A shared project and two people working on it, which is the only shape most version-control
 * rules can be seen in at all: one clone publishes, the other finds out.
 */
namespace editor::test
{
	namespace fs = std::filesystem;

	inline QString
	ToQString(const fs::path& path)
	{
		return QString::fromStdWString(path.generic_wstring());
	}

	inline QByteArray
	Read(const fs::path& file)
	{
		QFile in(ToQString(file));
		REQUIRE(in.open(QIODevice::ReadOnly));
		return in.readAll();
	}

	inline void
	Write(const fs::path& file, const QByteArray& contents)
	{
		fs::create_directories(file.parent_path());

		QFile out(ToQString(file));
		REQUIRE(out.open(QIODevice::WriteOnly));
		REQUIRE(out.write(contents) == contents.size());
	}

	/**
	 * A real, if empty, mesh.
	 *
	 * ADR-10's guard walks the whole project and refuses to guess at an asset it cannot read, so a
	 * data root any deleting verb touches has to hold real containers rather than stand-ins.
	 */
	inline void
	WriteMesh(const fs::path& file)
	{
		fs::create_directories(file.parent_path());
		assetlib::save(assetlib::BMesh{}, file);
	}

	/** A stub is enough: the scan reads referrers, and a texture is only ever a target. */
	inline void
	WriteTexture(const fs::path& file)
	{
		Write(file, "not really a texture");
	}

	/** A real `.bmaterial` routing its base colour from `routedFrom`, relative to the data root. */
	inline void
	WriteMaterial(const fs::path& file, const std::string& routedFrom)
	{
		fs::create_directories(file.parent_path());

		assetlib::BMaterial material;
		material.pbr.routes[0] = { routedFrom, 0 };
		assetlib::saveMaterial(material, file);
	}

	/**
	 * A shared project and two people working on it, which is the only shape most of these rules can
	 * be seen in at all: one clone publishes, the other finds out.
	 */
	struct SharedProject
	{
		fs::path root;
		fs::path origin;
		fs::path alice;
		fs::path bob;

		// What the seeded mesh holds, so a revert can be checked against it byte for byte.
		QByteArray seeded;

		explicit SharedProject(std::string_view name) :
			root(fs::temp_directory_path() / ("bernini_verbs_" + std::string(name))),
			origin(root / "origin"), alice(root / "alice"), bob(root / "bob")
		{
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root);

			REQUIRE(
				editor::RunGit(root, { "init", "--bare", "-q", "-b", "main", ToQString(origin) })
					.Succeeded());

			CloneInto(alice);
			WriteMesh(alice / "Data/Meshes/coyote.bmesh");
			seeded = Read(alice / "Data/Meshes/coyote.bmesh");
			REQUIRE(editor::RunGit(alice, { "add", "-A" }).Succeeded());
			REQUIRE(editor::RunGit(alice, { "commit", "-q", "-m", "first" }).Succeeded());
			REQUIRE(editor::RunGit(alice, { "push", "-q", "-u", "origin", "main" }).Succeeded());

			CloneInto(bob);
		}

		~SharedProject()
		{
			// A case that made the shared project unwritable left a tree that cannot be removed.
			std::error_code ec;
			for (const auto& entry : fs::recursive_directory_iterator(root, ec))
			{
				fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add, ec);
			}
			fs::remove_all(root, ec);
		}

		SharedProject(const SharedProject&) = delete;
		SharedProject&
		operator=(const SharedProject&) = delete;

		void
		CloneInto(const fs::path& into) const
		{
			REQUIRE(
				editor::RunGit(root, { "clone", "-q", ToQString(origin), ToQString(into) })
					.Succeeded());
			REQUIRE(editor::RunGit(into, { "config", "user.email", "test@bernini" }).Succeeded());
			REQUIRE(editor::RunGit(into, { "config", "user.name", "Bernini Test" }).Succeeded());
		}

		/** Alice changes one asset and publishes it, so Bob's clone is behind. */
		void
		AlicePublishes(const QByteArray& contents) const
		{
			Write(alice / "Data/Meshes/coyote.bmesh", contents);
			editor::GitVersionControl vcs(alice, alice / "Data");
			const auto outcome = vcs.Submit({ "Data/Meshes/coyote.bmesh" }, "alice's change");
			REQUIRE(outcome.status == editor::VersionControlStatus::kDone);
		}
	};

#ifndef _WIN32
	/** Makes every file and directory under `tree` unwritable, so a push into it fails. */
	inline void
	MakeReadOnly(const fs::path& tree)
	{
		for (const auto& entry : fs::recursive_directory_iterator(tree))
		{
			fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::remove);
		}
		fs::permissions(tree, fs::perms::owner_write, fs::perm_options::remove);
	}
#endif

	inline bool
	MergeInProgress(const fs::path& clone)
	{
		return fs::exists(clone / ".git" / "MERGE_HEAD");
	}
}
