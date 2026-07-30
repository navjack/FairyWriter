#include "file_catalog.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QCryptographicHash>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#include <algorithm>

namespace FairyWriter {


namespace {
Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
	return Qt::CaseInsensitive;
#else
	return Qt::CaseSensitive;
#endif
}

QString normalizedPath(const QString& path)
{
	return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

bool pathIsWithin(const QString& path, const QString& root)
{
	const QString normalized_path = normalizedPath(path);
	QString normalized_root = normalizedPath(root);
	if (normalized_path.compare(normalized_root, pathCaseSensitivity()) == 0) return true;
	if (!normalized_root.endsWith(QLatin1Char('/'))) normalized_root += QLatin1Char('/');
	return normalized_path.startsWith(normalized_root, pathCaseSensitivity());
}

bool containsPathSeparator(const QString& name)
{
	// Qt paths use '/', while '\' remains a native separator on Windows. Reject
	// both at the filename boundary so a caller can never smuggle traversal in
	// before Qt normalizes the path.
	return name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'));
}

QSettings recentSettings()
{
	QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	if (directory.isEmpty()) directory = QDir::home().filePath(QStringLiteral(".config/FairyWriter"));
	if (!QDir().mkpath(directory) || !QFileInfo(directory).isWritable()) {
		directory = QDir::temp().filePath(QStringLiteral("FairyWriter"));
		QDir().mkpath(directory);
	}
	return QSettings(QDir(directory).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
}
}
	
FileCatalog::FileCatalog(QString root) : m_root(normalizedPath(QDir(root).canonicalPath())) {
	if (!m_root.isEmpty()) m_roots.push_back(m_root);
	for (const QStorageInfo& storage : QStorageInfo::mountedVolumes()) {
		const QString mounted = normalizedPath(QDir(storage.rootPath()).canonicalPath());
		// Never widen a constrained catalog to the filesystem/drive root that
		// already contains it. Other mounted volumes remain explicit browse roots.
		if (mounted.isEmpty()
			|| mounted.compare(normalizedPath(QDir::rootPath()), pathCaseSensitivity()) == 0
			|| pathIsWithin(m_root, mounted)) {
			continue;
		}
		const auto duplicate = std::find_if(m_roots.cbegin(), m_roots.cend(),
			[&mounted](const QString& existing) {
				return existing.compare(mounted, pathCaseSensitivity()) == 0;
			});
		if (duplicate == m_roots.cend()) m_roots.push_back(mounted);
	}
	if (!m_root.isEmpty()) {
		QSettings settings = recentSettings();
		settings.sync();
		const QString key = QStringLiteral("FairyWriter/RecentPaths/") + idFor(m_root);
		const QStringList recent_paths = settings.value(key).toStringList();
		for (const QString& path : recent_paths) {
			const QString id = registerPath(path);
			const FileEntry* file = entry(id);
			if (id.isEmpty() || !file || file->directory || m_recent.contains(id)) continue;
			m_recent.push_back(id);
			if (m_recent.size() == 10) break;
		}
	}
}

bool FileCatalog::withinRoot(const QString& path) const {
	const QFileInfo info(path);
	const QString resolved = normalizedPath(
		info.exists() ? info.canonicalFilePath() : QDir(path).absolutePath());
	for (const QString& root : m_roots) if (pathIsWithin(resolved, root)) return true;
	return false;
}

QString FileCatalog::idFor(const QString& path) const {
	return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}

QString FileCatalog::registerPath(const QString& path) {
	const QFileInfo info(path);
	if (!info.exists() || !withinRoot(info.absoluteFilePath())) return {};
	const QString id = idFor(info.canonicalFilePath());
	for (const FileEntry& entry : m_entries) if (entry.id == id) return id;
	FileEntry entry;
	entry.id = id; entry.name = info.fileName(); entry.absolutePath = info.canonicalFilePath();
	entry.directory = info.isDir(); entry.writable = info.isWritable(); entry.size = info.size(); entry.modified = info.lastModified();
	if (info.canonicalFilePath() != m_root) entry.parentId = idFor(info.dir().canonicalPath());
	m_entries.push_back(entry);
	return id;
}

QString FileCatalog::createDirectory(const QString& parentId, const QString& name) {
	const FileEntry* parent = parentId.isEmpty() ? nullptr : entry(parentId);
	if (!parentId.isEmpty() && !parent) return {};
	const QString parentPath = parent ? parent->absolutePath : m_root;
	if (!QFileInfo(parentPath).isDir() || name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..") || containsPathSeparator(name)) return {};
	const QString path = QDir(parentPath).filePath(name);
	if (!withinRoot(path) || !QDir(parentPath).mkdir(name)) return {};
	return registerPath(path);
}

QString FileCatalog::createFile(const QString& parentId, const QString& name) {
	const QString path = newFilePath(parentId, name);
	if (path.isEmpty()) return {};
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return {};
	file.close();
	return registerPath(path);
}

QString FileCatalog::newFilePath(const QString& parentId, const QString& name) const {
	const FileEntry* parent = parentId.isEmpty() ? nullptr : entry(parentId);
	if (!parentId.isEmpty() && !parent) return {};
	const QString parentPath = parent ? parent->absolutePath : m_root;
	if (!QFileInfo(parentPath).isDir() || name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..") || containsPathSeparator(name)) return {};
	const QString path = QDir(parentPath).filePath(name);
	if (!withinRoot(path) || QFileInfo::exists(path)) return {};
	return path;
}

void FileCatalog::noteRecent(const QString& id) {
	const FileEntry* file = entry(id);
	if (!file || file->directory) return;
	m_recent.removeAll(id);
	m_recent.prepend(id);
	while (m_recent.size() > 10) m_recent.removeLast();

	QStringList recent_paths;
	recent_paths.reserve(m_recent.size());
	for (const QString& recent_id : m_recent) {
		const FileEntry* recent_file = entry(recent_id);
		if (recent_file && !recent_file->directory) recent_paths.push_back(recent_file->absolutePath);
	}

	QSettings settings = recentSettings();
	const QString root_id = idFor(m_root);
	settings.setValue(QStringLiteral("FairyWriter/RecentPaths/") + root_id, recent_paths);
	// Opaque IDs are intentionally process-private. An older build stored them,
	// but a fresh catalog cannot resolve an ID without its canonical path.
	settings.remove(QStringLiteral("FairyWriter/Recent/") + root_id);
	settings.sync();
}

QVector<FileEntry> FileCatalog::recentFiles() const {
	QVector<FileEntry> result;
	for (const QString& id : m_recent) {
		const FileEntry* file = entry(id);
		if (file && !file->directory) result.push_back(*file);
	}
	return result;
}

QVector<FileEntry> FileCatalog::roots() {
	QVector<FileEntry> result;
	for (const QString& root : m_roots) {
		const QString id = registerPath(root);
		if (const FileEntry* file = entry(id)) result.push_back(*file);
	}
	return result;
}

QVector<FileEntry> FileCatalog::list(const QString& parentId, bool show_hidden) {
	const QString parent = parentId.isEmpty() ? m_root : (entry(parentId) ? entry(parentId)->absolutePath : QString());
	if (parent.isEmpty() || !withinRoot(parent) || !QFileInfo(parent).isDir()) return {};
	QFileInfoList infos = QDir(parent).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | (show_hidden ? QDir::Hidden : QDir::Filter(0)), QDir::NoSort);
	QVector<FileEntry> result;
	for (const QFileInfo& info : infos) {
		// A leading dot is the portable document-browser convention for a
		// hidden entry. QDir::Hidden covers the native filesystem attribute,
		// which is a separate property on Windows.
		if (!show_hidden && info.fileName().startsWith(QLatin1Char('.'))) continue;
		const QString id = registerPath(info.absoluteFilePath());
		if (id.isEmpty()) continue;
		result.push_back(*entry(id));
	}
	std::sort(result.begin(), result.end(), [](const FileEntry& a, const FileEntry& b) {
		if (a.directory != b.directory) return a.directory > b.directory;
		return QString::localeAwareCompare(a.name, b.name) < 0;
	});
	return result;
}

const FileEntry* FileCatalog::entry(const QString& id) const {
	for (const FileEntry& entry : m_entries) if (entry.id == id) return &entry;
	return nullptr;
}

} // namespace FairyWriter
