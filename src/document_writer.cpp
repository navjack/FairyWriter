/*
	SPDX-FileCopyrightText: 2012 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "document_writer.h"

#include "docx_writer.h"
#include "odt_writer.h"
#include "rtf_writer.h"

#include <QSaveFile>
#include <QTextDocument>
#include <QTextStream>

#include <errno.h>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

//-----------------------------------------------------------------------------

DocumentWriter::DocumentWriter()
	: m_type("fodt")
	, m_document(nullptr)
	, m_write_bom(false)
{
}

//-----------------------------------------------------------------------------

DocumentWriter::~DocumentWriter()
{
}

//-----------------------------------------------------------------------------

bool DocumentWriter::write()
{
	Q_ASSERT(m_document);
	Q_ASSERT(!m_filename.isEmpty());

	bool saved = false;

	QSaveFile file(m_filename);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}

	if (m_type == "odt") {
		OdtWriter writer;
		saved = writer.write(&file, m_document);
	} else if (m_type == "fodt") {
		OdtWriter writer;
		writer.setFlatXML(true);
		saved = writer.write(&file, m_document);
	} else if (m_type == "docx") {
		DocxWriter writer;
		saved = writer.write(&file, m_document);
	} else if (m_type == "rtf") {
		file.setTextModeEnabled(true);
		RtfWriter writer;
		saved = writer.write(&file, m_document);
	} else {
		file.setTextModeEnabled(true);
		QTextStream stream(&file);
		if (m_write_bom) {
			stream.setGenerateByteOrderMark(true);
		}
		stream << m_document->toPlainText();
		saved = stream.status() == QTextStream::Ok;
	}

	saved &= file.flush();
	int ret;
	do {
#ifdef Q_OS_WIN
		ret = _commit(file.handle());
#else
		ret = fsync(file.handle());
#endif
	} while ((ret == -1) && (errno == EINTR));
	saved &= (ret == 0);
	if (!saved) { file.cancelWriting(); return false; }
	return file.commit();
}

//-----------------------------------------------------------------------------
