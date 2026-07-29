#ifndef FAIRYWRITER_FILE_CATALOG_H
#define FAIRYWRITER_FILE_CATALOG_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace FairyWriter {

struct FileEntry {
	QString id;
	QString name;
	QString parentId;
	QString absolutePath;
	bool directory = false;
	bool writable = false;
	qint64 size = 0;
	QDateTime modified;
};

// The cartridge receives opaque IDs and metadata, never host paths. IDs are
// valid only for this catalog snapshot and cannot be used to escape a root.
class FileCatalog final {
public:
	explicit FileCatalog(QString root);
	QVector<FileEntry> list(const QString& parentId = QString(), bool show_hidden = false);
	QVector<FileEntry> roots();
	QString registerPath(const QString& path);
	QString createDirectory(const QString& parentId, const QString& name);
	// Resolves a safe, non-existing target without creating a placeholder. The
	// persistence layer commits the complete file first, then registers it.
	QString newFilePath(const QString& parentId, const QString& name) const;
	QString createFile(const QString& parentId, const QString& name);
	void noteOpened(const QString& id);
	QVector<FileEntry> recentFiles() const;
	const FileEntry* entry(const QString& id) const;

private:
	QString m_root;
	QStringList m_roots;
	QVector<FileEntry> m_entries;
	QStringList m_recent;
	QString idFor(const QString& path) const;
	bool withinRoot(const QString& path) const;
};

} // namespace FairyWriter

#endif
