/*
	SPDX-FileCopyrightText: 2008 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "preferences_dialog.h"

#include "action_manager.h"
#include "daily_progress.h"
#include "dictionary_manager.h"
#include "format_manager.h"
#include "locale_dialog.h"
#include "preferences.h"
#include "shortcut_edit.h"
#include "smart_quotes.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtZipReader>

//-----------------------------------------------------------------------------

PreferencesDialog::PreferencesDialog(DailyProgress* daily_progress, QWidget* parent)
	: QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_daily_progress(daily_progress)
	, m_shortcut_conflicts(false)
	, m_shortcuts_page_index(-1)
{
	setWindowTitle(tr("Preferences"));
	setProperty("fairywriter.panelFill", true);

	m_category = new QComboBox(this);
	m_category->setObjectName(QStringLiteral("FairyWriterPreferencesCategory"));
	m_category->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_category->setMinimumContentsLength(8);
	m_pages = new QStackedWidget(this);
	m_pages->setObjectName(QStringLiteral("FairyWriterPreferencesPages"));
	m_pages->setMinimumSize(0, 0);
	m_pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	initGeneralPages();
	initDailyGoalPages();
	initStatisticsPages();
	initSpellingPages();
	initToolbarPages();
	initShortcutsPage();
	connect(m_category, &QComboBox::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
	buttons->setMinimumHeight(buttons->sizeHint().height());
	connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(m_category);
	layout->addWidget(m_pages, 1);
	layout->addWidget(buttons);

	// Load settings
	switch (Preferences::instance().goalType()) {
	case 1:
		m_option_time->setChecked(true);
		break;
	case 2:
		m_option_wordcount->setChecked(true);
		break;
	default:
		m_option_none->setChecked(true);
		break;
	}
	m_time->setValue(Preferences::instance().goalMinutes());
	m_wordcount->setValue(Preferences::instance().goalWords());

	m_goal_history->setChecked(Preferences::instance().goalHistory());
	m_goal_streaks->setChecked(Preferences::instance().goalStreaks());
	m_streak_minimum->setValue(Preferences::instance().goalStreakMinimum());

	m_show_characters->setChecked(Preferences::instance().showCharacters());
	m_show_pages->setChecked(Preferences::instance().showPages());
	m_show_paragraphs->setChecked(Preferences::instance().showParagraphs());
	m_show_words->setChecked(Preferences::instance().showWords());

	switch (Preferences::instance().pageType()) {
	case 1:
		m_option_paragraphs->setChecked(true);
		break;
	case 2:
		m_option_words->setChecked(true);
		break;
	default:
		m_option_characters->setChecked(true);
		break;
	}
	m_page_characters->setValue(Preferences::instance().pageCharacters());
	m_page_paragraphs->setValue(Preferences::instance().pageParagraphs());
	m_page_words->setValue(Preferences::instance().pageWords());

	switch (Preferences::instance().wordcountType()) {
	case 1:
		m_option_estimate_wordcount->setChecked(true);
		break;
	case 2:
		m_option_singlechar_wordcount->setChecked(true);
		break;
	default:
		m_option_accurate_wordcount->setChecked(true);
		break;
	}

	m_always_center->setChecked(Preferences::instance().alwaysCenter());
	m_block_cursor->setChecked(Preferences::instance().blockCursor());
	m_smooth_fonts->setChecked(Preferences::instance().smoothFonts());
	m_smart_quotes->setChecked(Preferences::instance().smartQuotes());
	m_double_quotes->setCurrentIndex(Preferences::instance().doubleQuotes());
	m_single_quotes->setCurrentIndex(Preferences::instance().singleQuotes());
#ifndef __OS2__
	m_typewriter_sounds->setChecked(Preferences::instance().typewriterSounds());
#endif

	m_scene_divider->setText(Preferences::instance().sceneDivider());

	m_save_positions->setChecked(Preferences::instance().savePositions());
	m_save_format->setCurrentIndex(m_save_format->findData(Preferences::instance().saveFormat().value()));
	m_write_bom->setChecked(Preferences::instance().writeByteOrderMark());

	m_always_show_scrollbar->setChecked(Preferences::instance().alwaysShowScrollBar());
	m_always_show_header->setChecked(Preferences::instance().alwaysShowHeader());
	m_always_show_footer->setChecked(Preferences::instance().alwaysShowFooter());
	m_wallpaper_motion->setChecked(Preferences::instance().wallpaperMotion());

	m_highlight_misspelled->setChecked(Preferences::instance().highlightMisspelled());
	m_ignore_numbers->setChecked(Preferences::instance().ignoredWordsWithNumbers());
	m_ignore_uppercase->setChecked(Preferences::instance().ignoredUppercaseWords());
	const int index = m_languages->findData(Preferences::instance().language());
	if (index != -1) {
		m_languages->setCurrentIndex(index);
	}

	int style = m_toolbar_style->findData(Preferences::instance().toolbarStyle());
	if (style == -1) {
		style = m_toolbar_style->findData(Qt::ToolButtonTextUnderIcon);
	}
	m_toolbar_style->setCurrentIndex(style);
	const QStringList actions = Preferences::instance().toolbarActions();
	int pos = 0;
	for (const QString& action : actions) {
		QString text = action;
		const bool checked = !text.startsWith("^");
		if (!checked) {
			text.remove(0, 1);
		}

		QListWidgetItem* item = nullptr;
		if (text != "|") {
			for (int i = pos, count = m_toolbar_actions->count(); i < count; ++i) {
				if (m_toolbar_actions->item(i)->data(Qt::UserRole).toString() == text) {
					item = m_toolbar_actions->takeItem(i);
					break;
				}
			}
		} else if (checked) {
			item = new QListWidgetItem(QString(20, QChar('-')));
			item->setData(Qt::UserRole, "|");
		}

		if (item) {
			item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
			m_toolbar_actions->insertItem(pos, item);
			pos++;
		}
	}
	m_toolbar_actions->setCurrentRow(0);

	resize(QSettings().value("Preferences/Size", QSize(650, 560)).toSize());
}

//-----------------------------------------------------------------------------

PreferencesDialog::~PreferencesDialog()
{
	QSettings().setValue("Preferences/Size", size());
}

//-----------------------------------------------------------------------------

void PreferencesDialog::accept()
{
	// Confirm close even with shortcut conflicts
	if (m_shortcut_conflicts) {
		m_category->setCurrentIndex(m_shortcuts_page_index);
		if (QMessageBox::question(this,
				tr("Question"),
				tr("One or more shortcuts conflict. Do you wish to proceed?"),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No) {
			return;
		}
	}

	// Save settings
	if (m_option_time->isChecked()) {
		Preferences::instance().setGoalType(1);
	} else if (m_option_wordcount->isChecked()) {
		Preferences::instance().setGoalType(2);
	} else {
		Preferences::instance().setGoalType(0);
	}
	Preferences::instance().setGoalMinutes(m_time->value());
	Preferences::instance().setGoalWords(m_wordcount->value());
	Preferences::instance().setGoalHistory(m_goal_history->isChecked());
	Preferences::instance().setGoalStreaks(m_goal_streaks->isChecked());
	Preferences::instance().setGoalStreakMinimum(m_streak_minimum->value());

	Preferences::instance().setShowCharacters(m_show_characters->isChecked());
	Preferences::instance().setShowPages(m_show_pages->isChecked());
	Preferences::instance().setShowParagraphs(m_show_paragraphs->isChecked());
	Preferences::instance().setShowWords(m_show_words->isChecked());

	if (m_option_paragraphs->isChecked()) {
		Preferences::instance().setPageType(1);
	} else if (m_option_words->isChecked()) {
		Preferences::instance().setPageType(2);
	} else {
		Preferences::instance().setPageType(0);
	}
	Preferences::instance().setPageCharacters(m_page_characters->value());
	Preferences::instance().setPageParagraphs(m_page_paragraphs->value());
	Preferences::instance().setPageWords(m_page_words->value());

	if (m_option_accurate_wordcount->isChecked()) {
		Preferences::instance().setWordcountType(0);
	} else if (m_option_estimate_wordcount->isChecked()) {
		Preferences::instance().setWordcountType(1);
	} else {
		Preferences::instance().setWordcountType(2);
	}

	Preferences::instance().setAlwaysCenter(m_always_center->isChecked());
	Preferences::instance().setBlockCursor(m_block_cursor->isChecked());
	Preferences::instance().setSmoothFonts(m_smooth_fonts->isChecked());
	Preferences::instance().setSmartQuotes(m_smart_quotes->isChecked());
	Preferences::instance().setDoubleQuotes(m_double_quotes->currentIndex());
	Preferences::instance().setSingleQuotes(m_single_quotes->currentIndex());
	Preferences::instance().setTypewriterSounds(m_typewriter_sounds->isChecked());

	Preferences::instance().setSceneDivider(m_scene_divider->text());

	Preferences::instance().setSavePositions(m_save_positions->isChecked());
	Preferences::instance().setWriteByteOrderMark(m_write_bom->isChecked());
	Preferences::instance().setSaveFormat(m_save_format->itemData(m_save_format->currentIndex()).toString());

	Preferences::instance().setAlwaysShowScrollbar(m_always_show_scrollbar->isChecked());
	Preferences::instance().setAlwaysShowHeader(m_always_show_header->isChecked());
	Preferences::instance().setAlwaysShowFooter(m_always_show_footer->isChecked());
	Preferences::instance().setWallpaperMotion(m_wallpaper_motion->isChecked());

	Preferences::instance().setToolbarStyle(m_toolbar_style->itemData(m_toolbar_style->currentIndex()).toInt());
	QStringList actions;
	for (int i = 0, count = m_toolbar_actions->count(); i < count; ++i) {
		const QListWidgetItem* item = m_toolbar_actions->item(i);
		const QString action = (item->checkState() == Qt::Unchecked ? "^" : QString()) + item->data(Qt::UserRole).toString();
		if (action != "^|") {
			actions.append(action);
		}
	}
	Preferences::instance().setToolbarActions(actions);

	ActionManager::instance()->setShortcuts(m_new_shortcuts);

	// Uninstall languages
	for (const QString& language : std::as_const(m_uninstalled)) {
		QFile::remove("dict:" + language + ".aff");
		QFile::remove("dict:" + language + ".dic");
	}

	// Install languages
	const QString path = DictionaryManager::path() + "/install/";
	const QString new_path = DictionaryManager::installedPath() + "/";
	QDir dir(path);
	const QStringList files = dir.entryList(QDir::Files);
	for (const QString& file : files) {
		QFile::remove(new_path + file);
		QFile::rename(path + file, new_path + file);
	}
	dir.cdUp();
	dir.rmdir("install");

	// Set dictionary
	Preferences::instance().setHighlightMisspelled(m_highlight_misspelled->isChecked());
	Preferences::instance().setIgnoreWordsWithNumbers(m_ignore_numbers->isChecked());
	Preferences::instance().setIgnoreUppercaseWords(m_ignore_uppercase->isChecked());
	if (m_languages->count()) {
		Preferences::instance().setLanguage(m_languages->itemData(m_languages->currentIndex()).toString());
	} else {
		Preferences::instance().setLanguage(QString());
	}

	Preferences::instance().saveChanges();

	// Save personal dictionary
	QStringList words;
	for (int i = 0, count = m_personal_dictionary->count(); i < count; ++i) {
		words.append(m_personal_dictionary->item(i)->text());
	}
	DictionaryManager::instance().setPersonal(words);

	QDialog::accept();
}

//-----------------------------------------------------------------------------

void PreferencesDialog::reject()
{
	if (!QDir(DictionaryManager::path() + "/install/").removeRecursively()) {
		qWarning("Failed to clean up dictionary install path");
	}
	QDialog::reject();
}

//-----------------------------------------------------------------------------

void PreferencesDialog::goalHistoryToggled()
{
	m_goal_streaks->setEnabled(m_goal_history->isChecked());
	m_streak_minimum->setEnabled(m_goal_streaks->isChecked() && m_goal_streaks->isEnabled());
	m_streak_minimum_label->setEnabled(m_goal_streaks->isChecked() && m_goal_streaks->isEnabled());
}

//-----------------------------------------------------------------------------

void PreferencesDialog::resetDailyGoal()
{
	if (QMessageBox::question(this,
			tr("Question"),
			tr("Reset daily progress for today to zero?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
		m_daily_progress->resetToday();
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::moveActionUp()
{
	const int from = m_toolbar_actions->currentRow();
	const int to = from - 1;
	if (from > 0) {
		m_toolbar_actions->insertItem(to, m_toolbar_actions->takeItem(from));
		m_toolbar_actions->setCurrentRow(to);
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::moveActionDown()
{
	const int from = m_toolbar_actions->currentRow();
	const int to = from + 1;
	if (to < m_toolbar_actions->count()) {
		m_toolbar_actions->insertItem(to, m_toolbar_actions->takeItem(from));
		m_toolbar_actions->setCurrentRow(to);
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::addSeparatorAction()
{
	QListWidgetItem* item = new QListWidgetItem(QString(20, QChar('-')));
	item->setCheckState(Qt::Checked);
	item->setData(Qt::UserRole, "|");
	m_toolbar_actions->insertItem(m_toolbar_actions->currentRow(), item);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::currentActionChanged(int action)
{
	if (action != -1) {
		m_move_up_button->setEnabled(action > 0);
		m_move_down_button->setEnabled((action + 1) < m_toolbar_actions->count());
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::addLanguage()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Select Dictionary"), QDir::homePath());
	if (path.isEmpty()) {
		return;
	}

	// File lists
	QStringList aff_files;
	QStringList dic_files;
	QStringList files;
	QStringList dictionaries;

	// Open archive
	QtZipReader zip(path);
	if (!zip.isReadable()) {
		QMessageBox::warning(this, tr("Sorry"), tr("Unable to open archive."));
		return;
	}

	// List files
	const QStringList entries = zip.fileList();
	for (const QString& name : entries) {
		if (name.endsWith(".aff")) {
			aff_files += name;
		} else if (name.endsWith(".dic")) {
			dic_files += name;
		}
	}

	// Find Hunspell dictionary files
	for (const QString& dic : std::as_const(dic_files)) {
		QString aff = dic;
		aff.replace(".dic", ".aff");
		if (aff_files.contains(aff)) {
			files += dic;
			files += aff;
			QString dictionary = dic.section('/', -1);
			dictionary.chop(4);
			dictionaries += dictionary;
		}
	}

	// Check for dictionaries
	if (!dictionaries.isEmpty()) {
		// Extract files
		const QDir dir(DictionaryManager::path());
		dir.mkdir("install");
		const QString install = dir.absoluteFilePath("install") + "/";
		for (const QString& file : std::as_const(files)) {
			// Ignore path for Hunspell dictionaries
			QString filename = file;
			filename = filename.section('/', -1);
			filename.replace(QChar('-'), QChar('_'));

			QFile out(install + filename);
			if (out.open(QIODevice::WriteOnly)) {
				out.write(zip.fileData(file));
			}
		}

		// Add to language selection
		const QString dictionary_path = DictionaryManager::path() + "/install/";
		const QString dictionary_new_path = DictionaryManager::installedPath() + "/";
		for (const QString& dictionary : std::as_const(dictionaries)) {
			QString language = dictionary;
			language.replace(QChar('-'), QChar('_'));
			const QString name = LocaleDialog::languageName(language);

			// Prompt user about replacing duplicate languages
			const QString aff_file = dictionary_path + dictionary + ".aff";
			const QString dic_file = dictionary_path + dictionary + ".dic";
			const QString new_aff_file = dictionary_new_path + language + ".aff";
			const QString new_dic_file = dictionary_new_path + language + ".dic";

			if (!m_uninstalled.contains(language) && (QFile::exists(new_aff_file) || QFile::exists(new_dic_file))) {
				if (QMessageBox::question(this,
						tr("Question"),
						tr("The dictionary \"%1\" already exists. Do you want to replace it?").arg(name),
						QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No) {
					QFile::remove(aff_file);
					QFile::remove(dic_file);
				}
				continue;
			}

			m_languages->addItem(name, language);
			m_languages->setCurrentIndex(m_languages->count() - 1);
		}
		m_languages->model()->sort(0);
	} else {
		QMessageBox::warning(this, tr("Sorry"), tr("The archive does not contain a usable dictionary."));
	}

	// Close archive
	zip.close();
}

//-----------------------------------------------------------------------------

void PreferencesDialog::removeLanguage()
{
	const int index = m_languages->currentIndex();
	if (index == -1) {
		return;
	}
	if (QMessageBox::question(this,
			tr("Question"),
			tr("Remove current dictionary?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
		m_uninstalled.append(m_languages->itemData(index).toString());
		m_languages->removeItem(index);
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::selectedLanguageChanged(int index)
{
	if (index != -1) {
		const QFileInfo info("dict:" + m_languages->itemData(index).toString() + ".dic");
		m_remove_language_button->setEnabled(info.absoluteFilePath().startsWith(DictionaryManager::installedPath()));
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::addWord()
{
	const QString word = m_word->text();
	m_word->clear();
	int row;
	for (row = 0; row < m_personal_dictionary->count(); ++row) {
		if (m_personal_dictionary->item(row)->text().localeAwareCompare(word) > 0) {
			break;
		}
	}
	m_personal_dictionary->insertItem(row, word);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::removeWord()
{
	delete m_personal_dictionary->selectedItems().constFirst();
	m_personal_dictionary->clearSelection();
}

//-----------------------------------------------------------------------------

void PreferencesDialog::selectedWordChanged()
{
	m_remove_word_button->setDisabled(m_personal_dictionary->selectedItems().isEmpty());
}

//-----------------------------------------------------------------------------

void PreferencesDialog::wordEdited()
{
	const QString word = m_word->text();
	m_add_word_button->setEnabled(!word.isEmpty() && m_personal_dictionary->findItems(word, Qt::MatchExactly).isEmpty());
}

//-----------------------------------------------------------------------------

void PreferencesDialog::selectedShortcutChanged()
{
	m_shortcut_edit->setEnabled(m_shortcuts->currentItem());
	if (!m_shortcuts->currentItem()) {
		m_shortcut_edit->blockSignals(true);
		m_shortcut_edit->setShortcut(QKeySequence(), QKeySequence());
		m_shortcut_edit->blockSignals(false);
		return;
	}

	// Set shortcut in editor
	const QString name = m_shortcuts->currentItem()->text(2);
	const QKeySequence shortcut = m_new_shortcuts.value(name, ActionManager::instance()->shortcut(name));
	m_shortcut_edit->blockSignals(true);
	m_shortcut_edit->setShortcut(shortcut, ActionManager::instance()->defaultShortcut(name));
	m_shortcut_edit->blockSignals(false);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::shortcutChanged()
{
	if (!m_shortcuts->currentItem()) {
		return;
	}

	// Find old shortcut
	const QString name = m_shortcuts->currentItem()->text(2);
	const QKeySequence old_shortcut = m_new_shortcuts.value(name, ActionManager::instance()->shortcut(name));
	const QKeySequence shortcut = m_shortcut_edit->shortcut();
	if (shortcut == old_shortcut) {
		return;
	}

	// Update shortcut
	m_new_shortcuts[name] = shortcut;
	m_shortcuts->currentItem()->setText(1, shortcut.toString(QKeySequence::NativeText));
	highlightShortcutConflicts();
}

//-----------------------------------------------------------------------------

void PreferencesDialog::shortcutDoubleClicked()
{
	m_shortcut_edit->setFocus();
}

//-----------------------------------------------------------------------------

void PreferencesDialog::highlightShortcutConflicts()
{
	m_shortcut_conflicts = false;
	QFont conflict = font();
	conflict.setBold(true);

	QHash<QKeySequence, QTreeWidgetItem*> shortcuts;
	for (int i = 0, count = m_shortcuts->topLevelItemCount(); i < count; ++i) {
		// Reset font and highlight
		QTreeWidgetItem* item = m_shortcuts->topLevelItem(i);
		item->setForeground(1, palette().windowText());
		item->setFont(1, font());

		// Find shortcut
		const QString name = item->text(2);
		const QKeySequence shortcut = m_new_shortcuts.value(name, ActionManager::instance()->shortcut(name));
		if (shortcut.isEmpty() || (shortcut == Qt::Key_unknown)) {
			continue;
		}

		// Highlight conflict
		if (shortcuts.contains(shortcut)) {
			m_shortcut_conflicts = true;
			item->setForeground(1, Qt::red);
			item->setFont(1,conflict);
			shortcuts[shortcut]->setForeground(1, Qt::red);
			shortcuts[shortcut]->setFont(1, conflict);
		}
		shortcuts[shortcut] = item;
	}
}

//-----------------------------------------------------------------------------

void PreferencesDialog::addPage(const QString& name, QWidget* page)
{
	page->setObjectName(QStringLiteral("FairyWriterPreferencesPage_%1").arg(m_pages->count()));
	m_category->addItem(name);
	m_pages->addWidget(page);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::initGeneralPages()
{
	QWidget* editing_page = new QWidget(this);

	// Create edit options
	QGroupBox* edit_group = new QGroupBox(tr("Editing"), editing_page);

	m_always_center = new QCheckBox(tr("Always vertically center"), edit_group);
	m_block_cursor = new QCheckBox(tr("Block insertion cursor"), edit_group);
	m_smooth_fonts = new QCheckBox(tr("Smooth fonts"), edit_group);
	m_typewriter_sounds = new QCheckBox(tr("Typewriter sounds"), edit_group);
#ifdef __OS2__
	m_typewriter_sounds->setEnabled(false);
#endif

	QVBoxLayout* edit_layout = new QVBoxLayout(edit_group);
	edit_layout->addWidget(m_always_center);
	edit_layout->addWidget(m_block_cursor);
	edit_layout->addWidget(m_smooth_fonts);
	edit_layout->addWidget(m_typewriter_sounds);
	QVBoxLayout* editing_layout = new QVBoxLayout(editing_page);
	editing_layout->addWidget(edit_group);
	editing_layout->addStretch();
	addPage(tr("Editing"), editing_page);

	// Smart quotes need two full-width selectors. Keeping them on a separate
	// page prevents the wide pixel font from competing with unrelated editing
	// controls in a Steam Deck-height panel.
	QWidget* quotes_page = new QWidget(this);
	QGroupBox* quotes_group = new QGroupBox(tr("Smart Quotes"), quotes_page);
	m_smart_quotes = new QCheckBox(tr("Use smart quotes"), quotes_group);
	m_double_quotes = new QComboBox(quotes_group);
	m_double_quotes->setEnabled(false);
	m_single_quotes = new QComboBox(quotes_group);
	m_single_quotes->setEnabled(false);
	const int count = SmartQuotes::count();
	for (int i = 0; i < count; ++i) {
		m_double_quotes->addItem(SmartQuotes::quoteString(tr("Double"), i));
		m_single_quotes->addItem(SmartQuotes::quoteString(tr("Single"), i));
	}
	m_double_quotes->setMaxVisibleItems(count);
	m_single_quotes->setMaxVisibleItems(count);
	connect(m_smart_quotes, &QCheckBox::toggled, m_double_quotes, &QComboBox::setEnabled);
	connect(m_smart_quotes, &QCheckBox::toggled, m_single_quotes, &QComboBox::setEnabled);

	QFormLayout* quotes_layout = new QFormLayout;
	quotes_layout->addRow(tr("Double:"), m_double_quotes);
	quotes_layout->addRow(tr("Single:"), m_single_quotes);
	QVBoxLayout* quotes_group_layout = new QVBoxLayout(quotes_group);
	quotes_group_layout->addWidget(m_smart_quotes);
	quotes_group_layout->addLayout(quotes_layout);
	QVBoxLayout* quotes_page_layout = new QVBoxLayout(quotes_page);
	quotes_page_layout->addWidget(quotes_group);
	quotes_page_layout->addStretch();
	addPage(tr("Smart Quotes"), quotes_page);

	// Create section options
	QWidget* scenes_page = new QWidget(this);
	QGroupBox* scene_group = new QGroupBox(tr("Scenes"), scenes_page);

	m_scene_divider = new QLineEdit(scene_group);

	QFormLayout* scene_layout = new QFormLayout(scene_group);
	scene_layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	scene_layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
	scene_layout->addRow(tr("Divider:"), m_scene_divider);
	QVBoxLayout* scenes_layout = new QVBoxLayout(scenes_page);
	scenes_layout->addWidget(scene_group);
	scenes_layout->addStretch();
	addPage(tr("Scenes"), scenes_page);

	// Create save options
	QWidget* saving_page = new QWidget(this);
	QGroupBox* save_group = new QGroupBox(tr("Saving"), saving_page);

	m_save_positions = new QCheckBox(tr("Remember cursor"), save_group);
	m_save_positions->setToolTip(tr("Remember cursor position"));
	m_write_bom = new QCheckBox(tr("Text byte order mark"), save_group);
	m_write_bom->setToolTip(tr("Write byte order mark in plain text files"));

	QLabel* save_format_label = new QLabel(tr("Default format:"), save_group);
	m_save_format = new QComboBox(save_group);
	const QStringList types = Preferences::instance().saveFormat().allowedValues();
	for (const QString& type : types) {
		const QString full_name = FormatManager::filter(type);
		m_save_format->addItem(full_name.section(QLatin1String(" ("), 0, 0), type);
		m_save_format->setItemData(m_save_format->count() - 1, full_name, Qt::ToolTipRole);
	}

	QVBoxLayout* save_format_layout = new QVBoxLayout;
	save_format_layout->setContentsMargins(0, 0, 0, 0);
	save_format_layout->addWidget(save_format_label);
	save_format_layout->addWidget(m_save_format);

	QVBoxLayout* save_layout = new QVBoxLayout(save_group);
	save_layout->addWidget(m_save_positions);
	save_layout->addWidget(m_write_bom);
	save_layout->addLayout(save_format_layout);
	QVBoxLayout* saving_layout = new QVBoxLayout(saving_page);
	saving_layout->addWidget(save_group);
	saving_layout->addStretch();
	addPage(tr("Saving"), saving_page);

	// Create view options
	QWidget* interface_page = new QWidget(this);
	QGroupBox* view_group = new QGroupBox(tr("User Interface"), interface_page);

	m_always_show_scrollbar = new QCheckBox(tr("Always show scrollbar"), view_group);
	m_always_show_header = new QCheckBox(tr("Always show top bar"), view_group);
	m_always_show_footer = new QCheckBox(tr("Always show bottom bar"), view_group);
	m_wallpaper_motion = new QCheckBox(tr("Animate wallpaper"), view_group);

	QVBoxLayout* view_layout = new QVBoxLayout(view_group);
	view_layout->addWidget(m_always_show_scrollbar);
	view_layout->addWidget(m_always_show_header);
	view_layout->addWidget(m_always_show_footer);
	view_layout->addWidget(m_wallpaper_motion);
	QVBoxLayout* interface_layout = new QVBoxLayout(interface_page);
	interface_layout->addWidget(view_group);
	interface_layout->addStretch();
	addPage(tr("Interface"), interface_page);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::initDailyGoalPages()
{
	QWidget* goal_page = new QWidget(this);

	// Create goal options
	m_option_none = new QRadioButton(tr("None"), goal_page);

	m_option_time = new QRadioButton(tr("Minutes:"), goal_page);
	m_time = new QSpinBox(goal_page);
	m_time->setCorrectionMode(QSpinBox::CorrectToNearestValue);
	m_time->setRange(Preferences::instance().goalMinutes().minimumValue(), Preferences::instance().goalMinutes().maximumValue());
	m_time->setSingleStep(5);
	m_time->setEnabled(false);

	m_option_wordcount = new QRadioButton(tr("Words:"), goal_page);
	m_wordcount = new QSpinBox(goal_page);
	m_wordcount->setCorrectionMode(QSpinBox::CorrectToNearestValue);
	m_wordcount->setRange(Preferences::instance().goalWords().minimumValue(), Preferences::instance().goalWords().maximumValue());
	m_wordcount->setSingleStep(100);
	m_wordcount->setEnabled(false);

	connect(m_option_none, &QRadioButton::toggled, m_time, &QSpinBox::setDisabled);
	connect(m_option_none, &QRadioButton::toggled, m_wordcount, &QSpinBox::setDisabled);

	connect(m_option_time, &QRadioButton::toggled, m_time, &QSpinBox::setEnabled);
	connect(m_option_time, &QRadioButton::toggled, m_wordcount, &QSpinBox::setDisabled);

	connect(m_option_wordcount, &QRadioButton::toggled, m_time, &QSpinBox::setDisabled);
	connect(m_option_wordcount, &QRadioButton::toggled, m_wordcount, &QSpinBox::setEnabled);

	QPushButton* reset_today_button = new QPushButton(tr("Reset Today"), goal_page);
	connect(reset_today_button, &QPushButton::clicked, this, &PreferencesDialog::resetDailyGoal);

	QGridLayout* goal_layout = new QGridLayout;
	goal_layout->setColumnStretch(2, 1);
	goal_layout->addWidget(m_option_none, 0, 0);
	goal_layout->addWidget(m_option_time, 1, 0);
	goal_layout->addWidget(m_time, 1, 1);
	goal_layout->addWidget(m_option_wordcount, 2, 0);
	goal_layout->addWidget(m_wordcount, 2, 1);
	goal_layout->addWidget(reset_today_button, 3, 0, 1, 2, Qt::AlignLeft | Qt::AlignVCenter);
	QVBoxLayout* goal_page_layout = new QVBoxLayout(goal_page);
	goal_page_layout->addLayout(goal_layout);
	goal_page_layout->addStretch();
	addPage(tr("Daily Goal"), goal_page);

	// Create history options
	QWidget* history_page = new QWidget(this);
	QGroupBox* history_group = new QGroupBox(tr("History"), history_page);

	m_goal_history = new QCheckBox(tr("Remember history"), history_group);
	connect(m_goal_history, &QCheckBox::toggled, this, &PreferencesDialog::goalHistoryToggled);

	m_goal_streaks = new QCheckBox(tr("Show streaks"), history_group);
	m_goal_streaks->setEnabled(false);
	connect(m_goal_streaks, &QCheckBox::toggled, this, &PreferencesDialog::goalHistoryToggled);

	m_streak_minimum = new QSpinBox(history_group);
	m_streak_minimum->setCorrectionMode(QSpinBox::CorrectToNearestValue);
	m_streak_minimum->setRange(Preferences::instance().goalStreakMinimum().minimumValue(), Preferences::instance().goalStreakMinimum().maximumValue());
	m_streak_minimum->setSuffix(QLocale().percent());
	m_streak_minimum->setEnabled(false);

	QFormLayout* history_layout = new QFormLayout(history_group);
	history_layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	history_layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
	history_layout->addRow(m_goal_history);
	history_layout->addRow(m_goal_streaks);
	m_streak_minimum_label = new QLabel(tr("Minimum progress:"), history_group);
	history_layout->addRow(m_streak_minimum_label);
	history_layout->addRow(m_streak_minimum);
	m_streak_minimum_label->setEnabled(false);

	QVBoxLayout* history_page_layout = new QVBoxLayout(history_page);
	history_page_layout->addWidget(history_group);
	history_page_layout->addStretch();
	addPage(tr("Goal History"), history_page);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::initStatisticsPages()
{
	QWidget* counts_page = new QWidget(this);

	// Create statistics options
	QGroupBox* counts_group = new QGroupBox(tr("Displayed Counts"), counts_page);
	m_show_words = new QCheckBox(tr("Word count"), counts_group);
	m_show_pages = new QCheckBox(tr("Page count"), counts_group);
	m_show_paragraphs = new QCheckBox(tr("Paragraph count"), counts_group);
	m_show_characters = new QCheckBox(tr("Character count"), counts_group);

	QVBoxLayout* counts_layout = new QVBoxLayout(counts_group);
	counts_layout->addWidget(m_show_words);
	counts_layout->addWidget(m_show_pages);
	counts_layout->addWidget(m_show_paragraphs);
	counts_layout->addWidget(m_show_characters);
	QVBoxLayout* counts_page_layout = new QVBoxLayout(counts_page);
	counts_page_layout->addWidget(counts_group);
	counts_page_layout->addStretch();
	addPage(tr("Displayed Counts"), counts_page);

	// Create word count algorithm options
	QWidget* wordcount_page = new QWidget(this);
	QGroupBox* wordcount_group = new QGroupBox(tr("Word Count Algorithm"), wordcount_page);

	m_option_accurate_wordcount = new QRadioButton(tr("Detect boundaries"), wordcount_group);
	m_option_accurate_wordcount->setToolTip(tr("Detect word boundaries"));
	m_option_estimate_wordcount = new QRadioButton(tr("Estimate from text length"), wordcount_group);
	m_option_estimate_wordcount->setToolTip(tr("Divide character count by six"));
	m_option_singlechar_wordcount = new QRadioButton(tr("Count each letter"), wordcount_group);
	m_option_singlechar_wordcount->setToolTip(tr("Count each letter as a word"));

	QVBoxLayout* wordcount_layout = new QVBoxLayout(wordcount_group);
	wordcount_layout->addWidget(m_option_accurate_wordcount);
	wordcount_layout->addWidget(m_option_estimate_wordcount);
	wordcount_layout->addWidget(m_option_singlechar_wordcount);
	QVBoxLayout* wordcount_page_layout = new QVBoxLayout(wordcount_page);
	wordcount_page_layout->addWidget(wordcount_group);
	wordcount_page_layout->addStretch();
	addPage(tr("Word Count"), wordcount_page);

	// Create page count algorithm options
	QWidget* page_count_page = new QWidget(this);
	QGroupBox* page_group = new QGroupBox(tr("Page Count Algorithm"), page_count_page);

	m_option_characters = new QRadioButton(tr("Characters:"), page_group);
	m_page_characters = new QSpinBox(page_group);
	m_page_characters->setCorrectionMode(QSpinBox::CorrectToNearestValue);
	m_page_characters->setRange(Preferences::instance().pageCharacters().minimumValue(), Preferences::instance().pageCharacters().maximumValue());
	m_page_characters->setSingleStep(250);
	m_page_characters->setEnabled(false);

	m_option_paragraphs = new QRadioButton(tr("Paragraphs:"), page_group);
	m_page_paragraphs = new QSpinBox(page_group);
	m_page_paragraphs->setCorrectionMode(QSpinBox::CorrectToNearestValue);
	m_page_paragraphs->setRange(Preferences::instance().pageParagraphs().minimumValue(), Preferences::instance().pageParagraphs().maximumValue());
	m_page_paragraphs->setSingleStep(1);
	m_page_paragraphs->setEnabled(false);

	m_option_words = new QRadioButton(tr("Words:"), page_group);
	m_page_words = new QSpinBox(page_group);
	m_page_words->setCorrectionMode(QSpinBox::CorrectToNearestValue);
	m_page_words->setRange(Preferences::instance().pageWords().minimumValue(), Preferences::instance().pageWords().maximumValue());
	m_page_words->setSingleStep(50);
	m_page_words->setEnabled(false);

	connect(m_option_characters, &QRadioButton::toggled, m_page_characters, &QSpinBox::setEnabled);
	connect(m_option_characters, &QRadioButton::toggled, m_page_paragraphs, &QSpinBox::setDisabled);
	connect(m_option_characters, &QRadioButton::toggled, m_page_words, &QSpinBox::setDisabled);

	connect(m_option_paragraphs, &QRadioButton::toggled, m_page_characters, &QSpinBox::setDisabled);
	connect(m_option_paragraphs, &QRadioButton::toggled, m_page_paragraphs, &QSpinBox::setEnabled);
	connect(m_option_paragraphs, &QRadioButton::toggled, m_page_words, &QSpinBox::setDisabled);

	connect(m_option_words, &QRadioButton::toggled, m_page_characters, &QSpinBox::setDisabled);
	connect(m_option_words, &QRadioButton::toggled, m_page_paragraphs, &QSpinBox::setDisabled);
	connect(m_option_words, &QRadioButton::toggled, m_page_words, &QSpinBox::setEnabled);

	QGridLayout* page_layout = new QGridLayout(page_group);
	page_layout->setColumnStretch(2, 1);
	page_layout->addWidget(m_option_characters, 0, 0);
	page_layout->addWidget(m_page_characters, 0, 1);
	page_layout->addWidget(m_option_paragraphs, 1, 0);
	page_layout->addWidget(m_page_paragraphs, 1, 1);
	page_layout->addWidget(m_option_words, 2, 0);
	page_layout->addWidget(m_page_words, 2, 1);

	QVBoxLayout* page_count_page_layout = new QVBoxLayout(page_count_page);
	page_count_page_layout->addWidget(page_group);
	page_count_page_layout->addStretch();
	addPage(tr("Page Count"), page_count_page);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::initSpellingPages()
{
	QWidget* spelling_page = new QWidget(this);

	// Create spelling options
	QWidget* general_group = new QWidget(spelling_page);

	m_highlight_misspelled = new QCheckBox(tr("Check spelling as you type"), general_group);
	m_ignore_uppercase = new QCheckBox(tr("Ignore words in UPPERCASE"), general_group);
	m_ignore_numbers = new QCheckBox(tr("Ignore words with numbers"), general_group);
#ifdef Q_OS_MAC
	m_ignore_uppercase->hide();
	m_ignore_numbers->hide();
#endif

	QVBoxLayout* general_group_layout = new QVBoxLayout(general_group);
	general_group_layout->setContentsMargins(0, 0, 0, 0);
	general_group_layout->addWidget(m_highlight_misspelled);
	general_group_layout->addWidget(m_ignore_uppercase);
	general_group_layout->addWidget(m_ignore_numbers);

	QVBoxLayout* spelling_layout = new QVBoxLayout(spelling_page);
	spelling_layout->addWidget(general_group);
	spelling_layout->addStretch();
	addPage(tr("Spell Checking"), spelling_page);

	// Language management is its own bounded page. On Linux all three spelling
	// switches are visible, so combining them with dictionary controls forced a
	// whole-panel scrollbar at 1280x800 even when macOS happened to fit.
	QWidget* languages_page = new QWidget(this);
	QGroupBox* languages_group = new QGroupBox(tr("Language"), languages_page);

	m_languages = new QComboBox(languages_group);
	connect(m_languages, &QComboBox::currentIndexChanged, this, &PreferencesDialog::selectedLanguageChanged);

	m_add_language_button = new QPushButton(tr("Add"), languages_group);
	m_add_language_button->setAutoDefault(false);
	connect(m_add_language_button, &QPushButton::clicked, this, &PreferencesDialog::addLanguage);
	m_remove_language_button = new QPushButton(tr("Remove"), languages_group);
	m_remove_language_button->setAutoDefault(false);
	connect(m_remove_language_button, &QPushButton::clicked, this, &PreferencesDialog::removeLanguage);

	const QStringList languages = DictionaryManager::instance().availableDictionaries();
	for (const QString& language : languages) {
		m_languages->addItem(LocaleDialog::languageName(language), language);
	}
	m_languages->model()->sort(0);

	// Lay out language selection
	QGridLayout* languages_layout = new QGridLayout(languages_group);
	languages_layout->addWidget(m_languages, 0, 0, 1, 2);
	languages_layout->addWidget(m_add_language_button, 1, 0);
	languages_layout->addWidget(m_remove_language_button, 1, 1);

	QVBoxLayout* languages_page_layout = new QVBoxLayout(languages_page);
	languages_page_layout->addWidget(languages_group);
	languages_page_layout->addStretch();
	addPage(tr("Dictionaries"), languages_page);

	// Read personal dictionary
	QWidget* dictionary_page = new QWidget(this);
	QGroupBox* personal_dictionary_group = new QGroupBox(tr("Personal Dictionary"), dictionary_page);

	m_word = new QLineEdit(personal_dictionary_group);
	connect(m_word, &QLineEdit::textChanged, this, &PreferencesDialog::wordEdited);

	m_add_word_button = new QPushButton(tr("Add"), personal_dictionary_group);
	m_add_word_button->setAutoDefault(false);
	m_add_word_button->setDisabled(true);
	connect(m_add_word_button, &QPushButton::clicked, this, &PreferencesDialog::addWord);

	m_personal_dictionary = new QListWidget(personal_dictionary_group);
	const QStringList words = DictionaryManager::instance().personal();
	for (const QString& word : words) {
		m_personal_dictionary->addItem(word);
	}
	connect(m_personal_dictionary, &QListWidget::itemSelectionChanged, this, &PreferencesDialog::selectedWordChanged);

	m_remove_word_button = new QPushButton(tr("Remove"), personal_dictionary_group);
	m_remove_word_button->setAutoDefault(false);
	m_remove_word_button->setDisabled(true);
	connect(m_remove_word_button, &QPushButton::clicked, this, &PreferencesDialog::removeWord);

	// Lay out personal dictionary group
	QGridLayout* personal_dictionary_layout = new QGridLayout(personal_dictionary_group);
	personal_dictionary_layout->addWidget(m_word, 0, 0, 1, 2);
	personal_dictionary_layout->addWidget(m_add_word_button, 1, 0);
	personal_dictionary_layout->addWidget(m_remove_word_button, 1, 1);
	personal_dictionary_layout->addWidget(m_personal_dictionary, 2, 0, 1, 2);

	QVBoxLayout* dictionary_layout = new QVBoxLayout(dictionary_page);
	dictionary_layout->addWidget(personal_dictionary_group, 1);
	addPage(tr("Personal Dictionary"), dictionary_page);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::initToolbarPages()
{
	QWidget* style_page = new QWidget(this);

	// Create style options
	QGroupBox* style_group = new QGroupBox(tr("Toolbar Style"), style_page);

	m_toolbar_style = new QComboBox(style_group);
	m_toolbar_style->addItem(tr("Icons Only"), Qt::ToolButtonIconOnly);
	m_toolbar_style->addItem(tr("Text Only"), Qt::ToolButtonTextOnly);
	m_toolbar_style->addItem(tr("Text Alongside Icons"), Qt::ToolButtonTextBesideIcon);
	m_toolbar_style->addItem(tr("Text Under Icons"), Qt::ToolButtonTextUnderIcon);

	// Lay out style options
	QFormLayout* style_layout = new QFormLayout(style_group);
	style_layout->addRow(m_toolbar_style);
	QVBoxLayout* style_page_layout = new QVBoxLayout(style_page);
	style_page_layout->addWidget(style_group);
	style_page_layout->addStretch();
	addPage(tr("Toolbar Style"), style_page);

	// Create action options
	QWidget* actions_page = new QWidget(this);
	QGroupBox* actions_group = new QGroupBox(tr("Toolbar Actions"), actions_page);

	m_toolbar_actions = new QListWidget(actions_group);
	m_toolbar_actions->setDragDropMode(QAbstractItemView::InternalMove);
	const QList<QAction*> actions = parentWidget()->window()->actions();
	for (QAction* action : actions) {
		if (action->data().isNull()) {
			continue;
		}
		QListWidgetItem* item = new QListWidgetItem(action->icon(), action->iconText(), m_toolbar_actions);
		item->setData(Qt::UserRole, action->data());
		item->setCheckState(Qt::Unchecked);
	}
	m_toolbar_actions->sortItems();
	connect(m_toolbar_actions, &QListWidget::currentRowChanged, this, &PreferencesDialog::currentActionChanged);

	m_move_up_button = new QPushButton(tr("Move Up"), actions_group);
	connect(m_move_up_button, &QPushButton::clicked, this, &PreferencesDialog::moveActionUp);
	m_move_down_button = new QPushButton(tr("Move Down"), actions_group);
	connect(m_move_down_button, &QPushButton::clicked, this, &PreferencesDialog::moveActionDown);
	// Lay out action options
	QGridLayout* actions_layout = new QGridLayout(actions_group);
	actions_layout->setRowStretch(0, 1);
	actions_layout->addWidget(m_toolbar_actions, 0, 0, 1, 2);
	actions_layout->addWidget(m_move_up_button, 1, 0);
	actions_layout->addWidget(m_move_down_button, 1, 1);

	// Lay out toolbar tab
	QVBoxLayout* actions_page_layout = new QVBoxLayout(actions_page);
	actions_page_layout->addWidget(actions_group, 1);
	addPage(tr("Toolbar Actions"), actions_page);

	QWidget* separator_page = new QWidget(this);
	QGroupBox* separator_group = new QGroupBox(tr("Toolbar Separator"), separator_page);
	QPushButton* add_separator_button = new QPushButton(tr("Add Separator"), separator_group);
	connect(add_separator_button, &QPushButton::clicked, this, &PreferencesDialog::addSeparatorAction);
	QVBoxLayout* separator_group_layout = new QVBoxLayout(separator_group);
	separator_group_layout->addWidget(add_separator_button);
	QVBoxLayout* separator_page_layout = new QVBoxLayout(separator_page);
	separator_page_layout->addWidget(separator_group);
	separator_page_layout->addStretch();
	addPage(tr("Toolbar Separator"), separator_page);
}

//-----------------------------------------------------------------------------

void PreferencesDialog::initShortcutsPage()
{
	QWidget* tab = new QWidget(this);
	m_shortcuts_page_index = m_pages->count();

	// Create shortcuts view
	m_shortcuts = new QTreeWidget(tab);
	m_shortcuts->setIconSize(QSize(16,16));
	m_shortcuts->setDragDropMode(QAbstractItemView::NoDragDrop);
	m_shortcuts->setItemsExpandable(false);
	m_shortcuts->setRootIsDecorated(false);
	m_shortcuts->setColumnCount(3);
	m_shortcuts->setColumnHidden(2, true);
	m_shortcuts->setHeaderLabels({ tr("Command"), tr("Shortcut"), tr("Action") });
	m_shortcuts->header()->setSectionsClickable(false);
	m_shortcuts->header()->setSectionsMovable(false);
	m_shortcuts->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	connect(m_shortcuts, &QTreeWidget::itemDoubleClicked, this, &PreferencesDialog::shortcutDoubleClicked);

	// List shortcuts
	QPixmap empty_icon(m_shortcuts->iconSize());
	empty_icon.fill(Qt::transparent);
	const QStringList actions = ActionManager::instance()->actions();
	for (const QString& name : actions) {
		const QAction* action = ActionManager::instance()->action(name);
		QIcon icon = action->icon();
		if (icon.isNull()) {
			icon = empty_icon;
		}
		QString text = action->statusTip();
		if (text.isEmpty()) {
			text = action->text();
		}
		text.replace("&", QString());
		const QStringList strings{ text, action->shortcut().toString(QKeySequence::NativeText), name };
		QTreeWidgetItem* item = new QTreeWidgetItem(m_shortcuts, strings);
		item->setIcon(0, icon);
	}
	m_shortcuts->sortByColumn(0, Qt::AscendingOrder);
	connect(m_shortcuts, &QTreeWidget::itemSelectionChanged, this, &PreferencesDialog::selectedShortcutChanged);

	// Create editor
	m_shortcut_edit = new ShortcutEdit(this);
	connect(m_shortcut_edit, &ShortcutEdit::changed, this, &PreferencesDialog::shortcutChanged);

	// Lay out shortcut tab
	QGridLayout* layout = new QGridLayout(tab);
	layout->setColumnStretch(1, 1);
	layout->setRowStretch(0, 1);
	layout->addWidget(m_shortcuts, 0, 0, 1, 2);
	layout->addWidget(new QLabel(ShortcutEdit::tr("Shortcut:"), tab), 1, 0);
	layout->addWidget(m_shortcut_edit, 1, 1);

	m_shortcuts->setCurrentItem(m_shortcuts->topLevelItem(0));
	highlightShortcutConflicts();

	addPage(tr("Shortcuts"), tab);
}

//-----------------------------------------------------------------------------
