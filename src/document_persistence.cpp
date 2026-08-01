#include "document_persistence.h"

#include "canvas_palette.h"
#include "document_engine.h"
#include "document_writer.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace FairyWriter {

// Every persistence operation enters this one FIFO. Callers wait for the typed
// result, which keeps document replacement and close transitions simple, while
// all filesystem reads, hashes, encodes, fsyncs and renames stay off the UI
// thread. Tasks receive immutable snapshots; the live QTextDocument never
// crosses threads.
class OrderedPersistenceWorker final {
public:
	OrderedPersistenceWorker()
		: m_thread([this] { work(); })
	{
	}

	~OrderedPersistenceWorker()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_ready.notify_one();
		if (m_thread.joinable()) m_thread.join();
	}

	template<typename Function>
	auto run(Function&& function)
		-> std::invoke_result_t<std::decay_t<Function>>
	{
		using Result = std::invoke_result_t<std::decay_t<Function>>;
		auto task = std::make_shared<std::packaged_task<Result()>>(
			std::forward<Function>(function));
		std::future<Result> result = task->get_future();
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_tasks.emplace_back([task] { (*task)(); });
		}
		m_ready.notify_one();
		return result.get();
	}

private:
	void work()
	{
		for (;;) {
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_ready.wait(lock, [this] {
					return m_stopping || !m_tasks.empty();
				});
				if (m_stopping && m_tasks.empty()) return;
				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			}
			task();
		}
	}

	std::mutex m_mutex;
	std::condition_variable m_ready;
	std::deque<std::function<void()>> m_tasks;
	bool m_stopping = false;
	std::thread m_thread;
};

namespace {

constexpr char RecoveryMagic[] = "FWRECOV1";
constexpr std::uint32_t RecoveryVersion = 2;

QString normalizedFormatName(QString name)
{
	name = name.trimmed().toLower();
	if (name.startsWith(QLatin1Char('.'))) name.remove(0, 1);
	if (name == QLatin1String("markdown")) return QStringLiteral("md");
	if (name == QLatin1String("text")) return QStringLiteral("txt");
	return name;
}

bool syncHandle(qintptr handle)
{
	int result;
	do {
#ifdef Q_OS_WIN
		result = _commit(static_cast<int>(handle));
#else
		result = fsync(static_cast<int>(handle));
#endif
	} while (result == -1 && errno == EINTR);
	return result == 0;
}

bool syncDirectory(const QString& path)
{
#ifdef Q_OS_WIN
	Q_UNUSED(path);
	return true;
#else
	const QByteArray encoded = QFile::encodeName(path);
	int descriptor;
	do {
		descriptor = ::open(encoded.constData(), O_RDONLY);
	} while (descriptor == -1 && errno == EINTR);
	if (descriptor == -1) return false;
	const bool synced = syncHandle(descriptor);
	int close_result;
	do {
		close_result = ::close(descriptor);
	} while (close_result == -1 && errno == EINTR);
	return synced && close_result == 0;
#endif
}

PersistenceResult durableWrite(const QString& path, const QByteArray& bytes)
{
	PersistenceResult result;
	result.path = path;
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly)) {
		result.error = PersistenceError::WriteFailed;
		result.detail = file.errorString();
		return result;
	}
	if (file.write(bytes) != bytes.size() || !file.flush() || !syncHandle(file.handle())) {
		result.error = PersistenceError::WriteFailed;
		result.detail = file.errorString();
		file.cancelWriting();
		return result;
	}
	if (!file.commit()) {
		result.error = PersistenceError::WriteFailed;
		result.detail = file.errorString();
		return result;
	}
	if (!syncDirectory(QFileInfo(path).absolutePath())) {
		result.error = PersistenceError::WriteFailed;
		result.detail = QStringLiteral("The containing directory could not be synchronized.");
		return result;
	}
	result.fingerprint = FileFingerprint::read(path);
	return result;
}

