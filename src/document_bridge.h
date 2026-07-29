#ifndef FAIRYWRITER_DOCUMENT_BRIDGE_H
#define FAIRYWRITER_DOCUMENT_BRIDGE_H

#include "document_engine.h"
#include "mailbox.h"
#include "file_catalog.h"
#include <optional>

namespace FairyWriter {

class DocumentBridge final {
public:
	static constexpr std::uint16_t EventFileEntry = 0x8200;
	static constexpr std::uint16_t CommandListFiles = 0x0100;
	static constexpr std::uint16_t CommandOpenFile = 0x0101;
	static constexpr std::uint16_t CommandSaveAs = 0x0102;
	static constexpr std::uint16_t CommandCreateDirectory = 0x0103;
	static constexpr std::uint16_t CommandPointerSetCursor = 0x0104;
	static constexpr std::uint16_t CommandPointerExtendCursor = 0x010f;
	// Scrollbar drag. Payload is the thumb's position along its travel as a
	// 16-bit numerator over CommandScrollTrackTravel. This scrolls the view only:
	// the caret stays where it is, and the user places it by clicking in the
	// scrolled view like any other click.
	static constexpr std::uint16_t CommandScrollToFraction = 0x0110;
	static constexpr std::uint16_t CommandGetPersistenceSettings = 0x0111;
	static constexpr std::uint16_t CommandSetPersistenceSettings = 0x0112;
	static constexpr std::uint16_t CommandSetMarkdownView = 0x0113;
	static constexpr std::uint16_t CommandTransitionDecision = 0x0114;
	static constexpr std::uint32_t CommandScrollTrackTravel = 226;
	static constexpr std::uint16_t CommandStatistics = 0x0105;
	static constexpr std::uint16_t CommandSetWordGoal = 0x010b;
	static constexpr std::uint16_t CommandListSessions = 0x010c;
	static constexpr std::uint16_t CommandCreateSession = 0x010d;
	static constexpr std::uint16_t CommandSwitchSession = 0x010e;
	static constexpr std::uint16_t CommandRecentFiles = 0x0106;
	static constexpr std::uint16_t CommandListRoots = 0x0107;
	static constexpr std::uint16_t CommandListRecovery = 0x0108;
	static constexpr std::uint16_t CommandRecover = 0x0109;
	static constexpr std::uint16_t CommandSaveAsNew = 0x010a;
	static constexpr std::uint16_t EventDirectoryCreated = 0x8201;
	static constexpr std::uint16_t EventStatistics = 0x8203;
	static constexpr std::uint16_t EventGoalProgress = 0x820a;
	static constexpr std::uint16_t EventFindResult = 0x820b;
	static constexpr std::uint16_t EventSessionEntry = 0x820c;
	static constexpr std::uint16_t EventSessionChanged = 0x820d;
	static constexpr std::uint16_t EventStructureSuggestion = 0x820e;
	static constexpr std::uint16_t EventFileListComplete = 0x820f;
	static constexpr std::uint16_t EventRecoveryAvailable = 0x8204;
	static constexpr std::uint16_t EventRecoveryRestored = 0x8205;
	static constexpr std::uint16_t EventOverwriteRequired = 0x8206;
	static constexpr std::uint16_t EventSaveConflict = 0x8207;
	static constexpr std::uint16_t EventReadOnly = 0x8208;
	static constexpr std::uint16_t EventOpenFailed = 0x8209;
	static constexpr std::uint16_t EventSaveAsRequired = 0x8210;
	static constexpr std::uint16_t EventPersistenceFailed = 0x8211;
	static constexpr std::uint16_t EventPersistenceSettings = 0x8212;
	static constexpr std::uint16_t EventTransitionRequired = 0x8213;
	explicit DocumentBridge(QString recovery_root = QString());
	DocumentEngine& engine() noexcept { return m_engine; }
	const DocumentEngine& engine() const noexcept { return m_engine; }
	DocumentPersistence& persistence() noexcept { return m_persistence; }
	const DocumentPersistence& persistence() const noexcept { return m_persistence; }
	bool submit(const MailboxRecord& command);
	bool pump();
	bool openFile(FileCatalog& catalog, const QString& id);
	static constexpr std::size_t FilePageSize = 7;
	bool listFiles(FileCatalog& catalog, const QString& parentId = QString(),
		bool showHidden = false, std::size_t offset = 0,
		std::optional<DocumentFormat> format = std::nullopt);
	bool listRecentFiles(FileCatalog& catalog, std::size_t offset = 0);
	bool listRoots(FileCatalog& catalog, std::size_t offset = 0);
	bool publishRecoveryPage(const QVector<FileEntry>& records,
		std::size_t offset = 0);
	bool publishRecoveryAvailable(const QString& format);
	bool recover(const QString& path, const QString& originalFilename = QString());
	bool saveAs(FileCatalog& catalog, const QString& id, bool overwriteConfirmed = false);
	bool saveAsNew(FileCatalog& catalog, const QString& parentId, const QString& name);
	bool createDirectory(FileCatalog& catalog, const QString& parentId, const QString& name);
	bool publishStatistics();
	bool publishGoalProgress();
	bool publishPersistenceFailure(const PersistenceResult& result);
	bool publishPersistenceSettings();
	bool publishTransitionRequired();
	bool listSessions();
	bool createSession(const QString& name);
	bool switchSession(FileCatalog& catalog, const QString& id);
	const ViewportSlots& viewports() const noexcept { return m_viewports; }
	MailboxRing& events() noexcept { return m_events; }

private:
	bool publish();
	bool publishStructureSuggestion();
	bool publishFilePage(const QVector<FileEntry>& files, std::size_t offset,
		std::uint8_t source);
	DocumentEngine m_engine;
	DocumentPersistence m_persistence;
	MailboxRing m_commands{MailboxLayout::CommandBytes};
	MailboxRing m_events{MailboxLayout::EventBytes};
	ViewportSlots m_viewports;
	std::optional<std::uint32_t> m_viewport_start;
	// Set while the view has been scrolled away from the caret. It is
	// authoritative for the next publish and is released by anything that moves
	// the caret or edits, so typing or clicking snaps the view back to the caret.
	std::optional<std::uint32_t> m_scroll_anchor;
	std::uint32_t m_word_goal = 0;
	QString m_session_id;
};

} // namespace FairyWriter

#endif
