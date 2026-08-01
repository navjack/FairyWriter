#include "document_bridge.h"
#include "canvas_palette.h"
#include "session_store.h"

#include <algorithm>
#include <QFileInfo>
#include <QStandardPaths>

namespace FairyWriter {

DocumentBridge::DocumentBridge(QString recovery_root)
	: m_persistence(m_engine, std::move(recovery_root))
{
	if (SessionStore::path().isEmpty()) SessionStore::setPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/sessions"));
	publish();
}

static bool sessionEvent(MailboxRing& events, std::uint16_t kind, const SessionRecord& session) {
	const QByteArray id = session.id.toUtf8();
	const QByteArray name = session.name.toUtf8();
	if (id.size() > 255 || name.size() > 0xffff) return false;
	MailboxRecord event; event.kind = kind;
	event.payload.push_back(static_cast<std::uint8_t>(id.size()));
	event.payload.push_back(static_cast<std::uint8_t>(name.size()));
	event.payload.push_back(static_cast<std::uint8_t>(name.size() >> 8));
	event.payload.push_back(static_cast<std::uint8_t>(session.id == QString() ? 1 : 0));
	event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(id.constData()), reinterpret_cast<const std::uint8_t*>(id.constData() + id.size()));
	event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(name.constData()), reinterpret_cast<const std::uint8_t*>(name.constData() + name.size()));
	return events.push(event);
}

bool DocumentBridge::listSessions() {
	for (const SessionRecord& session : SessionStore().list()) if (!sessionEvent(m_events, EventSessionEntry, session)) return false;
	return true;
}

bool DocumentBridge::createSession(const QString& name) {
	const QString id = SessionStore().create(name);
	if (id.isEmpty()) return false;
	return sessionEvent(m_events, EventSessionEntry, SessionStore().load(id));
}

bool DocumentBridge::switchSession(FileCatalog& catalog, const QString& id) {
	SessionStore store;
	if (!m_session_id.isEmpty()) {
		SessionStore::State state;
		if (!m_engine.filename().isEmpty()) state.files.append(m_engine.filename());
		if (!store.save(m_session_id, state)) return false;
	}
	const SessionRecord session = store.load(id);
	if (session.id.isEmpty()) return false;
	if (!session.files.isEmpty()) {
		const QString path = session.files.value(qBound(0, session.active, session.files.size() - 1));
		if (!QFileInfo::exists(path) || !m_persistence.load(path).succeeded()) return false;
		catalog.registerPath(path);
		releaseView();
	}
	m_session_id = id;
	return sessionEvent(m_events, EventSessionChanged, session)
		&& publish()
		&& publishStructureSuggestion();
}

bool DocumentBridge::publish() {
	ViewportSnapshot snapshot;
	// The current cartridge has a 239-cell WRAM working plane and 8-bit input
	// indexes. Publish a complete grapheme-safe UTF-8 window that it can decode
	// in one pass instead of handing it a larger payload and truncating bytes.
	// A live scroll anchor is authoritative; otherwise the last published start is
	// only a stability hint that keeps the window from re-centring every keystroke.
	const bool scrolled = m_scroll_anchor.has_value();
	if (!m_engine.makeViewport(MailboxLayout::CartridgeWorkingBytes, snapshot,
		scrolled ? m_scroll_anchor : m_viewport_start, scrolled)) return false;
	if (!m_viewports.publish(snapshot)) return false;
	m_viewport_start = snapshot.text_offset;
	return true;
}

bool DocumentBridge::publishStructureSuggestion() {
	const std::uint32_t count = m_engine.inferredChapterCount();
	if (count == 0) return true;
	MailboxRecord event;
	event.kind = EventStructureSuggestion;
	event.revision = m_engine.revision();
	event.payload = {
		1,
		static_cast<std::uint8_t>(count),
		static_cast<std::uint8_t>(count >> 8),
		static_cast<std::uint8_t>(count >> 16),
		static_cast<std::uint8_t>(count >> 24)
	};
	return m_events.push(event);
}

bool DocumentBridge::submit(const MailboxRecord& command) { return m_commands.push(command); }