QByteArray encodeRecord(const RecoveryRecord& record)
{
	QByteArray payload;
	QDataStream stream(&payload, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << quint32(RecoveryVersion)
		<< record.snapshot.document_id
		<< quint8(record.snapshot.format)
		<< record.snapshot.filename
		<< record.snapshot.rich_html
		<< record.snapshot.markdown_source
		<< record.snapshot.content_hash
		<< quint64(record.snapshot.viewport_revision)
		<< quint64(record.snapshot.content_generation)
		<< qint32(record.snapshot.cursor)
		<< qint32(record.snapshot.anchor)
		<< quint64(record.sequence)
		<< qint64(record.created.toMSecsSinceEpoch())
		<< record.manual
		<< record.matches_primary
		<< record.transition_resolved
		<< record.primary_fingerprint.exists
		<< qint64(record.primary_fingerprint.size)
		<< qint64(record.primary_fingerprint.modified_msecs)
		<< record.primary_fingerprint.sha256;
	if (stream.status() != QDataStream::Ok) return {};

	const QByteArray checksum = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
	QByteArray encoded;
	QDataStream outer(&encoded, QIODevice::WriteOnly);
	outer.setByteOrder(QDataStream::LittleEndian);
	outer.writeRawData(RecoveryMagic, 8);
	outer << quint32(payload.size());
	outer.writeRawData(payload.constData(), payload.size());
	outer.writeRawData(checksum.constData(), checksum.size());
	return outer.status() == QDataStream::Ok ? encoded : QByteArray();
}

bool decodeRecord(const QByteArray& encoded, RecoveryRecord& record)
{
	if (encoded.size() < 8 + 4 + 32
		|| QByteArrayView(encoded.constData(), 8) != QByteArrayView(RecoveryMagic, 8)) {
		return false;
	}
	QBuffer buffer;
	buffer.setData(encoded);
	if (!buffer.open(QIODevice::ReadOnly)) return false;
	QDataStream outer(&buffer);
	outer.setByteOrder(QDataStream::LittleEndian);
	char magic[8];
	if (outer.readRawData(magic, 8) != 8) return false;
	quint32 payload_size = 0;
	outer >> payload_size;
	if (payload_size > quint32(encoded.size() - 8 - 4 - 32)) return false;
	QByteArray payload(static_cast<qsizetype>(payload_size), Qt::Uninitialized);
	if (outer.readRawData(payload.data(), payload.size()) != payload.size()) return false;
	QByteArray checksum(32, Qt::Uninitialized);
	if (outer.readRawData(checksum.data(), checksum.size()) != checksum.size()
		|| !buffer.atEnd()
		|| checksum != QCryptographicHash::hash(payload, QCryptographicHash::Sha256)) {
		return false;
	}

	QDataStream stream(payload);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.setVersion(QDataStream::Qt_6_0);
	quint32 version = 0;
	quint8 format = 0;
	quint64 viewport_revision = 0;
	quint64 content_generation = 0;
	qint32 cursor = 0;
	qint32 anchor = 0;
	quint64 sequence = 0;
	qint64 created_msecs = 0;
	qint64 size = 0;
	qint64 modified = 0;
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1 || version > RecoveryVersion) {
		return false;
	}
	stream >> record.snapshot.document_id
		>> format
		>> record.snapshot.filename
		>> record.snapshot.rich_html
		>> record.snapshot.markdown_source
		>> record.snapshot.content_hash
		>> viewport_revision
		>> content_generation
		>> cursor
		>> anchor
		>> sequence
		>> created_msecs
		>> record.manual
		>> record.matches_primary;
	// Version 1 records predate the explicit transition boundary. Treat them as
	// unresolved so no recoverable work is silently hidden during migration.
	if (version >= 2) stream >> record.transition_resolved;
	stream >> record.primary_fingerprint.exists
		>> size
		>> modified
		>> record.primary_fingerprint.sha256;
	if (stream.status() != QDataStream::Ok || !stream.atEnd()
		|| format > quint8(DocumentFormat::PlainText)) {
		return false;
	}
	record.snapshot.format = static_cast<DocumentFormat>(format);
	record.snapshot.viewport_revision = viewport_revision;
	record.snapshot.content_generation = content_generation;
	record.snapshot.cursor = cursor;
	record.snapshot.anchor = anchor;
	record.sequence = sequence;
	record.created = QDateTime::fromMSecsSinceEpoch(created_msecs);
	record.primary_fingerprint.size = size;
	record.primary_fingerprint.modified_msecs = modified;
	return !record.snapshot.document_id.isNull()
		&& record.snapshot.content_hash.size() == 32;
}

QString defaultRecoveryRoot()
{
	QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (root.isEmpty()) root = QDir::temp().filePath(QStringLiteral("FairyWriter"));
	return QDir(root).filePath(QStringLiteral("recovery"));
}

QSettings persistenceSettings()
{
	QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	if (directory.isEmpty()) directory = QDir::temp().filePath(QStringLiteral("FairyWriter"));
	QDir().mkpath(directory);
	return QSettings(QDir(directory).filePath(QStringLiteral("settings.ini")),
		QSettings::IniFormat);
}

struct SnapshotWriteResult final {
	PersistenceResult persistence;
	QByteArray markdown_source;
};

