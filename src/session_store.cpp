#include "session_store.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QUuid>

namespace FairyWriter {

QString SessionStore::m_path;

void SessionStore::setPath(const QString& path) { m_path = path; QDir().mkpath(m_path); }
QString SessionStore::path() { return m_path; }
QString SessionStore::createId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QVector<SessionRecord> SessionStore::list() const {
	QVector<SessionRecord> result;
	QDir directory(m_path, QStringLiteral("*.session"));
	for (const QString& filename : directory.entryList(QDir::Files, QDir::Name)) {
		QSettings settings(directory.filePath(filename), QSettings::IniFormat);
		SessionRecord record;
		record.id = filename.left(filename.size() - QStringLiteral(".session").size());
		record.name = settings.value(QStringLiteral("Name")).toString();
		record.files = settings.value(QStringLiteral("Save/Current")).toStringList();
		record.active = settings.value(QStringLiteral("Save/Active"), 0).toInt();
		if (!record.id.isEmpty() && !record.name.isEmpty()) result.append(record);
	}
	return result;
}

QString SessionStore::create(const QString& name) {
	if (name.trimmed().isEmpty()) return QString();
	for (const SessionRecord& record : list()) if (record.name == name) return QString();
	const QString id = createId();
	QSettings settings(QDir(m_path).filePath(id + QStringLiteral(".session")), QSettings::IniFormat);
	settings.setValue(QStringLiteral("Name"), name);
	settings.setValue(QStringLiteral("Save/Current"), QStringList());
	settings.setValue(QStringLiteral("Save/Active"), 0);
	settings.sync();
	return settings.status() == QSettings::NoError ? id : QString();
}

SessionRecord SessionStore::load(const QString& id) const {
	SessionRecord record;
	if (id.isEmpty()) return record;
	QSettings settings(QDir(m_path).filePath(id + QStringLiteral(".session")), QSettings::IniFormat);
	if (!QFile::exists(settings.fileName())) return record;
	record.id = id;
	record.name = settings.value(QStringLiteral("Name")).toString();
	record.files = settings.value(QStringLiteral("Save/Current")).toStringList();
	record.active = settings.value(QStringLiteral("Save/Active"), 0).toInt();
	return record;
}

bool SessionStore::save(const QString& id, const State& state) {
	if (id.isEmpty()) return false;
	QSettings settings(QDir(m_path).filePath(id + QStringLiteral(".session")), QSettings::IniFormat);
	if (!QFile::exists(settings.fileName())) return false;
	settings.setValue(QStringLiteral("Save/Current"), state.files);
	settings.setValue(QStringLiteral("Save/Active"), state.active);
	settings.sync();
	return settings.status() == QSettings::NoError;
}

} // namespace FairyWriter
