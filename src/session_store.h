#ifndef FAIRYWRITER_SESSION_STORE_H
#define FAIRYWRITER_SESSION_STORE_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace FairyWriter {

struct SessionRecord {
	QString id;
	QString name;
	QStringList files;
	int active = 0;
};

class SessionStore final {
public:
	struct State { QStringList files; int active = 0; };
	static void setPath(const QString& path);
	static QString path();
	QVector<SessionRecord> list() const;
	QString create(const QString& name);
	SessionRecord load(const QString& id) const;
	bool save(const QString& id, const State& state);
	static QString createId();

private:
	static QString m_path;
};

} // namespace FairyWriter

#endif