SnapshotWriteResult writeSnapshot(const DocumentSnapshot& snapshot,
	const QString& path, DocumentFormat format,
	DocumentWriter::WriteMode mode,
	const std::optional<FileFingerprint>& expected = std::nullopt)
{
	SnapshotWriteResult output;
	output.persistence.path = path;
	const FileFingerprint before = FileFingerprint::read(path);
	if (mode == DocumentWriter::WriteMode::CreateNew && before.exists) {
		output.persistence.error = PersistenceError::AlreadyExists;
		output.persistence.fingerprint = before;
		return output;
	}
	if (mode == DocumentWriter::WriteMode::ReplaceExisting) {
		if (!before.exists) {
			output.persistence.error = PersistenceError::InvalidPath;
			return output;
		}
		if (!QFileInfo(path).isWritable()) {
			output.persistence.error = PersistenceError::ReadOnly;
			output.persistence.fingerprint = before;
			return output;
		}
		if (expected && !expected->sameContent(before)) {
			output.persistence.error = PersistenceError::ExternalConflict;
			output.persistence.fingerprint = before;
			return output;
		}
	}

	QTextDocument document;
	document.setResourceProvider([](const QUrl&) { return QVariant(); });
	document.setHtml(QString::fromUtf8(snapshot.rich_html));
	DocumentWriter writer;
	writer.setDocument(&document);
	writer.setFileName(path);
	writer.setType(documentFormatName(format));
	if (format == DocumentFormat::Markdown) {
		output.markdown_source = snapshot.format == DocumentFormat::Markdown
			? snapshot.markdown_source
			: document.toMarkdown(QTextDocument::MarkdownDialectGitHub).toUtf8();
		writer.setMarkdownSource(output.markdown_source);
	}
	if (expected) {
		writer.setPreCommitCheck([path, expected = *expected] {
			return expected.sameContent(FileFingerprint::read(path));
		});
	}
	if (!writer.write(mode)) {
		const FileFingerprint after = FileFingerprint::read(path);
		if (expected && !expected->sameContent(after)) {
			output.persistence.error = PersistenceError::ExternalConflict;
		} else if (mode == DocumentWriter::WriteMode::CreateNew && after.exists) {
			output.persistence.error = PersistenceError::AlreadyExists;
		} else {
			output.persistence.error = PersistenceError::WriteFailed;
		}
		output.persistence.fingerprint = after;
		return output;
	}
	output.persistence.fingerprint = FileFingerprint::read(path);
	return output;
}

struct LoadedSnapshot final {
	PersistenceResult persistence;
	DocumentSnapshot snapshot;
	bool read_only = false;
};

LoadedSnapshot loadSnapshot(const QString& path, DocumentFormat format)
{
	LoadedSnapshot output;
	output.persistence.path = path;
	const FileFingerprint before = FileFingerprint::read(path);
	if (!before.exists) {
		output.persistence.error = PersistenceError::LoadFailed;
		return output;
	}
	DocumentEngine parsed;
	if (!parsed.load(path, documentFormatName(format))) {
		output.persistence.error = PersistenceError::LoadFailed;
		return output;
	}
	const FileFingerprint after = FileFingerprint::read(path);
	if (!before.sameContent(after)) {
		output.persistence.error = PersistenceError::ExternalConflict;
		output.persistence.fingerprint = after;
		return output;
	}
	output.snapshot = parsed.snapshot();
	output.persistence.fingerprint = after;
	output.read_only = parsed.isReadOnly();
	return output;
}

} // namespace

QString documentFormatName(DocumentFormat format)
{
	switch (format) {
	case DocumentFormat::Odt: return QStringLiteral("odt");
	case DocumentFormat::Fodt: return QStringLiteral("fodt");
	case DocumentFormat::Docx: return QStringLiteral("docx");
	case DocumentFormat::Rtf: return QStringLiteral("rtf");
	case DocumentFormat::Markdown: return QStringLiteral("md");
	case DocumentFormat::PlainText: return QStringLiteral("txt");
	}
	return {};
}

QString documentFormatExtension(DocumentFormat format)
{
	return documentFormatName(format);
}

bool documentFormatFromName(const QString& name, DocumentFormat& format)
{
	const QString normalized = normalizedFormatName(name);
	if (normalized == QLatin1String("odt")) format = DocumentFormat::Odt;
	else if (normalized == QLatin1String("fodt")) format = DocumentFormat::Fodt;
	else if (normalized == QLatin1String("docx")) format = DocumentFormat::Docx;
	else if (normalized == QLatin1String("rtf")) format = DocumentFormat::Rtf;
	else if (normalized == QLatin1String("md")) format = DocumentFormat::Markdown;
	else if (normalized == QLatin1String("txt")) format = DocumentFormat::PlainText;
	else return false;
	return true;
}

FileFingerprint FileFingerprint::read(const QString& path)
{
	FileFingerprint result;
	QFileInfo info(path);
	result.exists = info.exists() && info.isFile();
	if (!result.exists) return result;
	result.size = info.size();
	result.modified_msecs = info.lastModified().toMSecsSinceEpoch();
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		result.exists = false;
		return result;
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		const QByteArray block = file.read(256 * 1024);
		if (block.isEmpty() && file.error() != QFile::NoError) {
			result.exists = false;
			result.sha256.clear();
			return result;
		}
		hash.addData(block);
	}
	result.sha256 = hash.result();
	return result;
}

bool FileFingerprint::sameContent(const FileFingerprint& other) const noexcept
{
	return exists == other.exists
		&& (!exists || (size == other.size && sha256 == other.sha256));
}