// A replacement document gets a fresh view. Both the live scrollbar anchor and
// the last published window belong to the document that just went away, and
// forcing them onto the new one would open it somewhere other than its caret.
void DocumentBridge::releaseView() {
	m_scroll_anchor.reset();
	m_viewport_start.reset();
}

bool DocumentBridge::publishOpenFailed(const QString& id) {
	MailboxRecord event;
	event.kind = EventOpenFailed;
	event.revision = m_engine.revision();
	const QByteArray bytes = id.toUtf8();
	event.payload.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
		reinterpret_cast<const std::uint8_t*>(bytes.constData() + bytes.size()));
	return m_events.push(event);
}

bool DocumentBridge::openFile(FileCatalog& catalog, const QString& id) {
	// An ID the catalog cannot resolve is still a failed open the user asked
	// for. It has to reach the cartridge as one, because the cartridge has
	// already left the browser for the document: without this event the request
	// would vanish and the unchanged document would look like a dead key.
	const FileEntry* file = catalog.entry(id);
	if (!file || file->directory) {
		publishOpenFailed(id);
		return false;
	}
	if (!m_persistence.load(file->absolutePath).succeeded()) return publishOpenFailed(id);
	catalog.noteRecent(id);
	releaseView();
	return publishPersistenceSettings() && publish() && publishStructureSuggestion();
}

bool DocumentBridge::publishFilePage(const QVector<FileEntry>& files,
	std::size_t offset, std::uint8_t source) {
	const std::size_t begin = std::min(offset, static_cast<std::size_t>(files.size()));
	const std::size_t end = std::min(begin + FilePageSize,
		static_cast<std::size_t>(files.size()));
	for (std::size_t index = begin; index < end; ++index) {
		const FileEntry& file = files[static_cast<qsizetype>(index)];
		const QByteArray id = file.id.toUtf8();
		const QByteArray name = file.name.toUtf8();
		if (id.size() > 255 || name.size() > 0xffff) return false;
		MailboxRecord event;
		event.kind = EventFileEntry;
		event.payload.reserve(20 + id.size() + name.size());
		event.payload.push_back(static_cast<std::uint8_t>(id.size()));
		event.payload.push_back(static_cast<std::uint8_t>(name.size()));
		event.payload.push_back(static_cast<std::uint8_t>(name.size() >> 8));
		event.payload.push_back(static_cast<std::uint8_t>((file.directory ? 1 : 0) | (file.writable ? 2 : 0)));
		const auto append64 = [&event](std::uint64_t value) {
			for (int i = 0; i < 8; ++i) event.payload.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
		};
		append64(static_cast<std::uint64_t>(file.size));
		append64(static_cast<std::uint64_t>(file.modified.toMSecsSinceEpoch()));
		event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(id.constData()), reinterpret_cast<const std::uint8_t*>(id.constData() + id.size()));
		event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(name.constData()), reinterpret_cast<const std::uint8_t*>(name.constData() + name.size()));
		if (!m_events.push(event)) return false;
	}
	MailboxRecord complete;
	complete.kind = EventFileListComplete;
	complete.revision = m_engine.revision();
	complete.payload.resize(11);
	const auto put32 = [&complete](std::size_t at, std::uint32_t value) {
		for (int byte = 0; byte < 4; ++byte) {
			complete.payload[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
		}
	};
	put32(0, static_cast<std::uint32_t>(files.size()));
	put32(4, static_cast<std::uint32_t>(begin));
	complete.payload[8] = static_cast<std::uint8_t>(end - begin);
	complete.payload[9] = source;
	complete.payload[10] = end < static_cast<std::size_t>(files.size()) ? 1 : 0;
	return m_events.push(complete);
}

bool DocumentBridge::listFiles(FileCatalog& catalog, const QString& parentId,
	bool showHidden, std::size_t offset, std::optional<DocumentFormat> format) {
	QVector<FileEntry> files = catalog.list(parentId, showHidden);
	if (format.has_value()) {
		const QString extension = documentFormatExtension(*format);
		files.erase(std::remove_if(files.begin(), files.end(),
			[&extension](const FileEntry& file) {
				return !file.directory
					&& QFileInfo(file.name).suffix().compare(
						extension, Qt::CaseInsensitive) != 0;
			}), files.end());
	}
	return publishFilePage(files, offset, 1);
}

