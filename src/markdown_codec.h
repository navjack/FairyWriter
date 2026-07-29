#ifndef FAIRYWRITER_MARKDOWN_CODEC_H
#define FAIRYWRITER_MARKDOWN_CODEC_H

#include <QByteArray>
#include <QString>

namespace FairyWriter {

class MarkdownCodec final {
public:
	// Parses with the pinned cmark-gfm reference implementation and all core GFM
	// extensions. Invalid UTF-8 is rejected before this boundary by the caller.
	static bool validate(const QByteArray& source);

	// Returns safe reference-rendered HTML for conformance tests and diagnostics.
	// cmark-gfm's safe default replaces raw HTML and strips unsafe destinations.
	static QString safeHtml(const QByteArray& source);
};

} // namespace FairyWriter

#endif