PersistenceSettings PersistenceSettings::load(QSettings& settings)
{
	PersistenceSettings result;
	const QString mode = settings.value(QStringLiteral("Persistence/AutosaveMode"),
		QStringLiteral("save-and-recovery")).toString();
	result.mode = mode == QLatin1String("recovery-only")
		? AutosaveMode::RecoveryOnly : AutosaveMode::SaveAndRecovery;
	result.interval_minutes = static_cast<std::uint8_t>(qBound(1,
		settings.value(QStringLiteral("Persistence/IntervalMinutes"), 1).toInt(), 255));
	result.recovery_copies = static_cast<std::uint8_t>(qBound(0,
		settings.value(QStringLiteral("Persistence/RecoveryCopies"), 5).toInt(), 255));
	// Bounded on load for the same reason the sound fields are: a stored index
	// past the palette is not a strange colour, it is a read off the end of the
	// table. A file written by a later build with more entries reverts to the
	// default rather than picking an arbitrary neighbour.
	result.canvas_color = static_cast<std::uint8_t>(qBound(0,
		settings.value(QStringLiteral("Display/CanvasColor"), 0).toInt(),
		static_cast<int>(CanvasPalette::Count) - 1));
	return result;
}

SoundSettings SoundSettings::load(QSettings& settings)
{
	SoundSettings result;
	result.typing_blips = settings.value(QStringLiteral("Sound/TypingBlips"), true).toBool();
	const QString wave = settings.value(QStringLiteral("Sound/Waveform"),
		QStringLiteral("square")).toString();
	if (wave == QLatin1String("triangle")) result.waveform = Waveform::Triangle;
	else if (wave == QLatin1String("noise")) result.waveform = Waveform::Noise;
	else result.waveform = Waveform::Square;
	// Each bound is the width of the register field the value lands in. A value
	// wider than its field would not be "loud" or "fast" -- it would overflow
	// into the neighbouring field and change a different parameter.
	const auto clamped = [&settings](const char* key, int fallback, int high) {
		return static_cast<std::uint8_t>(qBound(0,
			settings.value(QString::fromLatin1(key), fallback).toInt(), high));
	};
	result.attack = clamped("Sound/Attack", 15, 15);
	result.decay = clamped("Sound/Decay", 5, 7);
	result.sustain_level = clamped("Sound/SustainLevel", 3, 7);
	result.sustain_rate = clamped("Sound/SustainRate", 30, 31);
	result.release = clamped("Sound/Release", 20, 31);
	result.pitch = clamped("Sound/Pitch", 16, 63);
	result.volume = clamped("Sound/Volume", 0x60, 0x7f);
	result.echo_volume = clamped("Sound/EchoVolume", 0, 0x7f);
	// Four bits, and not merely for tidiness: the delay is how much ARAM the
	// echo unit writes to, so a wider value would run the buffer past the
	// region the cartridge reserved for it and into the running driver.
	result.echo_delay = clamped("Sound/EchoDelay", 2, 15);
	result.echo_feedback = clamped("Sound/EchoFeedback", 0, 0x7f);
	return result;
}

void SoundSettings::save(QSettings& settings) const
{
	settings.setValue(QStringLiteral("Sound/TypingBlips"), typing_blips);
	QString wave = QStringLiteral("square");
	if (waveform == Waveform::Triangle) wave = QStringLiteral("triangle");
	else if (waveform == Waveform::Noise) wave = QStringLiteral("noise");
	settings.setValue(QStringLiteral("Sound/Waveform"), wave);
	settings.setValue(QStringLiteral("Sound/Attack"), static_cast<int>(attack));
	settings.setValue(QStringLiteral("Sound/Decay"), static_cast<int>(decay));
	settings.setValue(QStringLiteral("Sound/SustainLevel"), static_cast<int>(sustain_level));
	settings.setValue(QStringLiteral("Sound/SustainRate"), static_cast<int>(sustain_rate));
	settings.setValue(QStringLiteral("Sound/Release"), static_cast<int>(release));
	settings.setValue(QStringLiteral("Sound/Pitch"), static_cast<int>(pitch));
	settings.setValue(QStringLiteral("Sound/Volume"), static_cast<int>(volume));
	settings.setValue(QStringLiteral("Sound/EchoVolume"), static_cast<int>(echo_volume));
	settings.setValue(QStringLiteral("Sound/EchoDelay"), static_cast<int>(echo_delay));
	settings.setValue(QStringLiteral("Sound/EchoFeedback"), static_cast<int>(echo_feedback));
	settings.sync();
}

std::uint8_t SoundSettings::adsr1() const noexcept
{
	// Bit 7 selects the ADSR envelope over the GAIN program.
	return static_cast<std::uint8_t>(0x80 | ((decay & 0x07) << 4) | (attack & 0x0f));
}

std::uint8_t SoundSettings::adsr2() const noexcept
{
	return static_cast<std::uint8_t>(((sustain_level & 0x07) << 5) | (sustain_rate & 0x1f));
}

std::uint8_t SoundSettings::gain() const noexcept
{
	// $a0 is custom mode, exponential decrease -- the release shape a
	// percussive blip wants. Only read once ADSR1 bit 7 is cleared, which is
	// how the cartridge ends a note early.
	return static_cast<std::uint8_t>(0xa0 | (release & 0x1f));
}

void PersistenceSettings::save(QSettings& settings) const
{
	settings.setValue(QStringLiteral("Persistence/AutosaveMode"),
		mode == AutosaveMode::RecoveryOnly
			? QStringLiteral("recovery-only") : QStringLiteral("save-and-recovery"));
	settings.setValue(QStringLiteral("Persistence/IntervalMinutes"),
		static_cast<int>(interval_minutes));
	settings.setValue(QStringLiteral("Persistence/RecoveryCopies"),
		static_cast<int>(recovery_copies));
	settings.setValue(QStringLiteral("Display/CanvasColor"),
		static_cast<int>(canvas_color));
	settings.sync();
}

