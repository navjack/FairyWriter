#ifndef FAIRYWRITER_DOCUMENT_ENGINE_H
#define FAIRYWRITER_DOCUMENT_ENGINE_H

#include <QTextCursor>
#include <QTextDocument>
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <cstdint>
#include <limits>
#include <optional>
#include "mailbox.h"

namespace FairyWriter {

class DocumentEngine final {
public:
	struct Statistics {
		std::uint32_t characters = 0;
		std::uint32_t words = 0;
		std::uint32_t lines = 1;
		std::uint32_t paragraphs = 1;
		std::uint32_t pages = 1;
	};
	enum Command : std::uint16_t {
		InsertText = 1, DeleteSelection = 2, MoveStart = 3, MoveEnd = 4,
		Undo = 5, Redo = 6, MoveLeft = 7, MoveRight = 8,
		MoveWordLeft = 9, MoveWordRight = 10, ReplaceAll = 11,
		ExtendLeft = 12, ExtendRight = 13, MoveUp = 14, MoveDown = 15,
		MoveLineStart = 16, MoveLineEnd = 17, MovePageUp = 18,
		MovePageDown = 19, ToggleBold = 20, ToggleItalic = 21,
		ToggleUnderline = 22, AlignLeft = 23, AlignCenter = 24,
		AlignRight = 25, IndentIncrease = 26, IndentDecrease = 27,
		ListBullet = 28, ListOrdered = 29, CopySelection = 30,
		PasteText = 31, Save = 32, NewDocument = 33, SelectAll = 34,
		FindNext = 35, SetSmartQuotes = 36, DeleteBackward = 37,
		DeleteForward = 38, ExtendUp = 39, ExtendDown = 40,
		ExtendLineStart = 41, ExtendLineEnd = 42,
		PromoteChapterHeadings = 43, SetCursorPosition = 44,
		ExtendCursorPosition = 45
	};
	DocumentEngine();

	std::uint64_t revision() const noexcept { return m_revision; }
	bool isDirty() const noexcept { return m_revision != m_saved_revision; }
	const QTextDocument* document() const noexcept { return &m_document; }
	QTextDocument* document() noexcept { return &m_document; }
	const QTextCursor& cursor() const noexcept { return m_cursor; }
	void syncCursor(const QTextCursor& cursor) { m_cursor = cursor; }
	QString text() const { return cachedPlainText(); }
	Statistics statistics() const;
	bool isReadOnly() const noexcept { return m_read_only; }
	void setReadOnly(bool value) noexcept { m_read_only = value; }
	QString filename() const { return m_filename; }
	QString format() const { return m_format; }
	bool load(const QString& filename, const QString& type = QString());
	bool save(const QString& filename = QString());
	// Recovery writes are independent of the opened file: they never change
	// filename, revision, dirty state, or the authoritative document.
	bool writeRecovery(const QString& filename) const;
	bool recover(const QString& filename, const QString& originalFilename = QString());
	bool newDocument(std::uint64_t expected_revision);

	// All mutations pass through this object. A command carrying an old
	// revision is rejected without changing text, cursor, or undo history.
	bool insertText(std::uint64_t expected_revision, const QString& value);
	bool deleteSelection(std::uint64_t expected_revision);
	bool deleteBackward(std::uint64_t expected_revision);
	bool deleteForward(std::uint64_t expected_revision);
	bool moveCursor(std::uint64_t expected_revision, QTextCursor::MoveOperation op,
		QTextCursor::MoveMode mode = QTextCursor::MoveAnchor, int n = 1);
	// Places the collapsed cursor at an absolute document position, clamped into
	// range. The cartridge resolves a pointer click to a viewport-relative
	// offset; the bridge adds the published viewport start and calls this so the
	// host, not the cartridge, owns the authoritative caret position.
	bool setCursorPosition(std::uint64_t expected_revision, int position);
	// Moves the active end of the selection to an absolute position, keeping the
	// existing anchor. A pointer drag calls this after the press set the anchor
	// with setCursorPosition, so the host owns the selection range.
	bool setSelectionEndPosition(std::uint64_t expected_revision, int position);
	bool undo(std::uint64_t expected_revision);
	bool redo(std::uint64_t expected_revision);
	bool selectAll(std::uint64_t expected_revision);
	bool replaceAll(std::uint64_t expected_revision, const QString& find, const QString& replacement);
	bool findNext(std::uint64_t expected_revision, const QString& query);
	std::uint32_t inferredChapterCount() const;
	bool promoteInferredChapterHeadings(std::uint64_t expected_revision);
	void setSmartQuotes(bool enabled) noexcept { m_smart_quotes = enabled; }
	bool smartQuotes() const noexcept { return m_smart_quotes; }
	bool apply(const MailboxRecord& command);
	// preferred_start is normally only a stability hint: it is honoured while it
	// still contains the cursor, so the window does not re-centre on every
	// keystroke. force_start makes it authoritative instead, which is what lets
	// the scrollbar move the view away from the caret entirely.
	bool makeViewport(std::size_t max_text_bytes, ViewportSnapshot& snapshot,
		std::optional<std::uint32_t> preferred_start = std::nullopt,
		bool force_start = false) const;
	QByteArray selectedUtf8() const { return m_cursor.selectedText().toUtf8(); }
	QString selectedText() const { return m_cursor.selectedText(); }

private:
	bool accepts(std::uint64_t expected_revision) const noexcept;
	void commit();
	void refreshDocumentCache() const;
	const QString& cachedPlainText() const;
	QTextDocument m_document;
	QTextCursor m_cursor;
	std::uint64_t m_revision = 0;
	std::uint64_t m_saved_revision = 0;
	QString m_filename;
	QString m_format = QStringLiteral("odt");
	QDateTime m_loaded_modified;
	bool m_has_loaded_file = false;
	bool m_read_only = false;
	bool m_smart_quotes = true;
	mutable int m_cached_document_revision = std::numeric_limits<int>::min();
	mutable QString m_cached_plain_text;
	mutable Statistics m_cached_statistics;
	mutable std::vector<int> m_cached_chapter_positions;
	mutable std::uint32_t m_cached_inferred_chapters = 0;
};

} // namespace FairyWriter

#endif
