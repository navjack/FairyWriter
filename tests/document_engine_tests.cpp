#include "document_engine.h"
#include "document_bridge.h"
#include "session_store.h"
#include "text_codec.h"
#include <iostream>
#include <QFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QThread>

using FairyWriter::DocumentEngine;
using FairyWriter::MailboxRecord;
static int failures = 0;
static void expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }
static std::uint16_t read16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
	return static_cast<std::uint16_t>(bytes[offset])
		| (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}
static std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
	return static_cast<std::uint32_t>(read16(bytes, offset))
		| (static_cast<std::uint32_t>(read16(bytes, offset + 2)) << 16);
}

int main(int argc, char** argv) {
	if (argc > 2) return 2;
	DocumentEngine engine;
	QTemporaryDir session_dir;
	FairyWriter::SessionStore::setPath(session_dir.path());
	FairyWriter::SessionStore sessions;
	const QString session_id = sessions.create(QStringLiteral("Writing Session"));
	expect(!session_id.isEmpty() && sessions.list().size() == 1, "session store creates and lists a persistent session");
	expect(sessions.save(session_id, { QStringList{QStringLiteral("/tmp/story.odt")}, 2 }), "session store saves current files and active index");
	const auto loaded_session = sessions.load(session_id);
	expect(loaded_session.name == QStringLiteral("Writing Session") && loaded_session.files == QStringList{QStringLiteral("/tmp/story.odt")} && loaded_session.active == 2, "session store round trips FocusWriter save keys");
	FairyWriter::DocumentBridge session_bridge;
	expect(session_bridge.createSession(QStringLiteral("Bridge Session")), "bridge creates a persisted session entry");
	MailboxRecord session_event;
	expect(session_bridge.events().pop(session_event) && session_event.kind == FairyWriter::DocumentBridge::EventSessionEntry && session_event.payload.size() > 4, "bridge emits an opaque session entry event");
	expect(session_bridge.listSessions() && session_bridge.events().pop(session_event) && session_event.kind == FairyWriter::DocumentBridge::EventSessionEntry, "bridge lists persisted session entries");
	while (session_bridge.events().pop(session_event)) {}
	QTemporaryDir session_files;
	const QString session_file_path = session_files.filePath(QStringLiteral("session.txt"));
	QFile session_file(session_file_path);
	expect(session_file.open(QIODevice::WriteOnly) && session_file.write("session document") == 16, "session fixture writes a real document");
	session_file.close();
	expect(sessions.save(session_id, { QStringList{session_file_path}, 0 }), "session store persists the active document path");
	FairyWriter::FileCatalog session_catalog(session_files.path());
	expect(session_bridge.switchSession(session_catalog, session_id) && session_bridge.engine().text() == QStringLiteral("session document"), "session switch loads the persisted active document through the catalog");
	expect(session_bridge.events().pop(session_event) && session_event.kind == FairyWriter::DocumentBridge::EventSessionChanged, "session switch publishes a typed changed event");
	MailboxRecord insert; insert.kind=DocumentEngine::InsertText; insert.payload={'a','l','p','h','a',' ','b','e','t','a','\n','g','a','m','m','a'};
	expect(engine.apply(insert), "mailbox insertion succeeds");
	expect(engine.text() == "alpha beta\ngamma", "text is authoritative");
	const auto stats = engine.statistics();
	expect(stats.characters == 16 && stats.words == 3 && stats.lines == 2, "statistics report characters, words, and lines");
	DocumentEngine quotes;
	expect(quotes.insertText(0, QStringLiteral("\"hello\" 'world'")) && quotes.text() == QString::fromUtf8("\u201chello\u201d \u2018world\u2019"), "engine applies locale-neutral smart quotes to inserted text");
	quotes.setSmartQuotes(false);
	const auto quotes_revision = quotes.revision();
	expect(quotes.insertText(quotes_revision, QStringLiteral(" \"raw\"")) && quotes.text().endsWith(QStringLiteral(" \"raw\"")), "smart quote setting disables transformation without losing literal punctuation");
	const auto revision = engine.revision();
	insert.revision = revision - 1; insert.payload={'s','t','a','l','e'};
	expect(!engine.apply(insert), "stale mailbox insertion is rejected");
	expect(engine.text() == "alpha beta\ngamma", "stale insertion does not mutate text");
	MailboxRecord start; start.kind=DocumentEngine::MoveStart; start.revision=revision;
	expect(engine.apply(start), "mailbox cursor movement succeeds");
	const auto moved = engine.revision();
	insert.revision = moved; insert.payload={'X'};
	expect(engine.apply(insert), "cursor insertion succeeds");
	const auto after_insert = engine.revision();
	expect(engine.undo(after_insert), "undo succeeds");
	const auto after_undo = engine.revision();
	expect(engine.text() == "alpha beta\ngamma", "undo restores text");
	expect(engine.redo(after_undo), "redo succeeds");
	expect(engine.text() == "Xalpha beta\ngamma", "redo restores insertion");
	DocumentEngine shortcut_engine;
	expect(shortcut_engine.insertText(0, QStringLiteral("Xalpha beta\ngamma")), "shortcut fixture inserts text");
	MailboxRecord select_all; select_all.kind = DocumentEngine::SelectAll; select_all.revision = shortcut_engine.revision();
	expect(shortcut_engine.apply(select_all) && shortcut_engine.cursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n')) == shortcut_engine.text(), "select-all creates an authoritative full-document selection");
	const auto selected_revision = shortcut_engine.revision();
	expect(shortcut_engine.deleteSelection(selected_revision) && shortcut_engine.text().isEmpty(), "select-all selection can be deleted");
	const auto deleted_revision = shortcut_engine.revision();
	expect(shortcut_engine.undo(deleted_revision) && shortcut_engine.text() == "Xalpha beta\ngamma", "undo restores a select-all deletion");
	expect(shortcut_engine.redo(shortcut_engine.revision()) && shortcut_engine.text().isEmpty(), "redo reapplies a select-all deletion");
	FairyWriter::ViewportSnapshot snapshot;
	expect(engine.makeViewport(64, snapshot) && snapshot.revision == engine.revision() && snapshot.cursor == engine.cursor().position() && snapshot.word_count == 3, "viewport reflects authoritative engine state and statistics");
	expect(std::string(snapshot.display_title.begin(), snapshot.display_title.end()) == "UNTITLED" && snapshot.chapter == 1,
		"untitled viewport publishes a semantic title and first chapter");
	QTemporaryDir header_directory;
	DocumentEngine header_engine;
	expect(header_engine.insertText(0, QStringLiteral("First chapter\nbody\nSecond chapter\nmore")), "header metadata fixture inserts");
	QTextBlock first_heading = header_engine.document()->begin();
	QTextBlockFormat first_heading_format = first_heading.blockFormat();
	first_heading_format.setHeadingLevel(1);
	QTextCursor first_heading_cursor(first_heading);
	first_heading_cursor.setBlockFormat(first_heading_format);
	QTextBlock second_heading = first_heading.next().next();
	QTextBlockFormat second_heading_format = second_heading.blockFormat();
	second_heading_format.setHeadingLevel(1);
	QTextCursor second_heading_cursor(second_heading);
	second_heading_cursor.setBlockFormat(second_heading_format);
	QTextCursor chapter_cursor(header_engine.document());
	chapter_cursor.setPosition(second_heading.position());
	header_engine.syncCursor(chapter_cursor);
	const QString header_path = header_directory.filePath(QStringLiteral("my-résumé.odt"));
	expect(header_engine.save(header_path), "header metadata fixture saves with a real filename");
	FairyWriter::ViewportSnapshot header_snapshot;
	expect(header_engine.makeViewport(128, header_snapshot) && header_snapshot.chapter == 2
		&& header_snapshot.word_count == 6
		&& std::string(header_snapshot.display_title.begin(), header_snapshot.display_title.end()) == "MY-RESUME",
		"viewport derives chapter, words, and resident display title from authoritative document state");
	DocumentEngine marker_chapters;
	expect(marker_chapters.insertText(0, QStringLiteral("Prologue\nopening\nChapter 1\none\nChapter II\ntwo\nChapter III\nthree")),
		"plain chapter-marker fixture inserts");
	QTextCursor marker_cursor(marker_chapters.document());
	marker_cursor.setPosition(marker_chapters.text().indexOf(QStringLiteral("Chapter II")));
	marker_chapters.syncCursor(marker_cursor);
	FairyWriter::ViewportSnapshot marker_snapshot;
	expect(marker_chapters.makeViewport(128, marker_snapshot) && marker_snapshot.chapter == 2,
		"plain Chapter paragraphs advance the chapter indicator without heading metadata");
	expect(marker_chapters.inferredChapterCount() == 3,
		"plain chapter markers are reported as non-destructive structure suggestions");
	expect(marker_chapters.promoteInferredChapterHeadings(marker_chapters.revision())
		&& marker_chapters.inferredChapterCount() == 0,
		"accepted chapter suggestion converts all inferred markers in one transaction");
	expect(marker_chapters.undo(marker_chapters.revision())
		&& marker_chapters.inferredChapterCount() == 3,
		"chapter metadata promotion is undoable as one document transaction");
	expect(marker_chapters.redo(marker_chapters.revision())
		&& marker_chapters.inferredChapterCount() == 0,
		"chapter metadata promotion can be redone without re-running inference");
	DocumentEngine huge;
	huge.insertText(0, QString(20000, QChar('x')));
	FairyWriter::ViewportSnapshot bounded;
	expect(huge.makeViewport(128, bounded) && bounded.utf8.size() <= 128 && bounded.text_offset > 0, "large document produces bounded viewport window");
	DocumentEngine hundred_thousand;
	QString hundred_thousand_words;
	hundred_thousand_words.reserve(600000);
	for (int i = 0; i < 100000; ++i) {
		if (i) hundred_thousand_words += QLatin1Char(' ');
		hundred_thousand_words += QStringLiteral("word");
	}
	expect(hundred_thousand.insertText(0, hundred_thousand_words), "100000-word document inserts without truncation");
	expect(hundred_thousand.statistics().words == 100000 && hundred_thousand.text().size() == hundred_thousand_words.size(), "100000-word statistics and authoritative text remain exact");
	FairyWriter::ViewportSnapshot hundred_view;
	expect(hundred_thousand.makeViewport(128, hundred_view) && hundred_view.utf8.size() <= 128 && hundred_view.text_offset > 0, "100000-word document produces a bounded viewport around the cursor");
	MailboxRecord hundred_end; hundred_end.kind = DocumentEngine::MoveEnd; hundred_end.revision = hundred_thousand.revision();
	expect(hundred_thousand.apply(hundred_end), "100000-word cursor reaches document end");
	MailboxRecord hundred_insert; hundred_insert.kind = DocumentEngine::InsertText; hundred_insert.revision = hundred_thousand.revision(); hundred_insert.payload = {' ', 'e', 'n', 'd'};
	expect(hundred_thousand.apply(hundred_insert) && hundred_thousand.text().endsWith(QStringLiteral(" end")), "100000-word document accepts an edit at its final viewport boundary");
	DocumentEngine unicode_view;
	expect(unicode_view.insertText(0, QStringLiteral("prefix e\u0301 family U0001F469\u200DU0001F4BB suffix")), "grapheme viewport fixture inserts Unicode text");
	FairyWriter::ViewportSnapshot unicode_snapshot;
	expect(unicode_view.makeViewport(12, unicode_snapshot), "Unicode viewport remains available under a tight byte budget");
	const QString unicode_text = QString::fromUtf8(reinterpret_cast<const char*>(unicode_snapshot.utf8.data()), static_cast<qsizetype>(unicode_snapshot.utf8.size()));
	expect(!unicode_text.contains(QChar(0xfffd)) && !unicode_text.endsWith(QChar(0x0301)) && !unicode_text.endsWith(QChar(0x200d)), "viewport never cuts a grapheme cluster");
	DocumentEngine grapheme_delete;
	expect(grapheme_delete.insertText(0, QStringLiteral("Ae\u0301\U0001F469\u200D\U0001F4BBB")), "grapheme deletion fixture inserts");
	expect(grapheme_delete.deleteBackward(grapheme_delete.revision()) && grapheme_delete.text() == QStringLiteral("Ae\u0301\U0001F469\u200D\U0001F4BB"), "backspace removes one ASCII grapheme");
	expect(grapheme_delete.deleteBackward(grapheme_delete.revision()) && grapheme_delete.text() == QStringLiteral("Ae\u0301"), "backspace removes one ZWJ emoji grapheme");
	expect(grapheme_delete.deleteBackward(grapheme_delete.revision()) && grapheme_delete.text() == QStringLiteral("A"), "backspace removes one combining-mark grapheme");
	MailboxRecord grapheme_start; grapheme_start.kind = DocumentEngine::MoveStart; grapheme_start.revision = grapheme_delete.revision();
	expect(grapheme_delete.apply(grapheme_start), "grapheme deletion cursor reaches start");
	expect(grapheme_delete.deleteForward(grapheme_delete.revision()) && grapheme_delete.text().isEmpty(), "forward delete removes one grapheme");
	MailboxRecord word_right; word_right.kind=DocumentEngine::MoveWordRight; word_right.revision=engine.revision();
	expect(engine.apply(word_right) && engine.cursor().position() > 1, "word-right movement succeeds");
	const auto word_revision = engine.revision();
	MailboxRecord word_left; word_left.kind=DocumentEngine::MoveWordLeft; word_left.revision=word_revision;
	expect(engine.apply(word_left) && engine.cursor().position() < engine.document()->characterCount(), "word-left movement succeeds");
	MailboxRecord replace; replace.kind=DocumentEngine::ReplaceAll; replace.revision=engine.revision(); replace.payload={'a', 0, 0xc3, 0xa9};
	expect(engine.apply(replace), "Unicode replace-all command succeeds");
	expect(!engine.text().contains("alpha") && engine.text().contains("é"), "replace-all updates authoritative text");
	DocumentEngine find_engine;
	expect(find_engine.insertText(0, QStringLiteral("one two one")), "find fixture inserts text");
	MailboxRecord find_next; find_next.kind = DocumentEngine::FindNext; find_next.revision = find_engine.revision(); find_next.payload = {'o','n','e'};
	expect(find_engine.apply(find_next) && find_engine.selectedText() == "one", "find-next selects the first match");
	find_next.revision = find_engine.revision();
	expect(find_engine.apply(find_next) && find_engine.cursor().selectionStart() == 8, "find-next wraps and advances to the next match");
	find_next.revision = find_engine.revision(); find_next.payload = {'n','o','t','-','f','o','u','n','d'};
	expect(!find_engine.apply(find_next) && !find_engine.cursor().hasSelection(), "find-next reports no match without leaving a stale selection");
	const QString before_invalid = engine.text();
	MailboxRecord invalid_utf8; invalid_utf8.kind = DocumentEngine::InsertText; invalid_utf8.revision = engine.revision(); invalid_utf8.payload = {0xc3, 0x28};
	expect(!engine.apply(invalid_utf8) && engine.text() == before_invalid, "invalid UTF-8 command is rejected without document mutation");
	MailboxRecord extend; extend.kind=DocumentEngine::ExtendRight; extend.revision=engine.revision();
	expect(engine.apply(extend) && engine.cursor().hasSelection(), "selection extension preserves anchor");
	extend.revision=engine.revision();
	expect(engine.apply(extend) && engine.cursor().selectedText().size() >= 2, "selection can extend repeatedly");
	const auto selection_revision = engine.revision();
	extend.kind=DocumentEngine::ExtendLeft; extend.revision=selection_revision;
	expect(engine.apply(extend) && engine.cursor().hasSelection(), "selection contracts with reverse extension");
	MailboxRecord down; down.kind=DocumentEngine::MoveDown; down.revision=engine.revision();
	expect(engine.apply(down), "downward line movement succeeds");
	MailboxRecord up; up.kind=DocumentEngine::MoveUp; up.revision=engine.revision();
	expect(engine.apply(up), "upward line movement succeeds");
	DocumentEngine selection_navigation;
	expect(selection_navigation.insertText(0, QStringLiteral("abcd\nefgh\nijkl")), "selection navigation fixture inserts");
	MailboxRecord extend_line_start; extend_line_start.kind=DocumentEngine::ExtendLineStart; extend_line_start.revision=selection_navigation.revision();
	expect(selection_navigation.apply(extend_line_start) && selection_navigation.cursor().hasSelection(), "Shift-Home extends selection to line start");
	MailboxRecord extend_up; extend_up.kind=DocumentEngine::ExtendUp; extend_up.revision=selection_navigation.revision();
	expect(selection_navigation.apply(extend_up) && selection_navigation.cursor().hasSelection(), "Shift-Up extends selection vertically");
	MailboxRecord extend_down; extend_down.kind=DocumentEngine::ExtendDown; extend_down.revision=selection_navigation.revision();
	expect(selection_navigation.apply(extend_down) && selection_navigation.cursor().hasSelection(), "Shift-Down extends selection vertically");
	QTextCursor line_end_anchor(selection_navigation.document());
	line_end_anchor.setPosition(0);
	selection_navigation.syncCursor(line_end_anchor);
	MailboxRecord extend_line_end; extend_line_end.kind=DocumentEngine::ExtendLineEnd; extend_line_end.revision=selection_navigation.revision();
	expect(selection_navigation.apply(extend_line_end) && selection_navigation.cursor().hasSelection(), "Shift-End extends selection to line end");
	MailboxRecord bold_selection; bold_selection.kind=DocumentEngine::ExtendRight; bold_selection.revision=engine.revision();
	expect(engine.apply(bold_selection) && engine.cursor().hasSelection(), "formatting selection is established");
	MailboxRecord bold; bold.kind=DocumentEngine::ToggleBold; bold.revision=engine.revision();
	expect(engine.apply(bold), "bold formatting command succeeds on selection");
	expect(engine.cursor().charFormat().fontWeight() == QFont::Bold, "bold formatting is preserved in document metadata");
	MailboxRecord italic; italic.kind=DocumentEngine::ToggleItalic; italic.revision=engine.revision();
	expect(engine.apply(italic) && engine.cursor().charFormat().fontItalic(), "italic formatting is preserved in document metadata");
	MailboxRecord underline; underline.kind=DocumentEngine::ToggleUnderline; underline.revision=engine.revision();
	expect(engine.apply(underline) && engine.cursor().charFormat().fontUnderline(), "underline formatting is preserved in document metadata");
	DocumentEngine proofing_engine;
	expect(proofing_engine.insertText(0, QStringLiteral("teh teh  corrected")), "proofing fixture inserts text");
	FairyWriter::ViewportSnapshot proofing_snapshot;
	expect(proofing_engine.makeViewport(256, proofing_snapshot), "proofing fixture exports a viewport snapshot");
	bool found_spelling_issue = false;
	bool found_grammar_issue = false;
	bool format_runs_sorted = true;
	for (const auto& run : proofing_snapshot.format_runs) {
		if ((run.flags & FairyWriter::FormatRunSpellingIssue) != 0) found_spelling_issue = true;
		if ((run.flags & FairyWriter::FormatRunGrammarIssue) != 0) found_grammar_issue = true;
	}
	for (std::size_t i = 1; i < proofing_snapshot.format_runs.size(); ++i) {
		const auto& previous = proofing_snapshot.format_runs[i - 1];
		const auto& current = proofing_snapshot.format_runs[i];
		if (current.offset < previous.offset
			|| (current.offset == previous.offset && current.length < previous.length)) {
			format_runs_sorted = false;
			break;
		}
	}
	expect(found_spelling_issue, "viewport format runs include spelling issue flags for typo heuristics");
	expect(found_grammar_issue, "viewport format runs include grammar issue flags for repeated words and spacing");
	expect(format_runs_sorted, "viewport format runs are emitted in deterministic offset order");
	MailboxRecord centered; centered.kind=DocumentEngine::AlignCenter; centered.revision=engine.revision();
	expect(engine.apply(centered) && (engine.cursor().blockFormat().alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignHCenter, "center alignment preserves block metadata");
	MailboxRecord indent; indent.kind=DocumentEngine::IndentIncrease; indent.revision=engine.revision();
	expect(engine.apply(indent) && engine.cursor().blockFormat().indent() == 1, "indent increase updates block metadata");
	indent.kind=DocumentEngine::IndentDecrease; indent.revision=engine.revision();
	expect(engine.apply(indent) && engine.cursor().blockFormat().indent() == 0, "indent decrease updates block metadata");
	MailboxRecord bullet; bullet.kind=DocumentEngine::ListBullet; bullet.revision=engine.revision();
	expect(engine.apply(bullet) && engine.cursor().currentList() != nullptr, "bullet list command creates list metadata");
	engine.setReadOnly(true);
	MailboxRecord blocked_insert; blocked_insert.kind=DocumentEngine::InsertText; blocked_insert.revision=engine.revision(); blocked_insert.payload={'x'};
	expect(!engine.apply(blocked_insert), "read-only engine rejects mutation");
	MailboxRecord blocked_undo; blocked_undo.kind=DocumentEngine::Undo; blocked_undo.revision=engine.revision();
	expect(!engine.apply(blocked_undo), "read-only engine rejects undo mutation");
	MailboxRecord blocked_redo; blocked_redo.kind=DocumentEngine::Redo; blocked_redo.revision=engine.revision();
	expect(!engine.apply(blocked_redo), "read-only engine rejects redo mutation");
	engine.setReadOnly(false);
	MailboxRecord paste; paste.kind=DocumentEngine::PasteText; paste.revision=engine.revision(); paste.payload={0xc3, 0xa9};
	expect(engine.apply(paste) && engine.text().contains(QStringLiteral("é")), "paste command inserts UTF-8 clipboard text");
	MailboxRecord line_start; line_start.kind=DocumentEngine::MoveLineStart; line_start.revision=engine.revision();
	expect(engine.apply(line_start), "line Home movement succeeds");
	MailboxRecord line_end; line_end.kind=DocumentEngine::MoveLineEnd; line_end.revision=engine.revision();
	expect(engine.apply(line_end), "line End movement succeeds");

	DocumentEngine large;
	QString body;
	for (int i = 0; i < 100000; ++i) body += QStringLiteral("word ");
	expect(large.insertText(0, body), "large document insertion succeeds");
	expect(large.document()->characterCount() == body.size() + 1, "large document remains untruncated");
	QTemporaryDir directory;
	const QString path = directory.filePath(QStringLiteral("story.txt"));
	expect(large.save(path), "atomic text save succeeds");
	DocumentEngine reopened;
	expect(reopened.load(path), "text load succeeds");
	expect(!reopened.isDirty(), "loaded document starts clean");
	expect(reopened.text() == large.text(), "saved text round trips without truncation");
	expect(reopened.insertText(reopened.revision(), QStringLiteral("!")) && reopened.isDirty(), "editing marks a loaded document dirty");
	expect(reopened.save() && !reopened.isDirty(), "successful save clears the dirty state");
	const QByteArray preserved_before_failure = [&] { QFile existing(path); return existing.open(QIODevice::ReadOnly) ? existing.readAll() : QByteArray(); }();
	expect(!reopened.save(directory.filePath(QStringLiteral("missing/subdirectory/target.txt"))), "atomic save rejects an invalid target without committing a partial file");
	QFile preserved_file(path);
	expect(preserved_file.open(QIODevice::ReadOnly) && preserved_file.readAll() == preserved_before_failure, "failed atomic save preserves the existing file exactly");
	for (const QString& suffix : {QStringLiteral("odt"), QStringLiteral("fodt"), QStringLiteral("docx"), QStringLiteral("rtf"), QStringLiteral("text")}) {
		const QString rich_path = directory.filePath(QStringLiteral("story.") + suffix);
		DocumentEngine rich;
		rich.insertText(0, QStringLiteral("Unicode élan — résumé\nsecond line"));
		QTextCursor rich_cursor(rich.document());
		rich_cursor.select(QTextCursor::Document);
	QTextCharFormat rich_format;
	rich_format.setFontWeight(QFont::Bold);
	rich_format.setFontItalic(true);
	rich_cursor.mergeCharFormat(rich_format);
		expect(rich.save(rich_path), "rich format save succeeds");
		DocumentEngine rich_reopened;
		expect(rich_reopened.load(rich_path), "rich format load succeeds");
		expect(rich_reopened.text() == rich.text(), "rich format text round trips");
		QTextCursor reopened_cursor(rich_reopened.document());
		reopened_cursor.select(QTextCursor::Document);
		if (suffix != QStringLiteral("text")) expect(reopened_cursor.charFormat().fontWeight() == QFont::Bold && reopened_cursor.charFormat().fontItalic(), "rich character metadata round trips");
	}
	// FairyWriter no longer links iconv. Plain text, ODT and DOCX are UTF-8 or
	// carry a declared XML encoding, so RTF is the only format that still needs
	// a legacy codepage, and CP1252 is the only one carried for it.
	//
	// codecForName checks FairyWriter's built-in table before Qt, so this
	// exercises the same deterministic implementation on every platform.
	{
		TextCodec* cp1252 = TextCodec::codecForName("CP1252");
		expect(cp1252 != nullptr, "cp1252 has a built-in table on every platform");
		if (cp1252) {
			// 0x92/0x97/0x85 are where CP1252 and Latin-1 disagree; 0xE9 is the
			// shared upper range that must still pass through unchanged.
			const QString decoded = cp1252->toUnicode(QByteArrayLiteral("don\x92t \x97 caf\xE9\x85"));
			expect(decoded == QString::fromUtf8("don’t — café…"),
				"built-in cp1252 table decodes curly quote, em dash, e-acute, and ellipsis");
			expect(cp1252->fromUnicode(decoded) == QByteArrayLiteral("don\x92t \x97 caf\xE9\x85"),
				"built-in cp1252 table round trips back to its original bytes");
		}
		// RtfReader's constructor calls setCodepage(1252) and then dereferences
		// m_codec with no null check, so a real RTF read proves the default
		// codepage still resolves at all once iconv is gone.
		const QString cp1252_path = directory.filePath(QStringLiteral("cp1252.rtf"));
		QFile cp1252_file(cp1252_path);
		expect(cp1252_file.open(QIODevice::WriteOnly), "cp1252 fixture opens");
		cp1252_file.write(QByteArrayLiteral(
			"{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0 Times;}}\n"
			"\\f0 don\\'92t \\'97 caf\\'e9\\'85\\par}\n"));
		cp1252_file.close();
		DocumentEngine cp1252_document;
		expect(cp1252_document.load(cp1252_path), "cp1252 rtf loads without iconv");
		expect(cp1252_document.text().startsWith(QString::fromUtf8("don’t — café…")),
			"rtf import decodes its declared cp1252 escapes");

		const QString fallback_path = directory.filePath(QStringLiteral("unknown-codepage.rtf"));
		QFile fallback_file(fallback_path);
		expect(fallback_file.open(QIODevice::WriteOnly), "unknown-codepage fixture opens");
		fallback_file.write(QByteArrayLiteral(
			"{\\rtf1\\ansi\\ansicpg99999\\deff0{\\fonttbl{\\f0 Times;}}\n"
			"\\f0 readable ASCII and caf\\'e9\\par}\n"));
		fallback_file.close();
		DocumentEngine fallback_document;
		expect(fallback_document.load(fallback_path),
			"unknown rtf codepage falls back instead of failing");
		expect(fallback_document.text().startsWith(QString::fromUtf8("readable ASCII and café")),
			"unknown rtf codepage uses the deterministic Latin-1 fallback");
	}
	QString long_document_text = QStringLiteral("FairyWriter Journey\n\n");
	const QString long_document_paragraph = QStringLiteral(
		"The writer walked beyond the workshop, carrying a small lamp and the "
		"unfinished sentence waiting to return. The road folded through rain, "
		"cedar, and quiet rooms where every word waited for its proper place. ");
	while (long_document_text.toUtf8().size() < 38000) {
		long_document_text += long_document_paragraph;
	}
	DocumentEngine long_document;
	expect(long_document.insertText(0, long_document_text),
		"generated 38KB document fixture inserts");
	QTextCursor long_document_title(long_document.document());
	long_document_title.setPosition(0);
	long_document_title.setPosition(
		QStringLiteral("FairyWriter Journey").size(), QTextCursor::KeepAnchor);
	long_document_title.mergeCharFormat([] {
		QTextCharFormat format;
		format.setFontWeight(QFont::Bold);
		format.setFontPointSize(18);
		return format;
	}());
	const QString long_document_path =
		directory.filePath(QStringLiteral("long-document.odt"));
	expect(long_document.save(long_document_path),
		"long document fixture saves as ODT");
	DocumentEngine long_document_reopened;
	expect(long_document_reopened.load(long_document_path)
			&& long_document_reopened.text() == long_document.text(),
		"long document fixture text round trips");
	QTextCursor long_document_reopened_title(long_document_reopened.document());
	long_document_reopened_title.setPosition(0);
	long_document_reopened_title.setPosition(
		QStringLiteral("FairyWriter Journey").size(), QTextCursor::KeepAnchor);
	expect(long_document_reopened_title.charFormat().fontWeight() == QFont::Bold,
		"long document fixture title formatting round trips");
	for (int boundary : {128, 4096, 12000, 24000}) {
		long_document.syncCursor(QTextCursor(long_document.document()));
		QTextCursor at_boundary(long_document.document());
		at_boundary.setPosition(qMin(
			boundary, long_document.document()->characterCount() - 1));
		long_document.syncCursor(at_boundary);
		FairyWriter::ViewportSnapshot boundary_view;
		expect(long_document.makeViewport(128, boundary_view)
				&& boundary_view.text_offset
					<= static_cast<std::uint32_t>(boundary),
			"long document fixture viewport reaches multiple boundaries");
		MailboxRecord boundary_insert;
		boundary_insert.kind = DocumentEngine::InsertText;
		boundary_insert.revision = long_document.revision();
		boundary_insert.payload = {'[', 'x', ']'};
		expect(long_document.apply(boundary_insert),
			"long document fixture boundary edit succeeds");
		const auto after_boundary_edit = long_document.revision();
		expect(long_document.undo(after_boundary_edit)
				&& long_document.redo(long_document.revision()),
			"long document fixture boundary edit undo/redo succeeds");
	}
	const QString long_document_final_path =
		directory.filePath(QStringLiteral("long-document-final.odt"));
	expect(long_document.save(long_document_final_path),
		"long document fixture final save succeeds");
	DocumentEngine long_document_final;
	expect(long_document_final.load(long_document_final_path)
			&& long_document_final.text() == long_document.text(),
		"long document fixture final reopen preserves text");
	DocumentEngine loaded;
	expect(loaded.load(path), "conflict fixture loads");
	QFile changed(path);
	expect(changed.open(QIODevice::Append), "conflict fixture can be modified");
	changed.write("external");
	changed.close();
	expect(!loaded.save(), "external modification blocks overwrite");
	expect(loaded.text() == reopened.text(), "conflict preserves in-memory document");
	const QString malformed = directory.filePath(QStringLiteral("malformed.odt"));
	QFile bad(malformed); expect(bad.open(QIODevice::WriteOnly), "malformed fixture opens"); bad.write("not an odt"); bad.close();
	DocumentEngine unchanged; unchanged.insertText(0, QStringLiteral("keep"));
	expect(!unchanged.load(malformed) && unchanged.text() == "keep", "malformed load preserves existing document");
	const QString readonly_path = directory.filePath(QStringLiteral("readonly.txt"));
	DocumentEngine readonly_source; readonly_source.insertText(0, QStringLiteral("locked"));
	expect(readonly_source.save(readonly_path), "read-only fixture saves initially");
	QFile::setPermissions(readonly_path, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
	DocumentEngine readonly;
	expect(readonly.load(readonly_path) && readonly.isReadOnly(), "non-writable file loads read-only");
	expect(!readonly.save(), "read-only file rejects save");
	FairyWriter::FileCatalog readonly_catalog(directory.path());
	const QString readonly_id = readonly_catalog.registerPath(readonly_path);
	FairyWriter::DocumentBridge readonly_bridge;
	expect(readonly_bridge.openFile(readonly_catalog, readonly_id), "bridge loads read-only fixture");
	MailboxRecord settings_event;
	expect(readonly_bridge.events().pop(settings_event)
			&& settings_event.kind
				== FairyWriter::DocumentBridge::EventPersistenceSettings,
		"open publishes current persistence and format settings");
	MailboxRecord readonly_save; readonly_save.kind = DocumentEngine::Save; readonly_save.revision = readonly_bridge.engine().revision();
	expect(readonly_bridge.submit(readonly_save) && readonly_bridge.pump(), "bridge processes read-only save command");
	MailboxRecord readonly_event;
	expect(readonly_bridge.events().pop(readonly_event) && readonly_event.kind == FairyWriter::DocumentBridge::EventReadOnly, "bridge distinguishes read-only save failure from external conflict");
	DocumentEngine save_command;
	const QString command_path = directory.filePath(QStringLiteral("command.txt"));
	expect(save_command.insertText(0, QStringLiteral("saved")) && save_command.save(command_path), "save command fixture initializes");
	MailboxRecord save; save.kind=DocumentEngine::Save; save.revision=save_command.revision();
	expect(save_command.apply(save), "mailbox save command succeeds");
	MailboxRecord new_document; new_document.kind=DocumentEngine::NewDocument; new_document.revision=save_command.revision();
	expect(save_command.apply(new_document) && save_command.text().isEmpty() && save_command.filename().isEmpty() && save_command.format() == "odt" && !save_command.isReadOnly(), "new document resets engine state");
	QTemporaryDir bridge_files;
	QFile bridge_file(bridge_files.filePath(QStringLiteral("open.txt")));
	expect(bridge_file.open(QIODevice::WriteOnly), "opaque open fixture opens"); bridge_file.write("opened"); bridge_file.close();
	FairyWriter::FileCatalog bridge_catalog(bridge_files.path());
	const auto open_entries = bridge_catalog.list();
	const auto open_id = open_entries.front().id;
	FairyWriter::DocumentBridge open_bridge;
	expect(open_bridge.openFile(bridge_catalog, open_id) && open_bridge.engine().text() == "opened", "bridge opens catalog entry by opaque ID");
	expect(open_bridge.events().pop(settings_event)
			&& settings_event.kind
				== FairyWriter::DocumentBridge::EventPersistenceSettings,
		"open refreshes the cartridge format default");
	expect(open_bridge.listRecentFiles(bridge_catalog), "bridge lists recently opened opaque files");
	MailboxRecord recent_event;
	expect(open_bridge.events().pop(recent_event) && recent_event.kind == FairyWriter::DocumentBridge::EventFileEntry, "recent file listing emits standard file metadata events");
	MailboxRecord recent_complete;
	expect(open_bridge.events().pop(recent_complete)
		&& recent_complete.kind == FairyWriter::DocumentBridge::EventFileListComplete
		&& recent_complete.payload.size() == 11
		&& recent_complete.payload[8] == 1
		&& recent_complete.payload[9] == 3
		&& recent_complete.payload[10] == 0,
		"recent file page ends with an explicit bounded-list completion event");
	expect(!open_bridge.openFile(bridge_catalog, QStringLiteral("/etc/passwd")), "bridge rejects host path masquerading as ID");
	MailboxRecord rejected_open;
	expect(open_bridge.events().pop(rejected_open)
			&& rejected_open.kind == FairyWriter::DocumentBridge::EventOpenFailed,
		"an unresolvable open ID reaches the cartridge as a visible failure");
	expect(open_bridge.listFiles(bridge_catalog), "bridge lists catalog entries");
	MailboxRecord file_event;
	expect(open_bridge.events().pop(file_event) && file_event.kind == FairyWriter::DocumentBridge::EventFileEntry && file_event.payload.size() >= 20, "file listing emits metadata event");
	if (file_event.kind == FairyWriter::DocumentBridge::EventFileEntry && file_event.payload.size() >= 20) {
		const auto id_bytes = file_event.payload[0];
		const auto name_bytes = static_cast<std::uint16_t>(file_event.payload[1] | (file_event.payload[2] << 8));
		expect(file_event.payload.size() == 20 + id_bytes + name_bytes && id_bytes != 0 && name_bytes != 0 && (file_event.payload[3] & 1) == 0 && (file_event.payload[3] & 2) != 0, "file event contains opaque ID, name, writable flag, and metadata");
	}
	MailboxRecord file_complete;
	expect(open_bridge.events().pop(file_complete)
		&& file_complete.kind == FairyWriter::DocumentBridge::EventFileListComplete
		&& file_complete.payload.size() == 11
		&& file_complete.payload[8] == 1
		&& file_complete.payload[9] == 1,
		"directory page terminates explicitly without relying on event-ring exhaustion");
	const QString save_as_path = bridge_files.filePath(QStringLiteral("save-as.txt"));
	QFile save_as_fixture(save_as_path);
	expect(save_as_fixture.open(QIODevice::WriteOnly), "save-as fixture opens"); save_as_fixture.close();
	const QString save_as_id = bridge_catalog.registerPath(save_as_path);
	expect(!save_as_id.isEmpty() && open_bridge.engine().insertText(open_bridge.engine().revision(), QStringLiteral("!")), "save-as source mutates");
	expect(open_bridge.saveAs(bridge_catalog, save_as_id), "bridge requests overwrite confirmation through opaque catalog ID");
	MailboxRecord overwrite_event;
	const QByteArray save_as_id_bytes = save_as_id.toUtf8();
	const std::vector<std::uint8_t> expected_overwrite_id(reinterpret_cast<const std::uint8_t*>(save_as_id_bytes.constData()), reinterpret_cast<const std::uint8_t*>(save_as_id_bytes.constData() + save_as_id_bytes.size()));
	expect(open_bridge.events().pop(overwrite_event) && overwrite_event.kind == FairyWriter::DocumentBridge::EventOverwriteRequired && overwrite_event.payload == expected_overwrite_id, "overwrite request carries only the opaque ID");
	expect(open_bridge.saveAs(bridge_catalog, save_as_id, true), "confirmed bridge save overwrites through opaque catalog ID");
	expect(open_bridge.events().pop(settings_event)
			&& settings_event.kind
				== FairyWriter::DocumentBridge::EventPersistenceSettings,
		"Save As refreshes the current format setting");
	expect(!bridge_catalog.recentFiles().isEmpty()
			&& bridge_catalog.recentFiles().front().absolutePath
				== QFileInfo(save_as_path).canonicalFilePath(),
		"successful Save As records the durable target in Recent Files");
	QFile saved_as(save_as_path);
	const QByteArray saved_as_bytes = saved_as.open(QIODevice::ReadOnly) ? saved_as.readAll() : QByteArray();
	expect(saved_as_bytes == "!opened", "save-as commits target atomically");
	FairyWriter::DocumentBridge conflict_bridge;
	expect(conflict_bridge.openFile(bridge_catalog, save_as_id), "bridge loads conflict fixture");
	expect(conflict_bridge.events().pop(settings_event)
			&& settings_event.kind
				== FairyWriter::DocumentBridge::EventPersistenceSettings,
		"conflict fixture consumes its open-format event");
	QThread::msleep(10);
	QFile conflict_change(save_as_path);
	expect(conflict_change.open(QIODevice::Append), "bridge conflict fixture opens for external mutation");
	conflict_change.write("changed"); conflict_change.close();
	MailboxRecord bridge_save_command; bridge_save_command.kind = DocumentEngine::Save; bridge_save_command.revision = conflict_bridge.engine().revision();
	expect(conflict_bridge.submit(bridge_save_command) && conflict_bridge.pump(), "bridge processes failed save command");
	MailboxRecord save_conflict;
	expect(conflict_bridge.events().pop(save_conflict) && save_conflict.kind == FairyWriter::DocumentBridge::EventSaveConflict, "bridge exposes external save conflict to the cartridge");
	expect(open_bridge.saveAsNew(bridge_catalog, QString(), QStringLiteral("new-save.txt")), "bridge creates and saves a new opaque filename");
	MailboxRecord new_save_event;
	expect(open_bridge.events().pop(new_save_event) && new_save_event.kind == FairyWriter::DocumentBridge::EventFileEntry, "new save-as publishes the created file entry");
	expect(open_bridge.events().pop(settings_event)
			&& settings_event.kind
				== FairyWriter::DocumentBridge::EventPersistenceSettings,
		"new Save As refreshes the current format setting");
	expect(!bridge_catalog.recentFiles().isEmpty()
			&& bridge_catalog.recentFiles().front().absolutePath
				== QFileInfo(bridge_files.filePath(QStringLiteral("new-save.txt")))
					.canonicalFilePath(),
		"successful Save As New records the created target in Recent Files");
	QFile new_saved(bridge_files.filePath(QStringLiteral("new-save.txt")));
	expect(new_saved.open(QIODevice::ReadOnly) && new_saved.readAll() == "!opened", "new save-as target contains the document");
	expect(open_bridge.saveAsNew(bridge_catalog, QStringLiteral("missing-parent"),
			QStringLiteral("lost-save.txt")),
		"invalid Save As New request is consumed as a visible cartridge outcome");
	MailboxRecord invalid_save_event;
	expect(open_bridge.events().pop(invalid_save_event)
			&& invalid_save_event.kind
				== FairyWriter::DocumentBridge::EventPersistenceFailed,
		"invalid Save As New emits a visible persistence failure instead of disappearing");
	const QString recovery_path = bridge_files.filePath(QStringLiteral("recovery.txt"));
	const QString recovery_filename_before = open_bridge.engine().filename();
	const std::uint64_t recovery_revision_before = open_bridge.engine().revision();
	expect(open_bridge.engine().writeRecovery(recovery_path), "recovery writes atomically without changing document ownership");
	expect(open_bridge.engine().filename() == recovery_filename_before && open_bridge.engine().revision() == recovery_revision_before, "recovery write preserves filename and revision");
	DocumentEngine recovered;
	expect(recovered.recover(recovery_path, save_as_path) && recovered.text() == "!opened" && recovered.filename() == save_as_path, "recovery restores text with the original filename");
	expect(open_bridge.publishRecoveryAvailable(QStringLiteral("txt")), "bridge publishes opaque recovery availability");
	MailboxRecord recovery_event;
	expect(open_bridge.events().pop(recovery_event) && recovery_event.kind == FairyWriter::DocumentBridge::EventRecoveryAvailable && recovery_event.payload.size() == 12, "recovery availability carries token and format without a host path");
	const QString created_id = open_bridge.engine().revision() >= 0 ? bridge_catalog.createDirectory(QString(), QStringLiteral("Created")) : QString();
	expect(!created_id.isEmpty() && bridge_catalog.entry(created_id)->directory, "catalog creates a safe directory for the cartridge workflow");
	expect(open_bridge.createDirectory(bridge_catalog, QString(), QStringLiteral("Created-Through-Bridge")), "bridge publishes directory creation");
	MailboxRecord directory_event;
	expect(open_bridge.events().pop(directory_event) && directory_event.kind == FairyWriter::DocumentBridge::EventDirectoryCreated, "directory creation emits an opaque event");
	FairyWriter::DocumentBridge bridge;
	MailboxRecord bridge_insert; bridge_insert.kind=DocumentEngine::InsertText; bridge_insert.payload={'o','k'};
	expect(bridge.submit(bridge_insert) && bridge.pump() && bridge.engine().text() == "ok", "bridge applies command and publishes viewport");
	const auto stale_revision = bridge.engine().revision() - 1;
	bridge_insert.revision = stale_revision; bridge_insert.payload={'!'};
	expect(bridge.submit(bridge_insert) && bridge.pump() && bridge.engine().text() == "ok", "bridge rejects stale command and republishes current viewport");
	MailboxRecord conflict;
	expect(bridge.events().pop(conflict) && conflict.kind == 0x8001 && conflict.revision == bridge.engine().revision(), "stale command emits current-revision conflict event");
	MailboxRecord copy; copy.kind=DocumentEngine::CopySelection; copy.revision=bridge.engine().revision();
	MailboxRecord select_copy; select_copy.kind=DocumentEngine::ExtendLeft; select_copy.revision=bridge.engine().revision();
	expect(bridge.submit(select_copy) && bridge.pump(), "copy selection command is accepted");
	copy.revision = bridge.engine().revision();
	expect(bridge.submit(copy) && bridge.pump(), "copy command is accepted");
	MailboxRecord copied;
	expect(bridge.events().pop(copied) && copied.kind == 0x8100 && copied.payload == std::vector<std::uint8_t>({'k'}), "copy emits selected clipboard text without mutation");
	expect(bridge.publishStatistics(), "statistics request publishes an event");
	MailboxRecord statistics_event;
	expect(bridge.events().pop(statistics_event) && statistics_event.kind == FairyWriter::DocumentBridge::EventStatistics && statistics_event.payload.size() == 12, "statistics event has a typed fixed payload");
	MailboxRecord goal_command; goal_command.kind = FairyWriter::DocumentBridge::CommandSetWordGoal; goal_command.revision = bridge.engine().revision(); goal_command.payload = {3, 0, 0, 0};
	expect(bridge.submit(goal_command) && bridge.pump(), "word goal command is accepted at the current document revision");
	MailboxRecord goal_event;
	expect(bridge.events().pop(goal_event) && goal_event.kind == FairyWriter::DocumentBridge::EventGoalProgress && goal_event.payload.size() == 16, "goal progress publishes a fixed typed payload");
	if (goal_event.payload.size() == 16) {
		const auto read32 = [&goal_event](std::size_t offset) { return static_cast<std::uint32_t>(goal_event.payload[offset]) | (static_cast<std::uint32_t>(goal_event.payload[offset + 1]) << 8) | (static_cast<std::uint32_t>(goal_event.payload[offset + 2]) << 16) | (static_cast<std::uint32_t>(goal_event.payload[offset + 3]) << 24); };
		expect(read32(0) == 1 && read32(4) == 3 && read32(8) == 33 && read32(12) == 2, "goal event reports words, goal, capped percent, and characters");
	}
	FairyWriter::DocumentBridge find_bridge;
	MailboxRecord find_insert; find_insert.kind = DocumentEngine::InsertText; find_insert.payload = {'o','n','e',' ','t','w','o'};
	expect(find_bridge.submit(find_insert) && find_bridge.pump(), "bridge find fixture inserts authoritative text");
	MailboxRecord find_command; find_command.kind = DocumentEngine::FindNext; find_command.revision = find_bridge.engine().revision(); find_command.payload = {'t','w','o'};
	expect(find_bridge.submit(find_command) && find_bridge.pump(), "bridge find command is accepted");
	MailboxRecord find_event;
	expect(find_bridge.events().pop(find_event) && find_event.kind == FairyWriter::DocumentBridge::EventFindResult && find_event.payload.size() == 9 && find_event.payload[0] == 1, "bridge publishes a typed find match result");
	if (find_event.payload.size() == 9) {
		const auto read32 = [&find_event](std::size_t offset) { return static_cast<std::uint32_t>(find_event.payload[offset]) | (static_cast<std::uint32_t>(find_event.payload[offset + 1]) << 8) | (static_cast<std::uint32_t>(find_event.payload[offset + 2]) << 16) | (static_cast<std::uint32_t>(find_event.payload[offset + 3]) << 24); };
		expect(read32(1) == 4 && read32(5) == 7, "find result carries UTF-16 selection bounds");
	}
	find_command.revision = find_bridge.engine().revision(); find_command.payload = {'n','o','p','e'};
	expect(find_bridge.submit(find_command) && find_bridge.pump(), "bridge no-match search is accepted");
	expect(find_bridge.events().pop(find_event) && find_event.kind == FairyWriter::DocumentBridge::EventFindResult && find_event.payload[0] == 0, "bridge publishes a typed no-match result");
	MailboxRecord paste_bridge; paste_bridge.kind = DocumentEngine::PasteText; paste_bridge.revision = bridge.engine().revision(); paste_bridge.payload = {'x', 'y'};
	expect(bridge.submit(paste_bridge) && bridge.pump() && bridge.engine().text() == "oxy", "paste command replaces the selected text through the bridge");
	// A real passage of prose is tens of kilobytes. The bridge's command ring
	// used to be sized at the 8 KiB SRAM command region, so anything past 8171
	// bytes was refused by the transport and the paste silently did nothing.
	FairyWriter::DocumentBridge wide_paste_bridge;
	MailboxRecord wide_paste; wide_paste.kind = DocumentEngine::PasteText; wide_paste.revision = 0;
	wide_paste.payload.resize(FairyWriter::MailboxRing::MaxRecordPayload);
	for (std::size_t i = 0; i < wide_paste.payload.size(); ++i) wide_paste.payload[i] = static_cast<std::uint8_t>('a' + (i % 26));
	expect(wide_paste_bridge.submit(wide_paste) && wide_paste_bridge.pump()
		&& wide_paste_bridge.engine().text().size() == static_cast<qsizetype>(FairyWriter::MailboxRing::MaxRecordPayload),
		"a paste of the widest record the wire can describe reaches the document");
	expect(wide_paste_bridge.engine().revision() == 1,
		"a wide paste commits exactly once, so it undoes in a single step");
	FairyWriter::DocumentBridge burst_bridge;
	MailboxRecord burst; burst.kind = DocumentEngine::InsertText; burst.revision = 0; burst.payload.resize(1000);
	for (std::size_t i = 0; i < burst.payload.size(); ++i) burst.payload[i] = static_cast<std::uint8_t>('a' + (i % 26));
	expect(burst_bridge.submit(burst) && burst_bridge.pump() && burst_bridge.engine().text().size() == 1000, "1000-character mailbox burst reaches the authoritative document");
	expect(burst_bridge.engine().revision() == 1 && burst_bridge.engine().text().at(999) == QChar('l'), "burst preserves order and commits exactly once");
	const std::uint32_t initial_viewport_start = read32(burst_bridge.viewports().active(), 20);
	bool stable_inside_guard = true;
	for (int i = 0; i < 10; ++i) {
		MailboxRecord left;
		left.kind = DocumentEngine::MoveLeft;
		left.revision = burst_bridge.engine().revision();
		stable_inside_guard = stable_inside_guard
			&& burst_bridge.submit(left)
			&& burst_bridge.pump()
			&& read32(burst_bridge.viewports().active(), 20) == initial_viewport_start;
	}
	expect(stable_inside_guard,
		"bridge-owned viewport remains anchored during cursor movement inside its edge guard");
	std::uint32_t previous_viewport_start = initial_viewport_start;
	int viewport_reanchors = 0;
	bool cursor_always_visible = true;
	for (int i = 0; i < 600; ++i) {
		MailboxRecord left;
		left.kind = DocumentEngine::MoveLeft;
		left.revision = burst_bridge.engine().revision();
		if (!burst_bridge.submit(left) || !burst_bridge.pump()) {
			cursor_always_visible = false;
			break;
		}
		const auto& active = burst_bridge.viewports().active();
		const std::uint32_t start_offset = read32(active, 20);
		const std::uint32_t text_bytes = read16(active, 32);
		const std::uint32_t cursor = read32(active, 8);
		cursor_always_visible = cursor_always_visible
			&& cursor >= start_offset
			&& cursor <= start_offset + text_bytes;
		if (start_offset != previous_viewport_start) {
			++viewport_reanchors;
			previous_viewport_start = start_offset;
		}
	}
	expect(cursor_always_visible && viewport_reanchors > 0 && viewport_reanchors < 8,
		"long-document cursor remains visible while the viewport moves in stable page-sized chunks");
	if (argc == 2) {
		const QString public_fixture_path = QString::fromLocal8Bit(argv[1]);
		QTemporaryDir public_fixture_workspace;
		const QString public_fixture_editable_path =
			public_fixture_workspace.filePath(
				QStringLiteral("FairyWriter Public Regression.odt"));
		const bool public_fixture_copied = public_fixture_workspace.isValid()
			&& QFile::copy(public_fixture_path, public_fixture_editable_path)
			&& QFile::setPermissions(public_fixture_editable_path,
				QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		expect(public_fixture_copied,
			"public ODT fixture copies into a writable test workspace");
		DocumentEngine public_fixture;
		expect(public_fixture.load(
				public_fixture_editable_path, QStringLiteral("odt")),
			"public ODT fixture loads through the production reader");
		const QString public_fixture_original = public_fixture.text();
		const auto public_fixture_stats = public_fixture.statistics();
		expect(public_fixture_original.toUtf8().size() >= 38000
				&& public_fixture_stats.words >= 6500,
			"public ODT fixture exposes a substantial document and thousands of words");
		expect(public_fixture.inferredChapterCount() == 3,
			"public ODT fixture exposes three plain chapter labels as a structure suggestion");
		const int chapter_two =
			public_fixture_original.indexOf(QStringLiteral("Chapter II"));
		QTextCursor public_fixture_cursor(public_fixture.document());
		public_fixture_cursor.setPosition(chapter_two);
		public_fixture.syncCursor(public_fixture_cursor);
		FairyWriter::ViewportSnapshot public_fixture_viewport;
		const bool public_fixture_viewport_made = public_fixture.makeViewport(
			FairyWriter::MailboxLayout::CartridgeWorkingBytes,
			public_fixture_viewport);
		const std::string public_fixture_title(
			public_fixture_viewport.display_title.begin(),
			public_fixture_viewport.display_title.end());
		expect(chapter_two > 0 && public_fixture_viewport_made
			&& public_fixture_viewport.chapter == 2
			&& public_fixture_viewport.word_count == public_fixture_stats.words
			&& public_fixture_title.rfind(
				"FAIRYWRITER PUBLIC REGRESSION", 0) == 0,
			"public fixture viewport reports its filename, second chapter, and full word count");
		expect(public_fixture.promoteInferredChapterHeadings(
				public_fixture.revision())
			&& public_fixture.inferredChapterCount() == 0,
			"public fixture chapter labels convert to real heading metadata after acceptance");
		for (int boundary : {128, 4096, 12000, 24000,
			qMax(0, static_cast<int>(public_fixture_original.size()) - 64)}) {
			QTextCursor at_boundary(public_fixture.document());
			at_boundary.setPosition(qMin(
				boundary, public_fixture.document()->characterCount() - 1));
			public_fixture.syncCursor(at_boundary);
			expect(public_fixture.insertText(
					public_fixture.revision(), QStringLiteral("[fixture]")),
				"public ODT fixture accepts edits at a real viewport boundary");
			expect(public_fixture.undo(public_fixture.revision())
					&& public_fixture.redo(public_fixture.revision()),
				"public fixture boundary edit survives undo and redo");
		}
		QTemporaryDir public_fixture_roundtrip;
		const QString public_fixture_copy =
			public_fixture_roundtrip.filePath(QStringLiteral("public-roundtrip.odt"));
		expect(public_fixture.save(public_fixture_copy),
			"public fixture saves atomically to a temporary ODT");
		DocumentEngine public_fixture_reopened;
		expect(public_fixture_reopened.load(
				public_fixture_copy, QStringLiteral("odt"))
			&& public_fixture_reopened.text() == public_fixture.text()
			&& public_fixture_reopened.inferredChapterCount() == 0,
			"public fixture reopens with every boundary edit and promoted heading intact");
		std::cout << "Public ODT fixture: "
			<< public_fixture_original.toUtf8().size() << " UTF-8 bytes, "
			<< public_fixture_stats.words << " words, chapter "
			<< public_fixture_viewport.chapter << " at Chapter II.\n";
	}
	{
		// Viewports carry authoritative dirty/read-only flags and the line count
		// so the cartridge can render document status without native chrome.
		DocumentEngine status_engine;
		expect(status_engine.insertText(0, QStringLiteral("one\ntwo")), "status fixture inserts");
		FairyWriter::ViewportSnapshot status_view;
		expect(status_engine.makeViewport(64, status_view)
			&& (status_view.status_flags & 1) != 0
			&& status_view.line_count == 2
			&& status_view.grapheme_offsets.size() == 7
			&& status_view.line_break_offsets.size() == 1
			&& status_view.line_break_offsets[0] == 3
			&& status_view.bytes_before == 0
			&& status_view.bytes_after == 0
			&& (status_view.status_flags & 4) != 0
			&& (status_view.status_flags & 8) != 0,
			"dirty document publishes status flags, line count, grapheme offsets, and line breaks");
		QTemporaryDir status_dir;
		expect(status_engine.save(status_dir.filePath(QStringLiteral("status.txt"))), "status fixture saves");
		expect(status_engine.makeViewport(64, status_view) && (status_view.status_flags & 1) == 0,
			"saved document clears the modified flag");
		status_engine.setReadOnly(true);
		expect(status_engine.makeViewport(64, status_view) && (status_view.status_flags & 2) != 0,
			"read-only document publishes the read-only flag");
	}
	{
		// A cartridge pointer click resolves to a viewport-relative UTF-16 offset;
		// the bridge adds the published viewport start and the host owns the
		// resulting caret. Prove both the in-window case and end clamping, and
		// that a stale-revision click never moves the authoritative cursor.
		FairyWriter::DocumentBridge pointer_bridge;
		expect(pointer_bridge.engine().insertText(0, QStringLiteral("hello world")), "pointer fixture inserts text");
		const auto pointer_revision = pointer_bridge.engine().revision();
		MailboxRecord click; click.kind = FairyWriter::DocumentBridge::CommandPointerSetCursor;
		click.revision = pointer_revision; click.payload = {6, 0}; // offset 6 -> 'w'
		expect(pointer_bridge.submit(click) && pointer_bridge.pump()
			&& pointer_bridge.engine().cursor().position() == 6
			&& !pointer_bridge.engine().cursor().hasSelection(),
			"pointer click places the collapsed caret at the viewport-relative offset");
		MailboxRecord past_end; past_end.kind = FairyWriter::DocumentBridge::CommandPointerSetCursor;
		past_end.revision = pointer_bridge.engine().revision(); past_end.payload = {200, 0};
		expect(pointer_bridge.submit(past_end) && pointer_bridge.pump()
			&& pointer_bridge.engine().cursor().position() == static_cast<int>(QStringLiteral("hello world").size()),
			"pointer click past the document end clamps to the last position");
		const auto settled = pointer_bridge.engine().cursor().position();
		MailboxRecord stale_click; stale_click.kind = FairyWriter::DocumentBridge::CommandPointerSetCursor;
		stale_click.revision = pointer_bridge.engine().revision() - 1; stale_click.payload = {0, 0};
		expect(pointer_bridge.submit(stale_click) && pointer_bridge.pump()
			&& pointer_bridge.engine().cursor().position() == settled,
			"a stale-revision pointer click is rejected without moving the caret");
		// A drag is a press (SetCursor, anchor) followed by extend commands that
		// move the active end while keeping the anchor. Press at offset 2, drag to
		// offset 8: the selection must be exactly [2,8) with the anchor preserved.
		MailboxRecord anchor; anchor.kind = FairyWriter::DocumentBridge::CommandPointerSetCursor;
		anchor.revision = pointer_bridge.engine().revision(); anchor.payload = {2, 0};
		expect(pointer_bridge.submit(anchor) && pointer_bridge.pump()
			&& pointer_bridge.engine().cursor().position() == 2
			&& !pointer_bridge.engine().cursor().hasSelection(),
			"drag press collapses the caret to the anchor cell");
		MailboxRecord drag; drag.kind = FairyWriter::DocumentBridge::CommandPointerExtendCursor;
		drag.revision = pointer_bridge.engine().revision(); drag.payload = {8, 0};
		expect(pointer_bridge.submit(drag) && pointer_bridge.pump()
			&& pointer_bridge.engine().cursor().selectionStart() == 2
			&& pointer_bridge.engine().cursor().selectionEnd() == 8
			&& pointer_bridge.engine().cursor().anchor() == 2,
			"drag extend selects from the anchor to the dragged cell");
		MailboxRecord drag_back; drag_back.kind = FairyWriter::DocumentBridge::CommandPointerExtendCursor;
		drag_back.revision = pointer_bridge.engine().revision(); drag_back.payload = {4, 0};
		expect(pointer_bridge.submit(drag_back) && pointer_bridge.pump()
			&& pointer_bridge.engine().cursor().selectionStart() == 2
			&& pointer_bridge.engine().cursor().selectionEnd() == 4
			&& pointer_bridge.engine().cursor().anchor() == 2,
			"dragging back toward the anchor shrinks the selection without moving the anchor");
	}
	{
		// Scrollbar drag scrolls the view only. The window anchors where the thumb
		// points even though the caret is nowhere near it, the caret itself does
		// not move, and the snapshot flags that the caret is off-window so the
		// cartridge can render no caret rather than parking one at the end of the
		// visible text. Placing the caret afterwards ends the scroll.
		FairyWriter::DocumentBridge scroll_bridge;
		QString long_text;
		for (int i = 0; i < 60; ++i) long_text += QStringLiteral("the quick brown fox jumps. ");
		expect(scroll_bridge.engine().insertText(0, long_text), "scroll fixture inserts a long document");
		expect(scroll_bridge.engine().moveCursor(scroll_bridge.engine().revision(), QTextCursor::Start),
			"scroll fixture parks the caret at the start");
		const auto caret_before = scroll_bridge.engine().cursor().position();
		FairyWriter::ViewportSnapshot at_start;
		expect(scroll_bridge.engine().makeViewport(510, at_start) && at_start.text_offset == 0
			&& (at_start.status_flags & 16) == 0,
			"an unscrolled window contains the caret and does not set the off-window flag");

		MailboxRecord scroll; scroll.kind = FairyWriter::DocumentBridge::CommandScrollToFraction;
		scroll.revision = scroll_bridge.engine().revision();
		// Halfway along the track.
		const std::uint32_t half = FairyWriter::DocumentBridge::CommandScrollTrackTravel / 2;
		scroll.payload = {static_cast<std::uint8_t>(half & 0xff), static_cast<std::uint8_t>(half >> 8)};
		expect(scroll_bridge.submit(scroll) && scroll_bridge.pump(),
			"the bridge accepts a scrollbar drag");
		expect(scroll_bridge.engine().cursor().position() == caret_before,
			"scrolling the view does not move the caret");
		const auto& scrolled = scroll_bridge.viewports().active();
		const std::uint32_t scrolled_offset = static_cast<std::uint32_t>(scrolled[20])
			| (static_cast<std::uint32_t>(scrolled[21]) << 8)
			| (static_cast<std::uint32_t>(scrolled[22]) << 16)
			| (static_cast<std::uint32_t>(scrolled[23]) << 24);
		expect(scrolled_offset > 0, "the scrolled window starts away from the document start");
		expect((scrolled[91] & 16) != 0,
			"a window scrolled off the caret flags the caret as out of view");

		// Clicking in the scrolled view places the caret there and ends the scroll,
		// so the window follows the caret again instead of staying pinned.
		MailboxRecord place; place.kind = FairyWriter::DocumentBridge::CommandPointerSetCursor;
		place.revision = scroll_bridge.engine().revision(); place.payload = {5, 0};
		expect(scroll_bridge.submit(place) && scroll_bridge.pump()
			&& scroll_bridge.engine().cursor().position() == static_cast<int>(scrolled_offset + 5),
			"clicking in the scrolled view places the caret at that spot in the document");
		const auto& placed = scroll_bridge.viewports().active();
		expect((placed[91] & 16) == 0, "placing the caret clears the out-of-view flag");

		// Typing after a scroll must snap the window back to the caret.
		MailboxRecord scroll_again; scroll_again.kind = FairyWriter::DocumentBridge::CommandScrollToFraction;
		scroll_again.revision = scroll_bridge.engine().revision();
		const std::uint32_t far_end = FairyWriter::DocumentBridge::CommandScrollTrackTravel;
		scroll_again.payload = {static_cast<std::uint8_t>(far_end & 0xff),
			static_cast<std::uint8_t>(far_end >> 8)};
		expect(scroll_bridge.submit(scroll_again) && scroll_bridge.pump(),
			"the bridge accepts a second scrollbar drag");
		MailboxRecord type; type.kind = FairyWriter::DocumentEngine::InsertText;
		type.revision = scroll_bridge.engine().revision(); type.payload = {'X'};
		expect(scroll_bridge.submit(type) && scroll_bridge.pump(),
			"the bridge accepts typing while scrolled");
		const auto& after_typing = scroll_bridge.viewports().active();
		expect((after_typing[91] & 16) == 0,
			"typing releases the scroll and brings the window back to the caret");

		// Opening a document is not a scroll. A window still anchored where the
		// user dragged the previous document's scrollbar would open the new file
		// somewhere in its middle, or at its end, instead of at its caret.
		MailboxRecord scroll_before_open = scroll_again;
		scroll_before_open.revision = scroll_bridge.engine().revision();
		expect(scroll_bridge.submit(scroll_before_open) && scroll_bridge.pump(),
			"the bridge accepts a scrollbar drag before an open");
		expect(read32(scroll_bridge.viewports().active(), 20) > 0,
			"the pre-open drag really moved the window off the document start");
		QTemporaryDir opened_files;
		const QString opened_path = opened_files.filePath(QStringLiteral("scrolled.txt"));
		QFile opened_file(opened_path);
		expect(opened_file.open(QIODevice::WriteOnly), "scrolled-open fixture opens");
		opened_file.write(long_text.toUtf8());
		opened_file.close();
		FairyWriter::FileCatalog opened_catalog(opened_files.path());
		const QString opened_id = opened_catalog.registerPath(opened_path);
		expect(!opened_id.isEmpty() && scroll_bridge.openFile(opened_catalog, opened_id),
			"the scrolled bridge opens a document from the catalog");
		const auto& opened_view = scroll_bridge.viewports().active();
		expect(read32(opened_view, 20) == 0 && (opened_view[91] & 16) == 0,
			"an opened document starts at its own caret, not the previous scroll offset");
	}
	if (!failures) std::cout << "All FairyWriter document engine tests passed.\n";
	return failures;
}
