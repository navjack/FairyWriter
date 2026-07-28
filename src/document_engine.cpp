#include "document_engine.h"
#include <QStringDecoder>
#include "document_writer.h"
#include "format_manager.h"
#include "format_reader.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextBoundaryFinder>
#include <memory>
#include <algorithm>
#include <string>
#include <unordered_set>

namespace FairyWriter {

static QString smartQuoteText(const QString& value, QChar previous) {
	QString result;
	result.reserve(value.size());
	for (const QChar c : value) {
		if (c == QChar('"') || c == QChar('\'')) {
			const bool closing = !previous.isNull() && !previous.isSpace() && previous.category() != QChar::Punctuation_Open;
			const bool single = c == QChar('\'');
			result += single ? (closing ? QChar(0x2019) : QChar(0x2018)) : (closing ? QChar(0x201d) : QChar(0x201c));
		} else {
			result += c;
		}
		previous = c;
	}
	return result;
}

static bool isChapterMarker(const QTextBlock& block) {
	static const QRegularExpression pattern(
		QStringLiteral("^\\s*chapter\\b"),
		QRegularExpression::CaseInsensitiveOption);
	return pattern.match(block.text()).hasMatch();
}
struct ProofingRange final {
	int start = 0;
	int length = 0;
	std::uint8_t flag = 0;
};
static std::vector<ProofingRange> proofingRanges(const QString& text) {
	std::vector<ProofingRange> ranges;
	if (text.isEmpty()) return ranges;

	static const QRegularExpression word_pattern(QStringLiteral("\\b[\\p{L}][\\p{L}'’-]*\\b"));
	static const std::unordered_set<std::string> common_typos = {
		"teh", "adn", "recieve", "seperate", "definately", "occurence",
		"occured", "wierd", "thier", "alot", "untill", "becuase"
	};

	QRegularExpressionMatchIterator words = word_pattern.globalMatch(text);
	QString previous_word;
	int previous_end = -1;
	while (words.hasNext()) {
		const QRegularExpressionMatch match = words.next();
		const int start = match.capturedStart();
		const int length = match.capturedLength();
		if (start < 0 || length <= 0) continue;
		const QString word = match.captured(0);
		const QString normalized = word.toLower();

		bool spelling = false;
		const std::string ascii = normalized.toStdString();
		if (common_typos.find(ascii) != common_typos.cend()) {
			spelling = true;
		} else if (word.size() >= 20) {
			spelling = true;
		} else {
			int repeated = 1;
			for (int i = 1; i < normalized.size(); ++i) {
				if (normalized.at(i) == normalized.at(i - 1)) ++repeated;
				else repeated = 1;
				if (repeated >= 4) { spelling = true; break; }
			}
		}
		if (spelling) ranges.push_back({start, length, FormatRunSpellingIssue});

		if (!previous_word.isEmpty()) {
			const QString gap = text.mid(previous_end, start - previous_end);
			if (normalized == previous_word && gap.contains(QRegularExpression(QStringLiteral("^\\s+$")))) {
				ranges.push_back({start, length, FormatRunGrammarIssue});
			}
		}
		previous_word = normalized;
		previous_end = start + length;
	}

	static const QRegularExpression double_space(QStringLiteral("  +"));
	QRegularExpressionMatchIterator spacing = double_space.globalMatch(text);
	while (spacing.hasNext()) {
		const QRegularExpressionMatch match = spacing.next();
		const int start = match.capturedStart();
		const int length = match.capturedLength();
		if (start >= 0 && length > 1) ranges.push_back({start, length, FormatRunGrammarIssue});
	}
	return ranges;
}

static std::vector<std::uint8_t> residentTitleBytes(const QString& filename) {
	QString title = filename.isEmpty() ? QStringLiteral("UNTITLED") : QFileInfo(filename).completeBaseName();
	if (title.isEmpty()) title = QStringLiteral("UNTITLED");
	title = title.toUpper();

	std::vector<std::uint8_t> result;
	result.reserve(MailboxLayout::ViewportTitleBytes);
	QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, title);
	boundary.toStart();
	int start = 0;
	for (int end = boundary.toNextBoundary();
		end >= 0 && result.size() < MailboxLayout::ViewportTitleBytes;
		start = end, end = boundary.toNextBoundary()) {
		const QString grapheme = title.mid(start, end - start);
		std::uint8_t resident = '?';
		if (grapheme.size() == 1) {
			const char16_t scalar = grapheme.front().unicode();
			if (scalar >= 0x20 && scalar <= 0x7e) resident = static_cast<std::uint8_t>(scalar);
			else if (scalar == 0x00a0) resident = ' ';
			else if (scalar == 0x2018 || scalar == 0x2019) resident = '\'';
			else if (scalar == 0x201c || scalar == 0x201d) resident = '"';
			else if (scalar == 0x2013 || scalar == 0x2014) resident = '-';
			else if (scalar == 0x2026) resident = '.';
		}
		if (resident == '?') {
			const QString decomposed = grapheme.normalized(QString::NormalizationForm_D);
			for (const QChar character : decomposed) {
				if (character.unicode() >= 0x20 && character.unicode() <= 0x7e) {
					resident = static_cast<std::uint8_t>(character.unicode());
					break;
				}
			}
		}
		result.push_back(resident);
	}
	return result;
}

