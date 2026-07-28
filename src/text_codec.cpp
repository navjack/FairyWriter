/*
	SPDX-FileCopyrightText: 2022 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "text_codec.h"

#include <QByteArray>
#include <QHash>
#include <QString>

//-----------------------------------------------------------------------------

namespace
{

// Windows-1252 differs from Latin-1 only in 0x80-0x9F, where Latin-1 has C1
// control codes and CP1252 has typographic punctuation. Those 32 characters are
// exactly what a Word export puts in prose -- curly quotes, en/em dashes, the
// ellipsis -- so the table covers that range and everything else is Latin-1.
//
// This is deliberately the *only* legacy codepage FairyWriter carries, and it
// exists for one format. Plain text, ODT and DOCX are UTF-8 or carry a declared
// XML encoding and never reach this file. RTF is the exception: the spec has
// documents declare a codepage and write non-ASCII as \'xx byte escapes, so RTF
// files are essentially never UTF-8. `RtfReader`'s constructor calls
// `setCodepage(1252)` and then dereferences `m_codec` without a null check, so
// CP1252 must always resolve or every RTF import crashes.
constexpr char16_t cp1252_high[32] = {
	0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
	0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
	0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
	0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178
};

// A single-byte codec driven by a 32-entry override table over a Latin-1 base.
class TextCodecSingleByte : public TextCodec
{
public:
	explicit TextCodecSingleByte(const char16_t* high)
		: TextCodec("ISO-8859-1")
		, m_high(high)
	{
	}

	bool isValid() const override
	{
		return m_high != nullptr;
	}

	QByteArray fromUnicode(const QString& input) override
	{
		QByteArray output;
		output.reserve(input.size());
		for (const QChar character : input) {
			const char16_t unicode = character.unicode();
			if ((unicode < 0x80) || ((unicode >= 0xa0) && (unicode < 0x100))) {
				// Shared with Latin-1, including the whole 0xA0-0xFF range.
				output.append(static_cast<char>(unicode));
				continue;
			}
			// The 0x80-0x9F byte range is the only place the two encodings
			// disagree, so a reverse scan of 32 entries settles it.
			char byte = '?';
			for (int i = 0; i < 32; ++i) {
				if ((m_high[i] == unicode) && (m_high[i] != 0xfffd)) {
					byte = static_cast<char>(0x80 + i);
					break;
				}
			}
			output.append(byte);
		}
		return output;
	}

	QString toUnicode(const QByteArray& input) override
	{
		QString output;
		output.reserve(input.size());
		for (const char raw : input) {
			const unsigned char byte = static_cast<unsigned char>(raw);
			if ((byte >= 0x80) && (byte < 0xa0)) {
				output.append(QChar(m_high[byte - 0x80]));
			} else {
				output.append(QChar(static_cast<char16_t>(byte)));
			}
		}
		return output;
	}

private:
	const char16_t* m_high;
};

//-----------------------------------------------------------------------------

TextCodec* builtinCodecForName(const QByteArray& name)
{
	const QByteArray upper = name.toUpper();
	if ((upper == "CP1252") || (upper == "WINDOWS-1252") || (upper == "CP-1252")) {
		return new TextCodecSingleByte(cp1252_high);
	}
	return nullptr;
}

//-----------------------------------------------------------------------------

class TextCodecCache
{
public:
	~TextCodecCache()
	{
		qDeleteAll(m_codecs);
	}

	TextCodec* fetch(const QByteArray& name)
	{
		TextCodec* codec = m_codecs.value(name, nullptr);
		if (codec) {
			return codec;
		}

		// The built-in table wins over Qt's converter so that the encodings
		// FairyWriter promises behave identically on every platform, rather
		// than depending on whether the local Qt was built against ICU.
		codec = builtinCodecForName(name);
		if (codec) {
			m_codecs.insert(name, codec);
			return codec;
		}

		codec = new TextCodec(name);
		if (codec->isValid()) {
			m_codecs.insert(name, codec);
			return codec;
		} else {
			delete codec;
		}

		return nullptr;
	}

private:
	QHash<QByteArray, TextCodec*> m_codecs;
};

}

//-----------------------------------------------------------------------------

TextCodec* TextCodec::codecForName(const QByteArray& name)
{
	static TextCodecCache codecs;
	return codecs.fetch(name);
}

//-----------------------------------------------------------------------------
