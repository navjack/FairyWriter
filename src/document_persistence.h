#ifndef FAIRYWRITER_DOCUMENT_PERSISTENCE_H
#define FAIRYWRITER_DOCUMENT_PERSISTENCE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUuid>
#include <QVector>

#include <cstdint>
#include <memory>

class QSettings;

namespace FairyWriter {

class DocumentEngine;
class OrderedPersistenceWorker;

enum class DocumentFormat : std::uint8_t {
	Odt,
	Fodt,
	Docx,
	Rtf,
	Markdown,
	PlainText
};

QString documentFormatName(DocumentFormat format);
QString documentFormatExtension(DocumentFormat format);
bool documentFormatFromName(const QString& name, DocumentFormat& format);

struct FileFingerprint final {
	bool exists = false;
	qint64 size = 0;
	qint64 modified_msecs = 0;
	QByteArray sha256;

	static FileFingerprint read(const QString& path);
	bool sameContent(const FileFingerprint& other) const noexcept;
};

struct DocumentSnapshot final {
	QUuid document_id;
	DocumentFormat format = DocumentFormat::Odt;
	QString filename;
	QByteArray rich_html;
	QByteArray markdown_source;
	QByteArray content_hash;
	std::uint64_t viewport_revision = 0;
	std::uint64_t content_generation = 0;
	int cursor = 0;
	int anchor = 0;
};

struct PersistenceSettings final {
	enum class AutosaveMode : std::uint8_t {
		RecoveryOnly,
		SaveAndRecovery
	};

	AutosaveMode mode = AutosaveMode::SaveAndRecovery;
	std::uint8_t interval_minutes = 1;
	std::uint8_t recovery_copies = 5;

	static PersistenceSettings load(QSettings& settings);
	void save(QSettings& settings) const;
};

enum class PersistenceError : std::uint8_t {
	None,
	NoChange,
	Disabled,
	NeedsSaveAs,
	AlreadyExists,
	ExternalConflict,
	ReadOnly,
	InvalidPath,
	UnsupportedFormat,
	LoadFailed,
	WriteFailed,
	CorruptRecovery
};

struct PersistenceResult final {
	PersistenceError error = PersistenceError::None;
	QString path;
	QString detail;
	FileFingerprint fingerprint;

	bool succeeded() const noexcept {
		return error == PersistenceError::None || error == PersistenceError::NoChange;
	}
};

struct RecoveryRecord final {
	QString path;
	DocumentSnapshot snapshot;
	FileFingerprint primary_fingerprint;
	// Runtime-only status input populated by the ordered worker. It is not part
	// of the checksummed recovery payload.
	FileFingerprint current_primary_fingerprint;
	std::uint64_t sequence = 0;
	QDateTime created;
	bool manual = false;
	bool matches_primary = false;
	// A later successful primary save or an explicit Discard resolved this
	// generation. It remains available in Recovery History, but it must not be
	// offered as unfinished work at startup.
	bool transition_resolved = false;
};

class RecoveryStore final {
public:
	explicit RecoveryStore(QString root);

	const QString& root() const noexcept { return m_root; }
	bool previousSessionWasClean() const noexcept { return m_previous_session_clean; }
	PersistenceResult checkpoint(const DocumentSnapshot& snapshot,
		const FileFingerprint& primary, bool manual, std::uint8_t retained_copies);
	PersistenceResult markPrimarySaved(const DocumentSnapshot& snapshot,
		const FileFingerprint& primary);
	PersistenceResult markDiscarded(const QUuid& document_id);
	QVector<RecoveryRecord> list(bool* had_corrupt = nullptr) const;
	QVector<RecoveryRecord> promptCandidates(bool* had_corrupt = nullptr) const;
	PersistenceResult load(const QString& path, RecoveryRecord& record) const;
	void beginSession();
	void markCleanShutdown();

private:
	QString m_root;
	bool m_previous_session_clean = true;

	QString sessionPath() const;
	PersistenceResult writeRecord(const RecoveryRecord& record) const;
	void rotate(const QUuid& document_id, std::uint8_t retained_copies,
		const QString& keep_path) const;
};

class DocumentPersistence final {
public:
	explicit DocumentPersistence(DocumentEngine& engine,
		QString recovery_root = QString());
	~DocumentPersistence();

	DocumentEngine& engine() noexcept { return m_engine; }
	const DocumentEngine& engine() const noexcept { return m_engine; }
	const PersistenceSettings& settings() const noexcept { return m_settings; }
	void setSettings(const PersistenceSettings& settings);
	QVector<RecoveryRecord> recoveryRecords(bool* had_corrupt = nullptr) const;
	QVector<RecoveryRecord> recoveryPromptCandidates(
		bool* had_corrupt = nullptr) const;
	void beginSession();
	void markCleanShutdown();

	PersistenceResult load(const QString& path);
	PersistenceResult save();
	PersistenceResult saveAs(const QString& path, bool overwrite);
	PersistenceResult checkpoint(bool manual = false);
	PersistenceResult timedAutosave();
	PersistenceResult recover(const QString& recovery_path);
	PersistenceResult discardRecoveryCandidates();
	void noteDocumentTransition();
	bool automaticPrimarySaveDisabled() const noexcept {
		return m_automatic_primary_save_disabled;
	}

private:
	DocumentEngine& m_engine;
	std::unique_ptr<OrderedPersistenceWorker> m_worker;
	std::unique_ptr<RecoveryStore> m_recovery;
	PersistenceSettings m_settings;
	bool m_automatic_primary_save_disabled = false;
};

} // namespace FairyWriter

#endif