DocumentEngine::DocumentEngine() : m_cursor(&m_document) {}

void DocumentEngine::refreshDocumentCache() const {
	const int document_revision = m_document.revision();
	if (document_revision == m_cached_document_revision) return;

	m_cached_plain_text = m_document.toPlainText();
	m_cached_statistics = {};
	m_cached_statistics.characters = static_cast<std::uint32_t>(m_cached_plain_text.size());
	m_cached_statistics.lines = 1;
	m_cached_statistics.paragraphs = static_cast<std::uint32_t>(std::max(1, m_document.blockCount()));
	bool inside_word = false;
	for (const QChar character : m_cached_plain_text) {
		if (character == QLatin1Char('\n')) ++m_cached_statistics.lines;
		const bool whitespace = character.isSpace();
		if (!whitespace && !inside_word) ++m_cached_statistics.words;
		inside_word = !whitespace;
	}
	m_cached_statistics.pages = std::max<std::uint32_t>(1, (m_cached_statistics.words + 249) / 250);

	m_cached_chapter_positions.clear();
	m_cached_inferred_chapters = 0;
	for (QTextBlock block = m_document.begin(); block.isValid(); block = block.next()) {
		const bool inferred = block.blockFormat().headingLevel() == 0 && isChapterMarker(block);
		if (block.blockFormat().headingLevel() == 1 || inferred) {
			m_cached_chapter_positions.push_back(block.position());
		}
		if (inferred) ++m_cached_inferred_chapters;
	}
	m_cached_document_revision = document_revision;
}

const QString& DocumentEngine::cachedPlainText() const {
	refreshDocumentCache();
	return m_cached_plain_text;
}

DocumentEngine::Statistics DocumentEngine::statistics() const {
	refreshDocumentCache();
	return m_cached_statistics;
}

bool DocumentEngine::load(const QString& filename, const QString& type) {
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly)) return false;
	const QString selected = type.isEmpty() ? filename.section(QLatin1Char('.'), -1).toLower() : type.toLower();
	std::unique_ptr<FormatReader> reader(FormatManager::createReader(&file, selected));
	if (!reader) return false;
	if ((selected == "odt" || selected == "fodt") && reader->type() != 3) return false;
	if (selected == "docx" && reader->type() != 4) return false;
	if (selected == "rtf" && reader->type() != 2) return false;
	QTextDocument parsed;
	reader->read(&file, &parsed);
	if (reader->hasError()) return false;
	m_document.clear();
	QTextCursor destination(&m_document);
	destination.insertFragment(QTextDocumentFragment(&parsed));
	m_filename = filename;
	m_format = selected.isEmpty() ? QStringLiteral("odt") : selected;
	m_loaded_modified = QFileInfo(filename).lastModified();
	m_has_loaded_file = true;
	m_read_only = !QFileInfo(filename).isWritable();
	m_revision = 0;
	m_saved_revision = 0;
	m_cursor = QTextCursor(&m_document);
	return true;
}

bool DocumentEngine::save(const QString& filename) {
	const QString target = filename.isEmpty() ? m_filename : filename;
	if (target.isEmpty()) return false;
	if (m_read_only && filename.isEmpty()) return false;
	if (filename.isEmpty() && m_has_loaded_file) {
		const QFileInfo current(target);
		if (!current.exists() || current.lastModified() != m_loaded_modified) return false;
	}
	DocumentWriter writer;
	writer.setDocument(&m_document);
	writer.setFileName(target);
	writer.setType(filename.isEmpty() ? m_format : target.section(QLatin1Char('.'), -1).toLower());
	if (!writer.write()) return false;
	m_filename = target;
	m_format = target.section(QLatin1Char('.'), -1).toLower();
	m_loaded_modified = QFileInfo(target).lastModified();
	m_has_loaded_file = true;
	m_saved_revision = m_revision;
	return true;
}