RecoveryStore::RecoveryStore(QString root)
	: m_root(root.isEmpty() ? defaultRecoveryRoot() : std::move(root))
{
	QDir().mkpath(m_root);
	QSettings session(sessionPath(), QSettings::IniFormat);
	m_previous_session_clean = session.value(QStringLiteral("CleanShutdown"), true).toBool();
}

QString RecoveryStore::sessionPath() const
{
	return QDir(m_root).filePath(QStringLiteral("session.ini"));
}

PersistenceResult RecoveryStore::writeRecord(const RecoveryRecord& record) const
{
	const QByteArray encoded = encodeRecord(record);
	if (encoded.isEmpty()) {
		return {PersistenceError::WriteFailed, record.path,
			QStringLiteral("The recovery snapshot could not be encoded."), {}};
	}
	QDir directory(QFileInfo(record.path).absolutePath());
	if (!directory.mkpath(QStringLiteral("."))) {
		return {PersistenceError::WriteFailed, record.path,
			QStringLiteral("The recovery directory could not be created."), {}};
	}
	return durableWrite(record.path, encoded);
}

PersistenceResult RecoveryStore::load(const QString& path, RecoveryRecord& record) const
{
	PersistenceResult result;
	result.path = path;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		result.error = PersistenceError::CorruptRecovery;
		result.detail = file.errorString();
		return result;
	}
	RecoveryRecord decoded;
	decoded.path = path;
	if (!decodeRecord(file.readAll(), decoded)) {
		result.error = PersistenceError::CorruptRecovery;
		result.detail = QStringLiteral("The recovery checksum or format is invalid.");
		return result;
	}
	record = std::move(decoded);
	result.fingerprint = FileFingerprint::read(path);
	return result;
}

QVector<RecoveryRecord> RecoveryStore::list(bool* had_corrupt) const
{
	if (had_corrupt) *had_corrupt = false;
	QVector<RecoveryRecord> result;
	QDir root(m_root);
	const QFileInfoList documents = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const QFileInfo& document : documents) {
		const QFileInfoList files = QDir(document.absoluteFilePath()).entryInfoList(
			QStringList{QStringLiteral("*.fwrecover")}, QDir::Files, QDir::Name);
		for (const QFileInfo& file : files) {
			RecoveryRecord record;
			if (load(file.absoluteFilePath(), record).succeeded()) result.push_back(record);
			else if (had_corrupt) *had_corrupt = true;
		}
	}
	std::sort(result.begin(), result.end(),
		[](const RecoveryRecord& left, const RecoveryRecord& right) {
			if (left.created != right.created) return left.created > right.created;
			return left.sequence > right.sequence;
		});
	return result;
}

QVector<RecoveryRecord> RecoveryStore::promptCandidates(bool* had_corrupt) const
{
	QVector<RecoveryRecord> candidates;
	for (const RecoveryRecord& record : list(had_corrupt)) {
		// Saved-identical and explicitly resolved generations remain history, not
		// unfinished startup work. Every other valid generation is newer than
		// (or conflicted with) the last resolved transition and remains
		// recoverable even after a clean Checkpoint transition.
		if (!record.matches_primary && !record.transition_resolved) {
			candidates.push_back(record);
		}
	}
	return candidates;
}

void RecoveryStore::rotate(const QUuid& document_id,
	std::uint8_t retained_copies, const QString& keep_path) const
{
	const QString directory_path = QDir(m_root).filePath(
		document_id.toString(QUuid::WithoutBraces));
	QDir directory(directory_path);
	const QFileInfoList files = directory.entryInfoList(
		QStringList{QStringLiteral("*.fwrecover")}, QDir::Files, QDir::Time);
	const int keep_count = retained_copies == 0 ? 1 : retained_copies;
	// The just-committed generation is mandatory even when filesystem timestamp
	// ordering ties put it after older records. Count it up front so rotation
	// can never retain N older records plus the new one.
	int valid_kept = 1;
	for (const QFileInfo& file : files) {
		RecoveryRecord record;
		if (!load(file.absoluteFilePath(), record).succeeded()) continue;
		if (file.absoluteFilePath() == keep_path) continue;
		if (valid_kept < keep_count) {
			++valid_kept;
			continue;
		}
		QFile::remove(file.absoluteFilePath());
	}
}

