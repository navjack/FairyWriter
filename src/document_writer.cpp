/*
	SPDX-FileCopyrightText: 2012 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "document_writer.h"

#include "docx_writer.h"
#include "odt_writer.h"
#include "rtf_writer.h"

#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextDocument>
#include <QTextStream>
#include <QUuid>

#include <errno.h>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <fcntl.h>
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

namespace {
bool syncHandle(qintptr handle)
{
	int ret;
	do {
#ifdef Q_OS_WIN
		ret = _commit(static_cast<int>(handle));
#else
		ret = fsync(static_cast<int>(handle));
#endif
	} while ((ret == -1) && (errno == EINTR));
	return ret == 0;
}

bool syncDirectory(const QString& path)
{
#ifdef Q_OS_WIN
	Q_UNUSED(path);
	return true;
#else
	const QByteArray encoded = QFile::encodeName(path);
	int descriptor;
	do {
		descriptor = ::open(encoded.constData(), O_RDONLY);
	} while (descriptor == -1 && errno == EINTR);
	if (descriptor == -1) return false;
	const bool synced = syncHandle(descriptor);
	int close_result;
	do {
		close_result = ::close(descriptor);
	} while (close_result == -1 && errno == EINTR);
	return synced && close_result == 0;
#endif
}
}

bool DocumentWriter::write(WriteMode mode)
{
	Q_ASSERT(m_document);
	Q_ASSERT(!m_filename.isEmpty());

	const auto writeDocument = [this](QIODevice& device) {
		bool saved = false;
		if (m_type == "odt") {
			OdtWriter writer;
			saved = writer.write(&device, m_document);
		} else if (m_type == "fodt") {
			OdtWriter writer;
			writer.setFlatXML(true);
			saved = writer.write(&device, m_document);
		} else if (m_type == "docx") {
			DocxWriter writer;
			saved = writer.write(&device, m_document);
		} else if (m_type == "rtf") {
			RtfWriter writer;
			saved = writer.write(&device, m_document);
		} else if (m_type == "md" || m_type == "markdown") {
			const QByteArray source = m_markdown_source.isNull()
				? m_document->toMarkdown(QTextDocument::MarkdownDialectGitHub).toUtf8()
				: m_markdown_source;
			saved = device.write(source) == source.size();
		} else {
			QTextStream stream(&device);
			if (m_write_bom) {
				stream.setGenerateByteOrderMark(true);
			}
			stream << m_document->toPlainText();
			saved = stream.status() == QTextStream::Ok;
		}
		return saved;
	};

	if (mode == WriteMode::CreateNew) {
		const QFileInfo target(m_filename);
		if (target.exists() || !target.dir().exists()) return false;
		const QString temporary = target.dir().filePath(
			QStringLiteral(".%1.fairywriter-%2.tmp")
				.arg(target.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces)));
		QFile file(temporary);
		if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
		const bool saved = writeDocument(file)
			&& file.flush()
			&& syncHandle(file.handle());
		file.close();
		if (!saved) {
			QFile::remove(temporary);
			return false;
		}
		if (m_precommit_check && !m_precommit_check()) {
			QFile::remove(temporary);
			return false;
		}
		if (!QFile::rename(temporary, m_filename)) {
			QFile::remove(temporary);
			return false;
		}
		return syncDirectory(target.absolutePath());
	}

	QSaveFile file(m_filename);
	// Never fall back to truncating the destination in place. Filesystems that
	// cannot provide an atomic replacement must fail while leaving the previous
	// document intact.
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
	bool saved = writeDocument(file);
	saved &= file.flush();
	saved &= syncHandle(file.handle());
	if (!saved) {
		file.cancelWriting();
		return false;
	}
	// Encoding can be long enough for another process to replace the primary
	// after the coordinator's initial fingerprint check. Recheck after the
	// complete temporary payload is synced and immediately before QSaveFile's
	// atomic rename. A failed check cancels the staged file and never touches
	// the primary.
	if (m_precommit_check && !m_precommit_check()) {
		file.cancelWriting();
		return false;
	}
	if (!file.commit()) return false;
	return syncDirectory(QFileInfo(m_filename).absolutePath());
}

//-----------------------------------------------------------------------------