bool DocumentEngine::writeRecovery(const QString& filename) const {
	if (filename.isEmpty()) return false;
	DocumentWriter writer;
	writer.setDocument(&m_document);
	writer.setFileName(filename);
	writer.setType(m_format.isEmpty() ? QStringLiteral("odt") : m_format);
	return writer.write();
}

bool DocumentEngine::recover(const QString& filename, const QString& originalFilename) {
	if (filename.isEmpty()) return false;
	const QString suffix = filename.section(QLatin1Char('.'), -1).toLower();
	const QString recoveryType = suffix.isEmpty() ? (m_format.isEmpty() ? QStringLiteral("odt") : m_format) : suffix;
	if (!load(filename, recoveryType)) return false;
	m_filename = originalFilename;
	m_has_loaded_file = !originalFilename.isEmpty();
	m_read_only = false;
	return true;
}

bool DocumentEngine::newDocument(std::uint64_t expected_revision) {
	if (!accepts(expected_revision)) return false;
	m_document.clear();
	m_cursor = QTextCursor(&m_document);
	m_filename.clear();
	m_format = QStringLiteral("odt");
	m_loaded_modified = QDateTime();
	m_has_loaded_file = false;
	m_read_only = false;
	m_revision = 0;
	m_saved_revision = 0;
	return true;
}

bool DocumentEngine::accepts(std::uint64_t expected_revision) const noexcept {
	return expected_revision == m_revision;
}

void DocumentEngine::commit() {
	++m_revision;
}

bool DocumentEngine::insertText(std::uint64_t expected_revision, const QString& value) {
	if (m_read_only || !accepts(expected_revision) || value.isEmpty()) return false;
	const QString insertion = m_smart_quotes ? smartQuoteText(value, m_document.characterAt(m_cursor.position() - 1)) : value;
	m_cursor.insertText(insertion);
	commit();
	return true;
}

bool DocumentEngine::deleteSelection(std::uint64_t expected_revision) {
	if (m_read_only || !accepts(expected_revision) || !m_cursor.hasSelection()) return false;
	m_cursor.removeSelectedText();
	commit();
	return true;
}

bool DocumentEngine::deleteBackward(std::uint64_t expected_revision) {
	if (m_read_only || !accepts(expected_revision)) return false;
	if (!m_cursor.hasSelection()) {
		const QString& plain = cachedPlainText();
		QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, plain);
		boundary.setPosition(m_cursor.position());
		const int previous = boundary.toPreviousBoundary();
		if (previous < 0) return false;
		m_cursor.setPosition(previous, QTextCursor::KeepAnchor);
	}
	m_cursor.removeSelectedText();
	commit();
	return true;
}

bool DocumentEngine::deleteForward(std::uint64_t expected_revision) {
	if (m_read_only || !accepts(expected_revision)) return false;
	if (!m_cursor.hasSelection()) {
		const QString& plain = cachedPlainText();
		QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, plain);
		boundary.setPosition(m_cursor.position());
		const int next = boundary.toNextBoundary();
		if (next < 0) return false;
		m_cursor.setPosition(next, QTextCursor::KeepAnchor);
	}
	m_cursor.removeSelectedText();
	commit();
	return true;
}

bool DocumentEngine::moveCursor(std::uint64_t expected_revision, QTextCursor::MoveOperation op,
	QTextCursor::MoveMode mode, int n) {
	if (!accepts(expected_revision) || n < 1) return false;
	if (!m_cursor.movePosition(op, mode, n)) return true; // valid boundary no-op
	commit();
	return true;
}

bool DocumentEngine::setCursorPosition(std::uint64_t expected_revision, int position) {
	if (!accepts(expected_revision)) return false;
	const int clamped = qBound(0, position, static_cast<int>(cachedPlainText().size()));
	if (m_cursor.position() == clamped && !m_cursor.hasSelection()) return true; // no-op
	m_cursor.setPosition(clamped);
	commit();
	return true;
}

bool DocumentEngine::setSelectionEndPosition(std::uint64_t expected_revision, int position) {
	if (!accepts(expected_revision)) return false;
	const int clamped = qBound(0, position, static_cast<int>(cachedPlainText().size()));
	if (m_cursor.position() == clamped) return true; // no-op
	m_cursor.setPosition(clamped, QTextCursor::KeepAnchor);
	commit();
	return true;
}

bool DocumentEngine::undo(std::uint64_t expected_revision) {
	if (m_read_only || !accepts(expected_revision) || !m_document.isUndoAvailable()) return false;
	m_document.undo();
	commit();
	return true;
}