PersistenceResult RecoveryStore::checkpoint(const DocumentSnapshot& snapshot,
	const FileFingerprint& primary, bool manual, std::uint8_t retained_copies)
{
	if (!manual && retained_copies == 0) {
		return {PersistenceError::Disabled, {}, {}, {}};
	}
	const QVector<RecoveryRecord> records = list();
	std::uint64_t next_sequence = 1;
	for (const RecoveryRecord& existing : records) {
		if (existing.snapshot.document_id != snapshot.document_id) continue;
		next_sequence = std::max(next_sequence, existing.sequence + 1);
		// Adjacent states must differ. Do not globally deduplicate: A -> B -> A
		// is meaningful temporal history and must retain the final A.
		if (existing.snapshot.content_hash == snapshot.content_hash) {
			if (manual) {
				RecoveryRecord pinned = existing;
				pinned.manual = true;
				PersistenceResult result = writeRecord(pinned);
				if (result.succeeded()) {
					rotate(snapshot.document_id, retained_copies, pinned.path);
				}
				return result;
			}
			return {PersistenceError::NoChange, existing.path, {}, {}};
		}
		break;
	}

	RecoveryRecord record;
	record.snapshot = snapshot;
	record.primary_fingerprint = primary;
	record.sequence = next_sequence;
	record.created = QDateTime::currentDateTimeUtc();
	record.manual = manual;
	record.matches_primary = false;
	const QString directory = QDir(m_root).filePath(
		snapshot.document_id.toString(QUuid::WithoutBraces));
	record.path = QDir(directory).filePath(
		QStringLiteral("%1-%2.fwrecover")
			.arg(record.sequence, 20, 10, QLatin1Char('0'))
			.arg(record.created.toMSecsSinceEpoch()));
	PersistenceResult result = writeRecord(record);
	if (!result.succeeded()) return result;
	rotate(snapshot.document_id, retained_copies, record.path);
	return result;
}

PersistenceResult RecoveryStore::markPrimarySaved(
	const DocumentSnapshot& snapshot, const FileFingerprint& primary)
{
	PersistenceResult result;
	for (RecoveryRecord record : list()) {
		if (record.snapshot.document_id != snapshot.document_id) continue;
		record.transition_resolved = true;
		if (record.snapshot.content_hash == snapshot.content_hash
			&& record.snapshot.filename == snapshot.filename
			&& record.snapshot.format == snapshot.format) {
			record.primary_fingerprint = primary;
			record.matches_primary = true;
		}
		result = writeRecord(record);
		if (!result.succeeded()) return result;
	}
	return result;
}

PersistenceResult RecoveryStore::markDiscarded(const QUuid& document_id)
{
	PersistenceResult result;
	for (RecoveryRecord record : list()) {
		if (record.snapshot.document_id != document_id) continue;
		record.transition_resolved = true;
		result = writeRecord(record);
		if (!result.succeeded()) return result;
	}
	return result;
}

void RecoveryStore::markCleanShutdown()
{
	QSettings session(sessionPath(), QSettings::IniFormat);
	session.setValue(QStringLiteral("CleanShutdown"), true);
	session.sync();
}

void RecoveryStore::beginSession()
{
	QSettings session(sessionPath(), QSettings::IniFormat);
	session.setValue(QStringLiteral("CleanShutdown"), false);
	session.sync();
}

DocumentPersistence::DocumentPersistence(DocumentEngine& engine,
	QString recovery_root)
	: m_engine(engine)
	, m_worker(std::make_unique<OrderedPersistenceWorker>())
{
	m_recovery = m_worker->run(
		[root = std::move(recovery_root)]() mutable {
			return std::make_unique<RecoveryStore>(std::move(root));
		});
	m_settings = m_worker->run([] {
		QSettings settings = persistenceSettings();
		return PersistenceSettings::load(settings);
	});
	m_sound = m_worker->run([] {
		QSettings settings = persistenceSettings();
		return SoundSettings::load(settings);
	});
}

DocumentPersistence::~DocumentPersistence() = default;

void DocumentPersistence::setSettings(const PersistenceSettings& settings)
{
	m_settings = settings;
	if (m_settings.interval_minutes == 0) m_settings.interval_minutes = 1;
	const PersistenceSettings committed = m_settings;
	m_worker->run([committed] {
		QSettings persistent = persistenceSettings();
		committed.save(persistent);
	});
}

void DocumentPersistence::setSoundSettings(const SoundSettings& settings)
{
	m_sound = settings;
	const SoundSettings committed = m_sound;
	m_worker->run([committed] {
		QSettings persistent = persistenceSettings();
		committed.save(persistent);
	});
}

QVector<RecoveryRecord> DocumentPersistence::recoveryRecords(
	bool* had_corrupt) const
{
	const auto result = m_worker->run([this] {
		bool corrupt = false;
		QVector<RecoveryRecord> records = m_recovery->list(&corrupt);
		for (RecoveryRecord& record : records) {
			record.current_primary_fingerprint =
				record.snapshot.filename.isEmpty()
					? FileFingerprint()
					: FileFingerprint::read(record.snapshot.filename);
		}
		return std::make_pair(std::move(records), corrupt);
	});
	if (had_corrupt) *had_corrupt = result.second;
	return result.first;
}

QVector<RecoveryRecord> DocumentPersistence::recoveryPromptCandidates(
	bool* had_corrupt) const
{
	const auto result = m_worker->run([this] {
		bool corrupt = false;
		QVector<RecoveryRecord> records =
			m_recovery->promptCandidates(&corrupt);
		for (RecoveryRecord& record : records) {
			record.current_primary_fingerprint =
				record.snapshot.filename.isEmpty()
					? FileFingerprint()
					: FileFingerprint::read(record.snapshot.filename);
		}
		return std::make_pair(std::move(records), corrupt);
	});
	if (had_corrupt) *had_corrupt = result.second;
	return result.first;
}

