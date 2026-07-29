/*
	SPDX-FileCopyrightText: 2012 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FOCUSWRITER_DOCUMENT_WRITER_H
#define FOCUSWRITER_DOCUMENT_WRITER_H

#include <QByteArray>
#include <QString>

#include <functional>
#include <utility>

class QTextDocument;

class DocumentWriter
{
public:
	enum class WriteMode {
		ReplaceExisting,
		CreateNew
	};

	explicit DocumentWriter();
	~DocumentWriter();

	void setDocument(const QTextDocument* document);
	void setFileName(const QString& filename);
	void setType(const QString& type);
	void setWriteByteOrderMark(bool write_bom);
	void setMarkdownSource(const QByteArray& source);
	void setPreCommitCheck(std::function<bool()> check);

	bool write(WriteMode mode = WriteMode::ReplaceExisting);

private:
	QString m_filename;
	QString m_type;
	const QTextDocument* m_document;
	QByteArray m_markdown_source;
	std::function<bool()> m_precommit_check;
	bool m_write_bom;
};

inline void DocumentWriter::setDocument(const QTextDocument* document)
{
	m_document = document;
}

inline void DocumentWriter::setFileName(const QString& filename)
{
	m_filename = filename;
}

inline void DocumentWriter::setType(const QString& type)
{
	m_type = type.toLower();
}

inline void DocumentWriter::setWriteByteOrderMark(bool write_bom)
{
	m_write_bom = write_bom;
}

inline void DocumentWriter::setMarkdownSource(const QByteArray& source)
{
	m_markdown_source = source;
}

inline void DocumentWriter::setPreCommitCheck(std::function<bool()> check)
{
	m_precommit_check = std::move(check);
}

#endif // FOCUSWRITER_DOCUMENT_WRITER_H