bool DocumentBridge::listRecentFiles(FileCatalog& catalog, std::size_t offset) {
	return publishFilePage(catalog.recentFiles(), offset, 3);
}

bool DocumentBridge::listRoots(FileCatalog& catalog, std::size_t offset) {
	return publishFilePage(catalog.roots(), offset, 2);
}

bool DocumentBridge::publishRecoveryPage(const QVector<FileEntry>& records,
	std::size_t offset) {
	return publishFilePage(records, offset, 4);
}

/*
 * List results are deliberately paged before they enter the bounded event
 * ring. The cartridge owns navigation and requests the next offset; the host
 * cannot partially publish an arbitrarily large directory and leave the UI in
 * an unknowable state.
 */

bool DocumentBridge::publishRecoveryAvailable(const QString& format) {
	MailboxRecord event;
	event.kind = EventRecoveryAvailable;
	event.revision = m_engine.revision();
	const QByteArray token = QByteArrayLiteral("current");
	const QByteArray type = format.toUtf8();
	event.payload.push_back(static_cast<std::uint8_t>(token.size()));
	event.payload.push_back(static_cast<std::uint8_t>(type.size()));
	event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(token.constData()), reinterpret_cast<const std::uint8_t*>(token.constData() + token.size()));
	event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(type.constData()), reinterpret_cast<const std::uint8_t*>(type.constData() + type.size()));
	return m_events.push(event);
}

bool DocumentBridge::recover(const QString& path, const QString& originalFilename) {
	Q_UNUSED(originalFilename);
	if (!m_persistence.recover(path).succeeded()) return false;
	releaseView();
	MailboxRecord event;
	event.kind = EventRecoveryRestored;
	event.revision = m_engine.revision();
	return m_events.push(event) && publish() && publishStructureSuggestion();
}

bool DocumentBridge::saveAs(FileCatalog& catalog, const QString& id, bool overwriteConfirmed) {
	const FileEntry* file = catalog.entry(id);
	if (!file || file->directory) {
		PersistenceResult invalid;
		invalid.error = PersistenceError::InvalidPath;
		invalid.detail = QStringLiteral("The selected save target is no longer valid.");
		return publishPersistenceFailure(invalid);
	}
	if (!file->writable) {
		PersistenceResult read_only;
		read_only.error = PersistenceError::ReadOnly;
		read_only.path = file->absolutePath;
		read_only.detail = QStringLiteral("The selected save target is read-only.");
		return publishPersistenceFailure(read_only);
	}
	if (!overwriteConfirmed) {
		MailboxRecord event;
		event.kind = EventOverwriteRequired;
		event.revision = m_engine.revision();
		const QByteArray bytes = id.toUtf8();
		event.payload.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()), reinterpret_cast<const std::uint8_t*>(bytes.constData() + bytes.size()));
		return m_events.push(event);
	}
	const PersistenceResult result = m_persistence.saveAs(file->absolutePath, true);
	if (!result.succeeded()) return publishPersistenceFailure(result);
	catalog.noteRecent(id);
	return publishPersistenceSettings() && publish();
}