void DocumentPersistence::beginSession()
{
	m_worker->run([this] { m_recovery->beginSession(); });
}

void DocumentPersistence::markCleanShutdown()
{
	m_worker->run([this] { m_recovery->markCleanShutdown(); });
}

PersistenceResult DocumentPersistence::load(const QString& path)
{
	PersistenceResult result;
	result.path = path;
	DocumentFormat format;
	if (!documentFormatFromName(QFileInfo(path).suffix(), format)) {
		result.error = PersistenceError::UnsupportedFormat;
		return result;
	}
	LoadedSnapshot loaded = m_worker->run(
		[path, format] { return loadSnapshot(path, format); });
	if (!loaded.persistence.succeeded()) return loaded.persistence;
	if (!m_engine.restoreSnapshot(loaded.snapshot, false)) {
		result.error = PersistenceError::LoadFailed;
		return result;
	}
	m_engine.m_loaded_fingerprint = loaded.persistence.fingerprint;
	m_engine.m_loaded_modified = QDateTime::fromMSecsSinceEpoch(
		loaded.persistence.fingerprint.modified_msecs);
	m_engine.m_read_only = loaded.read_only;
	m_automatic_primary_save_disabled = false;
	return loaded.persistence;
}

PersistenceResult DocumentPersistence::save()
{
	PersistenceResult result;
	result.path = m_engine.filename();
	if (result.path.isEmpty()) {
		result.error = PersistenceError::NeedsSaveAs;
		return result;
	}
	if (m_engine.isReadOnly()) {
		result.error = PersistenceError::ReadOnly;
		return result;
	}
	const DocumentSnapshot snapshot = m_engine.snapshot();
	const FileFingerprint expected = m_engine.loadedFingerprint();
	SnapshotWriteResult written = m_worker->run(
		[this, snapshot, expected] {
			SnapshotWriteResult output = writeSnapshot(snapshot,
				snapshot.filename, snapshot.format,
				DocumentWriter::WriteMode::ReplaceExisting, expected);
			if (output.persistence.succeeded()) {
				// The primary commit is already durable at this point. Recovery
				// metadata failure may leave a conservative startup prompt, but
				// must not make the engine forget the fingerprint it just wrote.
				m_recovery->markPrimarySaved(
					snapshot, output.persistence.fingerprint);
			}
			return output;
		});
	if (!written.persistence.succeeded()) return written.persistence;
	if (m_engine.contentGeneration() != snapshot.content_generation
		|| m_engine.contentHash() != snapshot.content_hash) {
		return {PersistenceError::WriteFailed, snapshot.filename,
			QStringLiteral("The document changed while its snapshot was saved."),
			written.persistence.fingerprint};
	}
	m_engine.m_loaded_fingerprint = written.persistence.fingerprint;
	m_engine.m_loaded_modified = QDateTime::fromMSecsSinceEpoch(
		written.persistence.fingerprint.modified_msecs);
	m_engine.m_has_loaded_file = true;
	m_engine.m_read_only = false;
	m_engine.markSaved();
	m_automatic_primary_save_disabled = false;
	return written.persistence;
}

PersistenceResult DocumentPersistence::saveAs(const QString& path, bool overwrite)
{
	PersistenceResult result;
	result.path = path;
	DocumentFormat format;
	if (path.isEmpty()) {
		result.error = PersistenceError::InvalidPath;
		return result;
	}
	if (!documentFormatFromName(QFileInfo(path).suffix(), format)) {
		result.error = PersistenceError::UnsupportedFormat;
		return result;
	}
	const DocumentSnapshot snapshot = m_engine.snapshot();
	SnapshotWriteResult written = m_worker->run(
		[this, snapshot, path, format, overwrite] {
			const FileFingerprint before = FileFingerprint::read(path);
			if (before.exists && !overwrite) {
				SnapshotWriteResult output;
				output.persistence = {PersistenceError::AlreadyExists,
					path, {}, before};
				return output;
			}
			SnapshotWriteResult output = writeSnapshot(snapshot, path, format,
				before.exists
					? DocumentWriter::WriteMode::ReplaceExisting
					: DocumentWriter::WriteMode::CreateNew,
				before.exists
					? std::optional<FileFingerprint>(before)
					: std::nullopt);
			if (output.persistence.succeeded()) {
				DocumentSnapshot saved_snapshot = snapshot;
				saved_snapshot.filename = path;
				saved_snapshot.format = format;
				m_recovery->markPrimarySaved(
					saved_snapshot, output.persistence.fingerprint);
			}
			return output;
		});
	if (!written.persistence.succeeded()) return written.persistence;
	if (m_engine.contentGeneration() != snapshot.content_generation
		|| m_engine.contentHash() != snapshot.content_hash) {
		return {PersistenceError::WriteFailed, path,
			QStringLiteral("The document changed while its snapshot was saved."),
			written.persistence.fingerprint};
	}
	m_engine.m_filename = path;
	m_engine.m_format = documentFormatName(format);
	if (format == DocumentFormat::Markdown) {
		m_engine.m_markdown_source = written.markdown_source;
		m_engine.m_markdown_source_pristine = true;
		m_engine.m_markdown_projection_text =
			m_engine.m_document.toPlainText();
	} else {
		m_engine.m_markdown_source.clear();
		m_engine.m_markdown_source_pristine = false;
		m_engine.m_markdown_projection_text.clear();
		m_engine.m_markdown_source_mode = false;
	}
	m_engine.m_loaded_fingerprint = written.persistence.fingerprint;
	m_engine.m_loaded_modified = QDateTime::fromMSecsSinceEpoch(
		written.persistence.fingerprint.modified_msecs);
	m_engine.m_has_loaded_file = true;
	m_engine.m_read_only = false;
	m_engine.markSaved();
	m_automatic_primary_save_disabled = false;
	return written.persistence;
}

