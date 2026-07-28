/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FOCUSWRITER_THEME_MANAGER_H
#define FOCUSWRITER_THEME_MANAGER_H

class Theme;
class ThemePreview;

#include <QDialog>
class QSettings;
class QComboBox;
class QPushButton;
class QAction;

class ThemeManager : public QDialog
{
	Q_OBJECT

public:
	explicit ThemeManager(QSettings& settings, QWidget* parent = nullptr);

Q_SIGNALS:
	void themeSelected(const Theme& theme);
	void editRequested(const Theme& theme, bool is_new);

protected:
	void hideEvent(QHideEvent* event) override;

private Q_SLOTS:
	void newTheme();
	void editTheme();
	void cloneTheme();
	void deleteTheme();
	void importTheme();
	void exportTheme();

private:
	int addItem(const QString& id, bool is_default, const QString& name);
	void scheduleIconGeneration(const QString& id, bool is_default);
	bool selectItem(const QString& id, bool is_default);
	void currentThemeChanged();
	bool currentThemeIsDefault() const;
	void updateActions();

private:
	QComboBox* m_themes;
	ThemePreview* m_preview;
	QPushButton* m_new;
	QPushButton* m_edit;
	QPushButton* m_duplicate;
	QPushButton* m_more;
	QAction* m_delete;
	QAction* m_import;
	QAction* m_export;
	QSettings& m_settings;
};

#endif // FOCUSWRITER_THEME_MANAGER_H