bool DocumentEngine::redo(std::uint64_t expected_revision) {
	if (m_read_only || !accepts(expected_revision) || !m_document.isRedoAvailable()) return false;
	m_document.redo();
	commit();
	return true;
}

bool DocumentEngine::selectAll(std::uint64_t expected_revision) {
	if (!accepts(expected_revision)) return false;
	m_cursor.select(QTextCursor::Document);
	commit();
	return true;
}

bool DocumentEngine::replaceAll(std::uint64_t expected_revision, const QString& find, const QString& replacement) {
	if (m_read_only || !accepts(expected_revision) || find.isEmpty()) return false;
	QTextCursor search(&m_document);
	QVector<QTextCursor> matches;
	while (!(search = m_document.find(find, search)).isNull()) matches.push_back(search);
	if (matches.isEmpty()) return false;
	QTextCursor transaction(&m_document);
	transaction.beginEditBlock();
	for (auto it = matches.crbegin(); it != matches.crend(); ++it) {
		QTextCursor cursor = *it;
		cursor.insertText(replacement);
	}
	transaction.endEditBlock();
	m_cursor = QTextCursor(&m_document);
	commit();
	return true;
}

bool DocumentEngine::findNext(std::uint64_t expected_revision, const QString& query) {
	if (!accepts(expected_revision) || query.isEmpty()) return false;
	QTextCursor search = m_cursor;
	QTextCursor match = m_document.find(query, search);
	if (match.isNull()) {
		search = QTextCursor(&m_document);
		match = m_document.find(query, search);
	}
	if (match.isNull()) {
		if (m_cursor.hasSelection()) {
			m_cursor.clearSelection();
			commit();
		}
		return false;
	}
	m_cursor = match;
	commit();
	return true;
}

std::uint32_t DocumentEngine::inferredChapterCount() const {
	refreshDocumentCache();
	return m_cached_inferred_chapters;
}

bool DocumentEngine::promoteInferredChapterHeadings(std::uint64_t expected_revision) {
	if (m_read_only || !accepts(expected_revision) || inferredChapterCount() == 0) return false;
	QTextCursor transaction(&m_document);
	transaction.beginEditBlock();
	for (QTextBlock block = m_document.begin(); block.isValid(); block = block.next()) {
		if (block.blockFormat().headingLevel() != 0 || !isChapterMarker(block)) continue;
		QTextCursor heading(block);
		QTextBlockFormat format = block.blockFormat();
		format.setHeadingLevel(1);
		heading.setBlockFormat(format);
	}
	transaction.endEditBlock();
	commit();
	return true;
}

