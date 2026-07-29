#include "markdown_codec.h"

extern "C" {
#include "cmark-gfm.h"
#include "cmark-gfm-extension_api.h"
#include "cmark-gfm-core-extensions.h"
}

#include <memory>

namespace FairyWriter {
namespace {

struct ParserDeleter final {
	void operator()(cmark_parser* parser) const {
		if (parser) cmark_parser_free(parser);
	}
};

struct NodeDeleter final {
	void operator()(cmark_node* node) const {
		if (node) cmark_node_free(node);
	}
};

std::unique_ptr<cmark_parser, ParserDeleter> parserWithGfmExtensions()
{
	cmark_gfm_core_extensions_ensure_registered();
	std::unique_ptr<cmark_parser, ParserDeleter> parser(
		cmark_parser_new(CMARK_OPT_SOURCEPOS | CMARK_OPT_VALIDATE_UTF8));
	if (!parser) return {};
	static constexpr const char* ExtensionNames[] = {
		"table", "strikethrough", "autolink", "tagfilter", "tasklist"
	};
	for (const char* name : ExtensionNames) {
		cmark_syntax_extension* extension = cmark_find_syntax_extension(name);
		if (!extension || !cmark_parser_attach_syntax_extension(parser.get(), extension)) {
			return {};
		}
	}
	return parser;
}

std::unique_ptr<cmark_node, NodeDeleter> parse(const QByteArray& source,
	cmark_llist** extensions = nullptr)
{
	auto parser = parserWithGfmExtensions();
	if (!parser) return {};
	cmark_parser_feed(parser.get(), source.constData(),
		static_cast<std::size_t>(source.size()));
	if (extensions) *extensions = cmark_parser_get_syntax_extensions(parser.get());
	return std::unique_ptr<cmark_node, NodeDeleter>(cmark_parser_finish(parser.get()));
}

} // namespace

bool MarkdownCodec::validate(const QByteArray& source)
{
	return parse(source) != nullptr;
}

QString MarkdownCodec::safeHtml(const QByteArray& source)
{
	auto parser = parserWithGfmExtensions();
	if (!parser) return {};
	cmark_parser_feed(parser.get(), source.constData(),
		static_cast<std::size_t>(source.size()));
	cmark_llist* extensions = cmark_parser_get_syntax_extensions(parser.get());
	std::unique_ptr<cmark_node, NodeDeleter> document(cmark_parser_finish(parser.get()));
	if (!document) return {};
	char* rendered = cmark_render_html(document.get(), CMARK_OPT_DEFAULT, extensions);
	if (!rendered) return {};
	const QString result = QString::fromUtf8(rendered);
	cmark_get_default_mem_allocator()->free(rendered);
	return result;
}

} // namespace FairyWriter