PersistenceResult DocumentPersistence::checkpoint(bool manual)
{
	const DocumentSnapshot snapshot = m_engine.snapshot();
	const FileFingerprint primary = m_engine.loadedFingerprint();
	const std::uint8_t retained = m_settings.recovery_copies;
	return m_worker->run([this, snapshot, primary, manual, retained] {
		return m_recovery->checkpoint(snapshot, primary, manual, retained);
	});
}

PersistenceResult DocumentPersistence::timedAutosave()
{
	if (m_settings.recovery_copies == 0) {
		return {PersistenceError::Disabled, {}, {}, {}};
	}
	const DocumentSnapshot snapshot = m_engine.snapshot();
	const FileFingerprint expected = m_engine.loadedFingerprint();
	const PersistenceSettings settings = m_settings;
	struct AutosaveResult final {
		PersistenceResult result;
		SnapshotWriteResult primary;
		bool primary_saved = false;
	};
	AutosaveResult autosaved = m_worker->run(
		[this, snapshot, expected, settings,
			primary_disabled = m_automatic_primary_save_disabled] {
			AutosaveResult output;
			output.result = m_recovery->checkpoint(snapshot, expected, false,
				settings.recovery_copies);
			if (!output.result.succeeded()
				|| output.result.error == PersistenceError::NoChange
				|| settings.mode
					== PersistenceSettings::AutosaveMode::RecoveryOnly
				|| snapshot.filename.isEmpty()
				|| primary_disabled) {
				return output;
			}
			output.primary = writeSnapshot(snapshot, snapshot.filename,
				snapshot.format, DocumentWriter::WriteMode::ReplaceExisting,
				expected);
			if (!output.primary.persistence.succeeded()) {
				output.result = output.primary.persistence;
				return output;
			}
			output.primary_saved = true;
			const PersistenceResult marked = m_recovery->markPrimarySaved(
				snapshot, output.primary.persistence.fingerprint);
			output.result = marked.succeeded()
				? output.primary.persistence : marked;
			return output;
		});
	if (autosaved.primary_saved) {
		m_engine.m_loaded_fingerprint =
			autosaved.primary.persistence.fingerprint;
		m_engine.m_loaded_modified = QDateTime::fromMSecsSinceEpoch(
			autosaved.primary.persistence.fingerprint.modified_msecs);
		m_engine.m_has_loaded_file = true;
		m_engine.m_read_only = false;
		m_engine.markSaved();
	}
	if (!autosaved.result.succeeded()
		&& (autosaved.result.error == PersistenceError::ExternalConflict
			|| autosaved.result.error == PersistenceError::ReadOnly
			|| autosaved.result.error == PersistenceError::WriteFailed)) {
		m_automatic_primary_save_disabled = true;
	}
	return autosaved.result;
}

PersistenceResult DocumentPersistence::recover(const QString& recovery_path)
{
	const auto loaded = m_worker->run([this, recovery_path] {
		RecoveryRecord record;
		PersistenceResult result = m_recovery->load(recovery_path, record);
		return std::make_pair(std::move(result), std::move(record));
	});
	PersistenceResult result = loaded.first;
	if (!result.succeeded()) return result;
	const RecoveryRecord& record = loaded.second;
	if (!m_engine.restoreSnapshot(record.snapshot, true)) {
		result.error = PersistenceError::CorruptRecovery;
		result.detail = QStringLiteral("The recovery document payload could not be restored.");
		return result;
	}
	m_engine.m_loaded_fingerprint = record.primary_fingerprint;
	m_engine.m_loaded_modified = record.primary_fingerprint.exists
		? QDateTime::fromMSecsSinceEpoch(record.primary_fingerprint.modified_msecs)
		: QDateTime();
	m_engine.m_read_only = false;
	m_automatic_primary_save_disabled = false;
	return result;
}

PersistenceResult DocumentPersistence::discardRecoveryCandidates()
{
	const QUuid document_id = m_engine.documentId();
	return m_worker->run([this, document_id] {
		return m_recovery->markDiscarded(document_id);
	});
}

void DocumentPersistence::noteDocumentTransition()
{
	m_automatic_primary_save_disabled = false;
}

} // namespace FairyWriter
