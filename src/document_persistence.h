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

// The typing blip's voice. Every field below is a real S-DSP register field, so
// this struct is a transport for hardware state rather than a model of it; the
// cartridge writes these straight through to the DSP. Ranges are the register
// widths, and load() clamps to them because a value wider than its field would
// silently corrupt the neighbouring one.
//
//	ADSR1 = EDDD AAAA -- enable, 3-bit decay, 4-bit attack
//	ADSR2 = LLLR RRRR -- 3-bit sustain level, 5-bit sustain rate
//
// Higher rate indices are faster; index 0 freezes the envelope entirely.
struct SoundSettings final {
	enum class Waveform : std::uint8_t {
		Square,
		Triangle,
		Noise
	};

	bool typing_blips = true;
	Waveform waveform = Waveform::Square;
	std::uint8_t attack = 15;        // 0-15
	std::uint8_t decay = 5;          // 0-7
	std::uint8_t sustain_level = 3;  // 0-7
	std::uint8_t sustain_rate = 30;  // 0-31
	std::uint8_t release = 20;       // 0-31, a GAIN exponential-decrease rate
	std::uint8_t pitch = 16;         // high byte of the 14-bit 2.12 pitch
	std::uint8_t volume = 0x60;      // 0-127, per side

	// The S-DSP's one built-in effect: a delay line through an 8-tap FIR, which
	// is what makes it a reverb rather than a plain repeat. A volume of 0 is
	// off. Delay is in 2048-byte steps of the ARAM buffer the cartridge
	// reserves for it, so its range is the hardware's four bits and nothing
	// wider. (There is no chorus unit on this chip.)
	std::uint8_t echo_volume = 0;    // 0-127
	std::uint8_t echo_delay = 2;     // 0-15
	std::uint8_t echo_feedback = 0;  // 0-127

	static SoundSettings load(QSettings& settings);
	void save(QSettings& settings) const;

	// The register bytes the cartridge sends to the SPC700 driver.
	std::uint8_t adsr1() const noexcept;
	std::uint8_t adsr2() const noexcept;
	std::uint8_t gain() const noexcept;
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
	const SoundSettings& soundSettings() const noexcept { return m_sound; }
	void setSoundSettings(const SoundSettings& settings);
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
	SoundSettings m_sound;
	bool m_automatic_primary_save_disabled = false;
};

} // namespace FairyWriter

#endif