bool DocumentBridge::saveAsNew(FileCatalog& catalog, const QString& parentId, const QString& name) {
	const QString path = catalog.newFilePath(parentId, name);
	if (path.isEmpty()) {
		PersistenceResult invalid;
		invalid.error = PersistenceError::InvalidPath;
		invalid.detail = QStringLiteral("The selected folder or filename is no longer valid.");
		return publishPersistenceFailure(invalid);
	}
	const PersistenceResult result = m_persistence.saveAs(path, false);
	if (!result.succeeded()) return publishPersistenceFailure(result);
	const QString id = catalog.registerPath(path);
	const FileEntry* file = catalog.entry(id);
	if (!file) return false;
	MailboxRecord event;
	event.kind = EventFileEntry;
	event.revision = m_engine.revision();
	const QByteArray id_bytes = file->id.toUtf8();
	const QByteArray name_bytes = file->name.toUtf8();
	event.payload.push_back(static_cast<std::uint8_t>(id_bytes.size()));
	event.payload.push_back(static_cast<std::uint8_t>(name_bytes.size()));
	event.payload.push_back(static_cast<std::uint8_t>(name_bytes.size() >> 8));
	event.payload.push_back(static_cast<std::uint8_t>(file->writable ? 2 : 0));
	for (int i = 0; i < 8; ++i) event.payload.push_back(static_cast<std::uint8_t>(file->size >> (i * 8)));
	const auto modified = static_cast<std::uint64_t>(file->modified.toMSecsSinceEpoch());
	for (int i = 0; i < 8; ++i) event.payload.push_back(static_cast<std::uint8_t>(modified >> (i * 8)));
	event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(id_bytes.constData()), reinterpret_cast<const std::uint8_t*>(id_bytes.constData() + id_bytes.size()));
	event.payload.insert(event.payload.end(), reinterpret_cast<const std::uint8_t*>(name_bytes.constData()), reinterpret_cast<const std::uint8_t*>(name_bytes.constData() + name_bytes.size()));
	catalog.noteRecent(id);
	return m_events.push(event) && publishPersistenceSettings() && publish();
}

bool DocumentBridge::publishPersistenceFailure(const PersistenceResult& result) {
	MailboxRecord event;
	event.revision = m_engine.revision();
	switch (result.error) {
	case PersistenceError::NeedsSaveAs:
		event.kind = EventSaveAsRequired;
		break;
	case PersistenceError::ExternalConflict:
	case PersistenceError::AlreadyExists:
		event.kind = EventSaveConflict;
		break;
	case PersistenceError::ReadOnly:
		event.kind = EventReadOnly;
		break;
	default:
		event.kind = EventPersistenceFailed;
		break;
	}
	event.payload.push_back(static_cast<std::uint8_t>(result.error));
	const QByteArray detail = result.detail.toUtf8().left(240);
	event.payload.insert(event.payload.end(),
		reinterpret_cast<const std::uint8_t*>(detail.constData()),
		reinterpret_cast<const std::uint8_t*>(detail.constData() + detail.size()));
	return m_events.push(event);
}

bool DocumentBridge::publishSoundSettings() {
	const SoundSettings& sound = m_persistence.soundSettings();
	MailboxRecord event;
	event.kind = EventSoundSettings;
	event.revision = m_engine.revision();
	event.payload = {
		static_cast<std::uint8_t>(sound.typing_blips ? 1 : 0),
		static_cast<std::uint8_t>(sound.waveform),
		sound.attack,
		sound.decay,
		sound.sustain_level,
		sound.sustain_rate,
		sound.release,
		sound.pitch,
		sound.volume,
		sound.echo_volume,
		sound.echo_delay,
		sound.echo_feedback
	};
	return m_events.push(event);
}

bool DocumentBridge::publishPersistenceSettings() {
	const PersistenceSettings& settings = m_persistence.settings();
	DocumentFormat format = DocumentFormat::Odt;
	documentFormatFromName(m_engine.format(), format);
	MailboxRecord event;
	event.kind = EventPersistenceSettings;
	event.revision = m_engine.revision();
	event.payload = {
		static_cast<std::uint8_t>(settings.mode),
		settings.interval_minutes,
		settings.recovery_copies,
		static_cast<std::uint8_t>(m_engine.markdownSourceMode() ? 1 : 0),
		static_cast<std::uint8_t>(format),
		settings.canvas_color
	};
	return m_events.push(event);
}

bool DocumentBridge::publishTransitionRequired() {
	MailboxRecord event;
	event.kind = EventTransitionRequired;
	event.revision = m_engine.revision();
	return m_events.push(event);
}

bool DocumentBridge::createDirectory(FileCatalog& catalog, const QString& parentId, const QString& name) {
	const QString id = catalog.createDirectory(parentId, name);
	if (id.isEmpty()) return false;
	MailboxRecord event;
	event.kind = EventDirectoryCreated;
	event.revision = m_engine.revision();
	const QByteArray bytes = id.toUtf8();
	event.payload.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()), reinterpret_cast<const std::uint8_t*>(bytes.constData() + bytes.size()));
	return m_events.push(event);
}