bool DocumentEngine::makeViewport(std::size_t max_text_bytes, ViewportSnapshot& snapshot,
	std::optional<std::uint32_t> preferred_start, bool force_start) const {
	if (max_text_bytes > MailboxLayout::ViewportSlotBytes - MailboxLayout::ViewportHeaderBytes) {
		max_text_bytes = MailboxLayout::ViewportSlotBytes - MailboxLayout::ViewportHeaderBytes;
	}
	const QString& plain = cachedPlainText();
	const int plain_size = static_cast<int>(plain.size());
	if (max_text_bytes == 0 && !plain.isEmpty()) return false;

	const int cursor_position = qBound(0, m_cursor.position(), plain_size);
	const auto boundaryAtOrBefore = [&plain, plain_size](int position) -> int {
		QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, plain);
		finder.setPosition(qBound(0, position, plain_size));
		if (finder.isAtBoundary()) return static_cast<int>(finder.position());
		const int previous = finder.toPreviousBoundary();
		return previous < 0 ? 0 : previous;
	};

	struct Window {
		int start = 0;
		int end = 0;
		int cursor_cells = 0;
		int cells = 0;
		QByteArray bytes;
	};
	const auto buildWindow = [&plain, plain_size, cursor_position, max_text_bytes](int start) {
		Window window;
		window.start = start;
		window.end = start;
		window.bytes.reserve(static_cast<qsizetype>(max_text_bytes));
		QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, plain);
		finder.setPosition(start);
		int grapheme_start = start;
		for (int grapheme_end = finder.toNextBoundary();
			grapheme_end >= 0;
			grapheme_start = grapheme_end, grapheme_end = finder.toNextBoundary()) {
			const QByteArray grapheme = plain.mid(grapheme_start,
				grapheme_end - grapheme_start).toUtf8();
			if (window.bytes.size() + grapheme.size() > static_cast<qsizetype>(max_text_bytes)) break;
			window.bytes.append(grapheme);
			window.end = grapheme_end;
			++window.cells;
			if (grapheme_end <= cursor_position) ++window.cursor_cells;
			if (grapheme_end == plain_size) break;
		}
		return window;
	};

	constexpr int EdgeGuardCells = 30;
	Window window;
	bool keep_preferred = false;
	if (preferred_start.has_value()) {
		const int start = boundaryAtOrBefore(static_cast<int>(
			qMin<std::uint32_t>(*preferred_start, static_cast<std::uint32_t>(plain_size))));
		window = buildWindow(start);
		if (force_start) {
			// The scrollbar owns the window here, so the usual cursor-containment
			// and edge-guard checks do not apply -- moving away from the caret is
			// the whole point.
			//
			// Dragging to the far end anchors at the very last character, which
			// builds an empty window and would fail the emptiness check below,
			// freezing the view. Back the start up so the last screenful stays
			// full, the same way the cursor-anchored path fills its final page.
			if (window.end == plain_size && window.start > 0
				&& window.bytes.size() < static_cast<qsizetype>(max_text_bytes)) {
				QTextBoundaryFinder backfill(QTextBoundaryFinder::Grapheme, plain);
				backfill.setPosition(window.start);
				int expanded_start = window.start;
				std::size_t expanded_bytes = static_cast<std::size_t>(window.bytes.size());
				for (int previous = backfill.toPreviousBoundary();
					previous >= 0;
					previous = backfill.toPreviousBoundary()) {
					const std::size_t grapheme_bytes = static_cast<std::size_t>(
						plain.mid(previous, expanded_start - previous).toUtf8().size());
					if (expanded_bytes + grapheme_bytes > max_text_bytes) break;
					expanded_start = previous;
					expanded_bytes += grapheme_bytes;
					if (previous == 0) break;
				}
				window = buildWindow(expanded_start);
			}
			keep_preferred = true;
		} else {
			const bool contains_cursor = cursor_position >= window.start && cursor_position <= window.end;
			const bool clear_left_edge = window.start == 0 || window.cursor_cells >= EdgeGuardCells;
			const bool clear_right_edge = window.end == plain_size
				|| window.cells - window.cursor_cells >= EdgeGuardCells;
			keep_preferred = contains_cursor && clear_left_edge && clear_right_edge;
		}
	}

	if (!keep_preferred) {
		int start = cursor_position;
		std::size_t bytes_before = 0;
		int cells_before = 0;
		const std::size_t target_before = max_text_bytes * 2 / 5;
		QTextBoundaryFinder backward(QTextBoundaryFinder::Grapheme, plain);
		backward.setPosition(cursor_position);
		for (int previous = backward.toPreviousBoundary();
			previous >= 0;
			previous = backward.toPreviousBoundary()) {
			const std::size_t grapheme_bytes = static_cast<std::size_t>(
				plain.mid(previous, start - previous).toUtf8().size());
			if (bytes_before + grapheme_bytes > target_before && cells_before >= EdgeGuardCells) break;
			if (bytes_before + grapheme_bytes > max_text_bytes) break;
			start = previous;
			bytes_before += grapheme_bytes;
			++cells_before;
			if (previous == 0) break;
		}
		window = buildWindow(start);

		// Near the document end, use otherwise empty transport capacity for
		// earlier text. This keeps the final page full without moving it on
		// each cursor command.
		if (window.end == plain_size && window.start > 0
			&& window.bytes.size() < static_cast<qsizetype>(max_text_bytes)) {
			QTextBoundaryFinder backfill(QTextBoundaryFinder::Grapheme, plain);
			backfill.setPosition(window.start);
			int expanded_start = window.start;
			std::size_t expanded_bytes = static_cast<std::size_t>(window.bytes.size());
			for (int previous = backfill.toPreviousBoundary();
				previous >= 0;
				previous = backfill.toPreviousBoundary()) {
				const std::size_t grapheme_bytes = static_cast<std::size_t>(
					plain.mid(previous, expanded_start - previous).toUtf8().size());
				if (expanded_bytes + grapheme_bytes > max_text_bytes) break;
				expanded_start = previous;
				expanded_bytes += grapheme_bytes;
				if (previous == 0) break;
			}
			window = buildWindow(expanded_start);
		}
	}

	if (!plain.isEmpty() && window.bytes.isEmpty()) return false;
	snapshot.revision = m_revision;
	snapshot.cursor = static_cast<std::uint32_t>(m_cursor.position());
	const int selection_start = m_cursor.hasSelection() ? m_cursor.selectionStart() : m_cursor.position();
	const int selection_end = m_cursor.hasSelection() ? m_cursor.selectionEnd() : m_cursor.position();
	snapshot.selection_start = static_cast<std::uint32_t>(selection_start);
	snapshot.selection_end = static_cast<std::uint32_t>(selection_end);
	snapshot.text_offset = static_cast<std::uint32_t>(window.start);
	snapshot.character_count = m_cached_statistics.characters;
	snapshot.word_count = m_cached_statistics.words;
	snapshot.paragraph_count = m_cached_statistics.paragraphs;
	snapshot.page_count = m_cached_statistics.pages;

	const QString text_before_cursor = plain.left(m_cursor.position());
	std::uint32_t cursor_words = 0;
	bool in_word = false;
	for (const QChar ch : text_before_cursor) {
		const bool space = ch.isSpace();
		if (!space && !in_word) ++cursor_words;
		in_word = !space;
	}
	snapshot.current_page = std::clamp<std::uint32_t>((cursor_words + 249) / 250, 1, snapshot.page_count);

	const QByteArray full_utf8 = plain.toUtf8();
	snapshot.total_document_bytes = static_cast<std::uint32_t>(full_utf8.size());
	snapshot.bytes_before = static_cast<std::uint32_t>(plain.left(window.start).toUtf8().size());
	snapshot.bytes_after = static_cast<std::uint32_t>(plain.mid(window.end).toUtf8().size());

	const std::uint32_t chapter = static_cast<std::uint32_t>(
		std::upper_bound(m_cached_chapter_positions.cbegin(),
			m_cached_chapter_positions.cend(), m_cursor.position())
		- m_cached_chapter_positions.cbegin());
	snapshot.chapter = static_cast<std::uint16_t>(qMin<std::uint32_t>(qMax<std::uint32_t>(1, chapter), 0xffff));
	// Bit 4 tells the cartridge the caret is not inside this window at all, which
	// only happens when the view has been scrolled away from it. Without it the
	// cartridge would compute a viewport-relative cursor that matches no drawn
	// character and fall back to parking the caret at the end of the text.
	const bool cursor_in_view =
		m_cursor.position() >= window.start && m_cursor.position() <= window.end;
	snapshot.status_flags = static_cast<std::uint8_t>(
		(isDirty() ? 1 : 0) |
		(m_read_only ? 2 : 0) |
		(window.start == 0 ? 4 : 0) |
		(window.end == plain_size ? 8 : 0) |
		(cursor_in_view ? 0 : 16));
	snapshot.line_count = m_cached_statistics.lines;
	snapshot.display_title = residentTitleBytes(m_filename);
	snapshot.utf8.assign(reinterpret_cast<const std::uint8_t*>(window.bytes.constData()),
		reinterpret_cast<const std::uint8_t*>(window.bytes.constData() + window.bytes.size()));

	// Extract grapheme boundary offsets
	snapshot.grapheme_offsets.clear();
	const QString window_text = plain.mid(window.start, window.end - window.start);
	QTextBoundaryFinder g_finder(QTextBoundaryFinder::Grapheme, window_text);
	int g_pos = 0;
	while ((g_pos = g_finder.toNextBoundary()) >= 0) {
		const std::uint16_t byte_offset = static_cast<std::uint16_t>(window_text.left(g_pos).toUtf8().size());
		snapshot.grapheme_offsets.push_back(byte_offset);
	}

	// Extract line break offsets
	snapshot.line_break_offsets.clear();
	for (int i = 0; i < window.bytes.size(); ++i) {
		if (window.bytes[i] == '\n') {
			snapshot.line_break_offsets.push_back(static_cast<std::uint16_t>(i));
		}
	}

	// Extract formatting runs
	snapshot.format_runs.clear();
	const auto toUnitOffset = [](int utf16_offset) -> std::uint16_t {
		return static_cast<std::uint16_t>(std::clamp(utf16_offset, 0, 0xffff));
	};
	QTextBlock block = m_document.findBlock(window.start);
	while (block.isValid() && block.position() < window.end) {
		const int heading = block.blockFormat().headingLevel();
		const auto align = block.blockFormat().alignment();
		std::uint8_t align_val = 0;
		if (align & Qt::AlignHCenter) align_val = 1;
		else if (align & Qt::AlignRight) align_val = 2;
		else if (align & Qt::AlignJustify) align_val = 3;

		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
			QTextFragment fragment = it.fragment();
			if (!fragment.isValid()) continue;
			const int frag_start = fragment.position();
			const int frag_end = frag_start + fragment.length();
			if (frag_end <= window.start || frag_start >= window.end) continue;

			QTextCharFormat fmt = fragment.charFormat();
			std::uint8_t flags = 0;
			if (fmt.fontWeight() == QFont::Bold || fmt.font().bold()) flags |= FormatRunBold;
			if (fmt.fontItalic()) flags |= FormatRunItalic;
			if (fmt.fontUnderline()) flags |= FormatRunUnderline;

			if (flags != 0 || heading != 0 || align_val != 0) {
				const int start_char = std::max(window.start, frag_start);
				const int end_char = std::min(window.end, frag_end);
				FormatRunWire run;
				run.offset = toUnitOffset(start_char - window.start);
				run.length = toUnitOffset(end_char - start_char);
				run.flags = flags;
				run.heading_level = static_cast<std::uint8_t>(heading);
				run.alignment = align_val;
				run.reserved = 0;
				snapshot.format_runs.push_back(run);
			}
		}
		block = block.next();
	}
	const std::vector<ProofingRange> proofing = proofingRanges(window_text);
	for (const auto& range : proofing) {
		const int window_units = static_cast<int>(window_text.size());
		const int start = std::clamp(range.start, 0, window_units);
		const int end = std::clamp(range.start + range.length, start, window_units);
		if (end <= start) continue;
		FormatRunWire run;
		run.offset = toUnitOffset(start);
		run.length = toUnitOffset(end - start);
		run.flags = range.flag;
		run.heading_level = 0;
		run.alignment = 0;
		run.reserved = 0;
		snapshot.format_runs.push_back(run);
	}
	std::sort(snapshot.format_runs.begin(), snapshot.format_runs.end(),
		[](const FormatRunWire& lhs, const FormatRunWire& rhs) {
			if (lhs.offset != rhs.offset) return lhs.offset < rhs.offset;
			if (lhs.length != rhs.length) return lhs.length < rhs.length;
			if (lhs.heading_level != rhs.heading_level) return lhs.heading_level < rhs.heading_level;
			if (lhs.alignment != rhs.alignment) return lhs.alignment < rhs.alignment;
			return lhs.flags < rhs.flags;
		});
	std::vector<FormatRunWire> normalized_runs;
	normalized_runs.reserve(snapshot.format_runs.size());
	for (const auto& run : snapshot.format_runs) {
		if (run.length == 0) continue;
		if (!normalized_runs.empty()
			&& normalized_runs.back().offset == run.offset
			&& normalized_runs.back().length == run.length) {
			normalized_runs.back().flags |= run.flags;
			if (normalized_runs.back().heading_level == 0) normalized_runs.back().heading_level = run.heading_level;
			if (normalized_runs.back().alignment == 0) normalized_runs.back().alignment = run.alignment;
			continue;
		}
		normalized_runs.push_back(run);
	}
	snapshot.format_runs = std::move(normalized_runs);

	return true;
}

