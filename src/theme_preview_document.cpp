/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "theme_preview_document.h"

#include "snes_style.h"
#include "theme.h"

#include <QGridLayout>
#include <QPalette>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <algorithm>

ThemePreviewDocument::ThemePreviewDocument(QWidget* parent)
	: QWidget(parent)
{
	m_text = new QTextEdit(this);
	m_text->setReadOnly(true);
	m_text->setFrameStyle(QFrame::NoFrame);
	m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_text->setAutoFillBackground(false);
	m_layout = new QGridLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);
	m_layout->addWidget(m_text, 1, 1);
}

void ThemePreviewDocument::cloneFrom(const QTextDocument* source)
{
	QTextDocument* clone = source ? source->clone(m_text) : new QTextDocument(m_text);
	m_text->setDocument(clone);
	m_text->moveCursor(QTextCursor::Start);
}

void ThemePreviewDocument::applyTheme(const Theme& theme, int virtual_pixel_pitch)
{
	const int pitch = std::max(1, virtual_pixel_pitch);
	QTextDocument* document = m_text->document();
	document->blockSignals(true);

	QPalette palette = m_text->palette();
	palette.setBrush(QPalette::Base, Qt::transparent);
	palette.setColor(QPalette::Text, theme.textColor());
	palette.setColor(QPalette::Highlight, theme.textColor());
	palette.setColor(QPalette::HighlightedText, theme.foregroundColor());
	m_text->setPalette(palette);

	QTextBlockFormat format;
	format.setLineHeight(theme.lineSpacing(), theme.lineSpacing() == 100
		? QTextBlockFormat::SingleHeight : QTextBlockFormat::ProportionalHeight);
	format.setTextIndent(theme.tabWidth() * pitch * theme.indentFirstLine());
	format.setTopMargin(theme.spacingAboveParagraph() * pitch);
	format.setBottomMargin(theme.spacingBelowParagraph() * pitch);
	for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
		QTextCursor cursor(block);
		cursor.mergeBlockFormat(format);
	}
	m_text->setTabStopDistance(theme.tabWidth() * pitch);
	document->setIndentWidth(theme.tabWidth() * pitch);
	m_text->setFont(SnesStyle::scaledDocumentFont(theme.textFont(), theme.documentFontPixels(), pitch));

	const int padding = theme.foregroundPadding() * pitch;
	m_layout->setRowMinimumHeight(0, padding);
	m_layout->setRowMinimumHeight(2, padding);
	m_layout->setColumnMinimumWidth(0, padding);
	m_layout->setColumnMinimumWidth(2, padding);
	document->blockSignals(false);
}