bool DocumentBridge::publishStatistics() {
	const DocumentEngine::Statistics stats = m_engine.statistics();
	MailboxRecord event;
	event.kind = EventStatistics;
	event.revision = m_engine.revision();
	event.payload.resize(12);
	const auto put32 = [&event](std::size_t offset, std::uint32_t value) {
		for (int i = 0; i < 4; ++i) event.payload[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
	};
	put32(0, stats.characters); put32(4, stats.words); put32(8, stats.lines);
	return m_events.push(event);
}

bool DocumentBridge::publishGoalProgress() {
	const DocumentEngine::Statistics stats = m_engine.statistics();
	MailboxRecord event;
	event.kind = EventGoalProgress;
	event.revision = m_engine.revision();
	event.payload.resize(16);
	const auto put32 = [&event](std::size_t offset, std::uint32_t value) {
		for (int i = 0; i < 4; ++i) event.payload[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
	};
	put32(0, stats.words);
	put32(4, m_word_goal);
	const std::uint32_t percent = m_word_goal == 0 ? 0 : std::min<std::uint32_t>(100, (stats.words * 100u) / m_word_goal);
	put32(8, percent);
	put32(12, stats.characters);
	return m_events.push(event);
}

bool DocumentBridge::pump() {
	MailboxRecord command;
	if (!m_commands.pop(command)) return false;
	const bool current = mailboxRevisionMatches(command, m_engine.revision());
	if (current && command.kind == CommandGetPersistenceSettings) {
		return publishPersistenceSettings();
	}
	if (current && command.kind == CommandSetPersistenceSettings) {
		if (command.payload.size() != 4 || command.payload[0] > 1
			|| command.payload[3] >= CanvasPalette::Count) return false;
		PersistenceSettings settings = m_persistence.settings();
		settings.mode = static_cast<PersistenceSettings::AutosaveMode>(
			command.payload[0]);
		settings.interval_minutes = std::max<std::uint8_t>(1, command.payload[1]);
		settings.recovery_copies = command.payload[2];
		// The canvas index is rejected rather than clamped: the cartridge wraps
		// it inside the palette itself, so an out-of-range value is a wire
		// disagreement between the two tables, not a user choice to round off.
		settings.canvas_color = command.payload[3];
		m_persistence.setSettings(settings);
		return publishPersistenceSettings();
	}
	if (current && command.kind == CommandGetSoundSettings) {
		return publishSoundSettings();
	}
	if (current && command.kind == CommandSetSoundSettings) {
		// One atomic nine-byte value, for the same reason the persistence
		// settings cross as one three-byte value: a torn update here would not
		// be a stale reading, it would be a voice configured from half of two
		// different presets.
		if (command.payload.size() != 12) return false;
		if (command.payload[0] > 1 || command.payload[1] > 2) return false;
		SoundSettings settings;
		settings.typing_blips = command.payload[0] != 0;
		settings.waveform = static_cast<SoundSettings::Waveform>(command.payload[1]);
		// Each mask is the width of the register field the value lands in.
		settings.attack = command.payload[2] & 0x0f;
		settings.decay = command.payload[3] & 0x07;
		settings.sustain_level = command.payload[4] & 0x07;
		settings.sustain_rate = command.payload[5] & 0x1f;
		settings.release = command.payload[6] & 0x1f;
		settings.pitch = command.payload[7] & 0x3f;
		settings.volume = command.payload[8] & 0x7f;
		settings.echo_volume = command.payload[9] & 0x7f;
		settings.echo_delay = command.payload[10] & 0x0f;
		settings.echo_feedback = command.payload[11] & 0x7f;
		m_persistence.setSoundSettings(settings);
		return publishSoundSettings();
	}
	if (current && command.kind == CommandSetMarkdownView) {
		if (command.payload.size() != 1 || command.payload[0] > 1) return false;
		m_engine.setMarkdownSourceMode(command.payload[0] != 0);
		return publishPersistenceSettings() && publish();
	}
	if (current && command.kind == CommandSetWordGoal) {
		if (command.payload.size() != 4) return false;
		m_word_goal = static_cast<std::uint32_t>(command.payload[0]) |
			(static_cast<std::uint32_t>(command.payload[1]) << 8) |
			(static_cast<std::uint32_t>(command.payload[2]) << 16) |
			(static_cast<std::uint32_t>(command.payload[3]) << 24);
		return publishGoalProgress();
	}
	if (current && command.kind == CommandScrollToFraction) {
		if (command.payload.size() != 2) return false;
		const std::uint32_t thumb = std::min<std::uint32_t>(
			static_cast<std::uint32_t>(command.payload[0])
				| (static_cast<std::uint32_t>(command.payload[1]) << 8),
			CommandScrollTrackTravel);
		const auto length = static_cast<std::uint32_t>(m_engine.document()->characterCount());
		// Anchor the window without touching the caret. The cartridge derives the
		// thumb from UTF-8 byte counts while this works in characters; the product
		// is deliberately single-byte-glyph only, so the two agree, and the host
		// republishes the thumb from bytes afterwards either way.
		m_scroll_anchor = static_cast<std::uint32_t>(
			static_cast<std::uint64_t>(thumb) * length / CommandScrollTrackTravel);
		return publish();
	}
	if (current && (command.kind == CommandPointerSetCursor
			|| command.kind == CommandPointerExtendCursor)) {
		if (command.payload.size() != 2) return false;
		const std::uint32_t offset = static_cast<std::uint32_t>(command.payload[0]) |
			(static_cast<std::uint32_t>(command.payload[1]) << 8);
		const int position = static_cast<int>(m_viewport_start.value_or(0) + offset);
		// Placing the caret ends the scroll: the window follows the caret again.
		// The view does not jump, because the caret was just placed inside the
		// scrolled window and that window is still the stability hint.
		m_scroll_anchor.reset();
		if (command.kind == CommandPointerExtendCursor)
			m_engine.setSelectionEndPosition(command.revision, position);
		else
			m_engine.setCursorPosition(command.revision, position);
		return publish();
	}
	if (current && command.kind == CommandListSessions) return listSessions();
	if (current && command.kind == CommandCreateSession) {
		const QString name = QString::fromUtf8(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size()));
		return createSession(name);
	}
	if (current && command.kind == DocumentEngine::CopySelection) {
		MailboxRecord copied;
		copied.kind = 0x8100;
		copied.revision = m_engine.revision();
		const QByteArray text = m_engine.selectedUtf8();
		copied.payload.assign(reinterpret_cast<const std::uint8_t*>(text.constData()), reinterpret_cast<const std::uint8_t*>(text.constData() + text.size()));
		if (!m_events.push(copied)) return false;
		return publish();
	}
	if (current && command.kind == DocumentEngine::Save) {
		const PersistenceResult result = m_persistence.save();
		if (!result.succeeded() && !publishPersistenceFailure(result)) return false;
		return publish();
	}
	// Everything from here is a real editing or caret command, so any scroll the
	// user was holding is over and the window snaps back to the caret.
	if (current) m_scroll_anchor.reset();
	const bool applied = current && m_engine.apply(command);
	if (current && command.kind == DocumentEngine::FindNext) {
		MailboxRecord result;
		result.kind = EventFindResult;
		result.revision = m_engine.revision();
		result.payload.resize(9);
		const auto put32 = [&result](std::size_t offset, std::uint32_t value) {
			for (int i = 0; i < 4; ++i) result.payload[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
		};
		result.payload[0] = applied && m_engine.cursor().hasSelection() ? 1 : 0;
		put32(1, static_cast<std::uint32_t>(m_engine.cursor().selectionStart()));
		put32(5, static_cast<std::uint32_t>(m_engine.cursor().selectionEnd()));
		if (!m_events.push(result)) return false;
	}
	if (!applied && !current) {
		MailboxRecord conflict;
		conflict.kind = 0x8001;
		conflict.revision = m_engine.revision();
		conflict.payload.resize(8);
		for (int i = 0; i < 8; ++i) conflict.payload[i] = static_cast<std::uint8_t>(conflict.revision >> (i * 8));
		if (!m_events.push(conflict)) return false;
	}
	return publish();
}

} // namespace FairyWriter