bool DocumentEngine::apply(const MailboxRecord& command) {
	if (command.protocol != MailboxProtocol || !accepts(command.revision)) return false;
	switch (command.kind) {
	case NewDocument:
		return newDocument(command.revision);
	case InsertText: {
		QStringDecoder decoder(QStringDecoder::Utf8);
		const QString value = decoder.decode(QByteArray(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size())));
		if (decoder.hasError()) return false;
		return insertText(command.revision, value);
	}
	case PasteText: {
		QStringDecoder decoder(QStringDecoder::Utf8);
		const QString value = decoder.decode(QByteArray(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size())));
		if (decoder.hasError()) return false;
		return insertText(command.revision, value);
	}
	case Save:
		if (!accepts(command.revision)) return false;
		return save();
	case SelectAll: return selectAll(command.revision);
	case DeleteSelection: return deleteSelection(command.revision);
	case DeleteBackward: return deleteBackward(command.revision);
	case DeleteForward: return deleteForward(command.revision);
	case MoveStart: return moveCursor(command.revision, QTextCursor::Start);
	case MoveEnd: return moveCursor(command.revision, QTextCursor::End);
	case MoveLeft: return moveCursor(command.revision, QTextCursor::Left);
	case MoveRight: return moveCursor(command.revision, QTextCursor::Right);
	case MoveWordLeft: return moveCursor(command.revision, QTextCursor::PreviousWord);
	case MoveWordRight: return moveCursor(command.revision, QTextCursor::NextWord);
	case ExtendLeft: return moveCursor(command.revision, QTextCursor::Left, QTextCursor::KeepAnchor);
	case ExtendRight: return moveCursor(command.revision, QTextCursor::Right, QTextCursor::KeepAnchor);
	case MoveUp: return moveCursor(command.revision, QTextCursor::Up);
	case MoveDown: return moveCursor(command.revision, QTextCursor::Down);
	case ExtendUp: return moveCursor(command.revision, QTextCursor::Up, QTextCursor::KeepAnchor);
	case ExtendDown: return moveCursor(command.revision, QTextCursor::Down, QTextCursor::KeepAnchor);
	case MoveLineStart: return moveCursor(command.revision, QTextCursor::StartOfLine);
	case MoveLineEnd: return moveCursor(command.revision, QTextCursor::EndOfLine);
	case ExtendLineStart: return moveCursor(command.revision, QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
	case ExtendLineEnd: return moveCursor(command.revision, QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
	case PromoteChapterHeadings: return promoteInferredChapterHeadings(command.revision);
	case MovePageUp: return moveCursor(command.revision, QTextCursor::Up, QTextCursor::MoveAnchor, 8);
	case MovePageDown: return moveCursor(command.revision, QTextCursor::Down, QTextCursor::MoveAnchor, 8);
	case SetCursorPosition: {
		if (command.payload.size() != 4) return false;
		const std::uint32_t position = *reinterpret_cast<const std::uint32_t*>(command.payload.data());
		return setCursorPosition(command.revision, static_cast<int>(position));
	}
	case ExtendCursorPosition: {
		if (command.payload.size() != 4) return false;
		const std::uint32_t position = *reinterpret_cast<const std::uint32_t*>(command.payload.data());
		return setSelectionEndPosition(command.revision, static_cast<int>(position));
	}
	case ToggleBold:
	case ToggleItalic:
	case ToggleUnderline: {
		if (m_read_only || !accepts(command.revision) || !m_cursor.hasSelection()) return false;
		QTextCharFormat format;
		if (command.kind == ToggleBold) format.setFontWeight(m_cursor.charFormat().fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
		if (command.kind == ToggleItalic) format.setFontItalic(!m_cursor.charFormat().fontItalic());
		if (command.kind == ToggleUnderline) format.setFontUnderline(!m_cursor.charFormat().fontUnderline());
		m_cursor.mergeCharFormat(format);
		commit();
		return true;
	}
	case AlignLeft:
	case AlignCenter:
	case AlignRight: {
		if (m_read_only || !accepts(command.revision)) return false;
		QTextBlockFormat block = m_cursor.blockFormat();
		block.setAlignment(command.kind == AlignLeft ? Qt::AlignLeft : command.kind == AlignCenter ? Qt::AlignHCenter : Qt::AlignRight);
		m_cursor.setBlockFormat(block);
		commit();
		return true;
	}
	case IndentIncrease:
	case IndentDecrease: {
		if (m_read_only || !accepts(command.revision)) return false;
		QTextBlockFormat block = m_cursor.blockFormat();
		block.setIndent(qMax(0, block.indent() + (command.kind == IndentIncrease ? 1 : -1)));
		m_cursor.setBlockFormat(block);
		commit();
		return true;
	}
	case ListBullet:
	case ListOrdered: {
		if (m_read_only || !accepts(command.revision)) return false;
		QTextListFormat list;
		list.setStyle(command.kind == ListBullet ? QTextListFormat::ListDisc : QTextListFormat::ListDecimal);
		m_cursor.createList(list);
		commit();
		return true;
	}
	case ReplaceAll: {
		const auto separator = std::find(command.payload.cbegin(), command.payload.cend(), std::uint8_t(0));
		if (separator == command.payload.cend()) return false;
		const QByteArray find_bytes(reinterpret_cast<const char*>(command.payload.data()), separator - command.payload.cbegin());
		const auto replacement_begin = separator + 1;
		const QByteArray replacement_bytes(reinterpret_cast<const char*>(command.payload.data() + (replacement_begin - command.payload.cbegin())), command.payload.cend() - replacement_begin);
		QStringDecoder decoder(QStringDecoder::Utf8);
		const QString find = decoder.decode(find_bytes);
		if (decoder.hasError()) return false;
		QStringDecoder replacement_decoder(QStringDecoder::Utf8);
		const QString replacement = replacement_decoder.decode(replacement_bytes);
		if (replacement_decoder.hasError()) return false;
		return replaceAll(command.revision, find, replacement);
	}
	case FindNext: {
		QStringDecoder decoder(QStringDecoder::Utf8);
		const QString query = decoder.decode(QByteArray(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size())));
		if (decoder.hasError()) return false;
		return findNext(command.revision, query);
	}
	case SetSmartQuotes:
		if (command.payload.size() != 1) return false;
		m_smart_quotes = command.payload[0] != 0;
		return true;
	case Undo: return undo(command.revision);
	case Redo: return redo(command.revision);
	default: return false;
	}
}

} // namespace FairyWriter
