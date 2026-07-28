/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_THEME_PALETTE_H
#define FAIRYWRITER_THEME_PALETTE_H

#include <QColor>

struct ThemePalette
{
	QColor background;
	QColor paper;
	QColor deep;
	QColor primary;
	QColor secondary;
	QColor accent;
	QColor ui_text;
	QColor document_text;
	QColor misspelled;

	bool operator==(const ThemePalette& other) const
	{
		return background == other.background && paper == other.paper && deep == other.deep
			&& primary == other.primary && secondary == other.secondary && accent == other.accent
			&& ui_text == other.ui_text && document_text == other.document_text
			&& misspelled == other.misspelled;
	}
};

#endif // FAIRYWRITER_THEME_PALETTE_H
