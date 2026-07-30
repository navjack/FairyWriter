#include "file_catalog.h"
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <iostream>
#include <algorithm>

using FairyWriter::FileCatalog;
static int failures = 0;
static void expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }

int main() {
	QTemporaryDir temp;
	QTemporaryDir outside;
	QDir root(temp.path()); root.mkdir(QStringLiteral("Folder"));
	QFile outside_file(QDir(outside.path()).filePath(QStringLiteral("outside.txt")));
	expect(outside_file.open(QIODevice::WriteOnly), "outside fixture file opens");
	outside_file.close();
	for (const QString& name : {QStringLiteral("zeta.txt"), QStringLiteral(".hidden.txt"), QStringLiteral("écriture.txt")}) {
		QFile file(root.filePath(name));
		expect(file.open(QIODevice::WriteOnly), "fixture file opens");
	}
	FileCatalog catalog(temp.path());
	const auto entries = catalog.list();
	expect(entries.size() == 3, "hidden files are excluded by default");
	expect(entries[0].directory && entries[0].name == "Folder", "folders sort first");
	const auto zeta = std::find_if(entries.cbegin(), entries.cend(), [](const auto& e) { return e.name == "zeta.txt"; });
	expect(zeta != entries.cend() && !zeta->id.isEmpty(), "entries expose opaque IDs and names");
	expect(zeta != entries.cend() && zeta->size == 0 && zeta->writable, "entries expose metadata");
	catalog.noteRecent(zeta->id);
	expect(catalog.recentFiles().size() == 1 && catalog.recentFiles().front().id == zeta->id, "recent files retain opaque open history");
	FileCatalog restored_catalog(temp.path());
	expect(restored_catalog.recentFiles().size() == 1 && restored_catalog.recentFiles().front().name == "zeta.txt",
		"recent files are usable immediately after a fresh catalog instance");
	const auto restored_entries = restored_catalog.list();
	const auto restored_zeta = std::find_if(restored_entries.cbegin(), restored_entries.cend(), [](const auto& e) { return e.name == "zeta.txt"; });
	expect(restored_zeta != restored_entries.cend(), "restored catalog still lists the recent file");
	expect(restored_catalog.recentFiles().size() == 1 && restored_catalog.recentFiles().front().name == "zeta.txt", "recent file order persists across catalog instances");
	const auto hidden = catalog.list(QString(), true);
	expect(hidden.size() == 4, "hidden-file toggle exposes hidden files");
	expect(catalog.registerPath(outside_file.fileName()) == QString(), "outside path is rejected");
	const QString folder_id = catalog.createDirectory(QString(), QStringLiteral("新しいフォルダ"));
	expect(!folder_id.isEmpty() && catalog.entry(folder_id)->directory, "Unicode directory creation returns an opaque ID");
	const QString created_file_id = catalog.createFile(folder_id, QStringLiteral("新規.txt"));
	expect(!created_file_id.isEmpty() && catalog.entry(created_file_id) && !catalog.entry(created_file_id)->directory, "safe filename creation returns an opaque file ID");
	expect(catalog.createFile(QStringLiteral("missing-parent"), QStringLiteral("lost.txt")).isEmpty()
			&& !QFileInfo::exists(root.filePath(QStringLiteral("lost.txt"))),
		"unknown parent IDs never fall back to the catalog root for file creation");
	expect(catalog.createDirectory(QStringLiteral("missing-parent"), QStringLiteral("Lost")).isEmpty()
			&& !QFileInfo::exists(root.filePath(QStringLiteral("Lost"))),
		"unknown parent IDs never fall back to the catalog root for directory creation");
	expect(catalog.createFile(folder_id, QStringLiteral("../escape.txt")).isEmpty(), "file creation rejects traversal");
	expect(catalog.createFile(folder_id, QStringLiteral("..\\escape.txt")).isEmpty(), "file creation rejects native Windows traversal");
	expect(catalog.createDirectory(QString(), QStringLiteral("../escape")).isEmpty(), "directory traversal is rejected");
	expect(catalog.createDirectory(QString(), QStringLiteral("..\\escape")).isEmpty(), "directory rejects native Windows traversal");
	const auto roots = catalog.roots();
	expect(!roots.isEmpty() && roots.front().directory, "catalog exposes at least the constrained home root");
	for (const auto& root_entry : roots) {
		expect(QDir::cleanPath(root_entry.absolutePath).compare(
			QDir::cleanPath(QDir::rootPath()),
#ifdef Q_OS_WIN
			Qt::CaseInsensitive
#else
			Qt::CaseSensitive
#endif
			) != 0, "catalog never admits the filesystem root as a mounted volume");
	}
	if (!failures) std::cout << "All FairyWriter file catalog tests passed.\n";
	return failures;
}
