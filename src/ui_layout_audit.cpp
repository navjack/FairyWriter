/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ui_layout_audit.h"

#include "action_manager.h"
#include "daily_progress_dialog.h"
#include "preferences_dialog.h"
#include "document.h"
#include "presentation_surface.h"
#include "snes_side_panel.h"
#include "snes_style.h"
#include "stack.h"
#include "theme.h"
#include "theme_dialog.h"
#include "theme_manager.h"
#include "window.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QDebug>
#include <QEventLoop>
#include <QGroupBox>
#include <QLabel>
#include <QMetaObject>
#include <QPalette>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleOptionComboBox>
#include <QStringList>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>

namespace
{

class Audit
{
public:
	void require(bool condition, const QString& message)
	{
		if (!condition) {
			m_failures.append(message);
		}
	}

	bool passed() const { return m_failures.isEmpty(); }
	QString report() const
	{
		return passed()
			? QStringLiteral("FairyWriter UI layout audit passed.\n")
			: QStringLiteral("FairyWriter UI layout audit failed:\n - %1\n").arg(m_failures.join(QStringLiteral("\n - ")));
	}

private:
	QStringList m_failures;
};

void flushLayouts()
{
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

bool isInsideScrollView(const QWidget* widget, const QWidget* root)
{
	for (const QWidget* parent = widget->parentWidget(); parent && parent != root; parent = parent->parentWidget()) {
		if (qobject_cast<const QAbstractScrollArea*>(parent)) {
			return true;
		}
	}
	return false;
}

void auditTextControl(Audit& audit, QWidget* widget, const QString& context)
{
	if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget)) {
		if (!button->text().isEmpty()) {
			audit.require(button->width() >= button->sizeHint().width(),
				QStringLiteral("%1 clips button text '%2' (%3 < %4)")
					.arg(context, button->text()).arg(button->width()).arg(button->sizeHint().width()));
			audit.require(button->height() >= button->sizeHint().height(),
				QStringLiteral("%1 clips button height '%2' (%3 < %4)")
					.arg(context, button->text()).arg(button->height()).arg(button->sizeHint().height()));
		}
		return;
	}
	if (const QLabel* label = qobject_cast<const QLabel*>(widget)) {
		if (!label->wordWrap() && !label->text().isEmpty() && label->pixmap().isNull()) {
			const int required = label->fontMetrics().horizontalAdvance(label->text());
			audit.require(label->contentsRect().width() >= required,
				QStringLiteral("%1 clips label '%2' (%3 < %4)")
					.arg(context, label->text()).arg(label->contentsRect().width()).arg(required));
		}
		return;
	}
	if (const QComboBox* combo = qobject_cast<const QComboBox*>(widget)) {
		if (!combo->currentText().isEmpty()) {
			QStyleOptionComboBox option;
			option.initFrom(combo);
			option.currentText = combo->currentText();
			option.currentIcon = combo->itemIcon(combo->currentIndex());
			option.editable = combo->isEditable();
			QSize contents(combo->fontMetrics().horizontalAdvance(option.currentText), combo->fontMetrics().height());
			if (!option.currentIcon.isNull()) {
				contents.rwidth() += combo->iconSize().width();
			}
			const int required = combo->style()->sizeFromContents(QStyle::CT_ComboBox, &option, contents, combo).width();
			audit.require(combo->width() >= required,
				QStringLiteral("%1 clips selector '%2' (%3 < %4)")
					.arg(context, combo->currentText()).arg(combo->width()).arg(required));
		}
		return;
	}
	if (const QGroupBox* group = qobject_cast<const QGroupBox*>(widget)) {
		const int required = group->fontMetrics().horizontalAdvance(group->title()) + 16;
		audit.require(group->width() >= required,
			QStringLiteral("%1 clips group title '%2' (%3 < %4)")
				.arg(context, group->title()).arg(group->width()).arg(required));
	}
}

void auditWidgetTree(Audit& audit, QWidget* root, QWidget* parent, const QString& context)
{
	const QList<QWidget*> children = parent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
	for (QWidget* child : children) {
		if (child->isWindow() || !child->isVisibleTo(root)) {
			continue;
		}
		if (!qobject_cast<QAbstractScrollArea*>(parent) && !isInsideScrollView(child, root)) {
			audit.require(parent->contentsRect().contains(child->geometry()),
				QStringLiteral("%1 places %2 outside its parent: child=%3,%4 %5x%6 parent=%7x%8")
					.arg(context, QString::fromLatin1(child->metaObject()->className()))
					.arg(child->x()).arg(child->y()).arg(child->width()).arg(child->height())
					.arg(parent->contentsRect().width()).arg(parent->contentsRect().height()));
			auditTextControl(audit, child, context);
		}
		if (!qobject_cast<QAbstractScrollArea*>(child)) {
			auditWidgetTree(audit, root, child, context);
		}
	}
}

void savePanelFrame(SnesSidePanel* panel, const QString& family, int page)
{
	const QString output = qEnvironmentVariable("FAIRYWRITER_UI_AUDIT_DIR");
	if (output.isEmpty()) {
		return;
	}
	QDir().mkpath(output);
	panel->grab().save(QDir(output).filePath(QStringLiteral("%1-%2.png").arg(family).arg(page, 2, 10, QLatin1Char('0'))));
}

void saveWindowFrame(Window* window, const QString& name)
{
	const QString output = qEnvironmentVariable("FAIRYWRITER_UI_AUDIT_DIR");
	if (output.isEmpty()) {
		return;
	}
	QDir().mkpath(output);
	window->grab().save(QDir(output).filePath(name + QStringLiteral(".png")));
}

int wrappedLineCount(QTextEdit* editor)
{
	int lines = 0;
	for (QTextBlock block = editor->document()->begin(); block.isValid(); block = block.next()) {
		if (block.layout()) {
			lines += block.layout()->lineCount();
		}
	}
	return lines;
}

void auditPages(
	Audit& audit,
	SnesSidePanel* panel,
	QWidget* tool,
	const QString& selector_name,
	const QString& pages_name,
	const QString& family)
{
	QComboBox* selector = tool->findChild<QComboBox*>(selector_name);
	QStackedWidget* pages = tool->findChild<QStackedWidget*>(pages_name);
	audit.require(selector != nullptr, family + QStringLiteral(" selector is missing"));
	audit.require(pages != nullptr, family + QStringLiteral(" page stack is missing"));
	if (!selector || !pages) {
		return;
	}
	QScrollArea* viewport = panel->findChild<QScrollArea*>(QStringLiteral("FairyWriterToolViewport"));
	const QList<QDialogButtonBox*> button_boxes = tool->findChildren<QDialogButtonBox*>();
	for (QDialogButtonBox* box : button_boxes) {
		if (qEnvironmentVariableIsSet("FAIRYWRITER_UI_AUDIT_DEBUG")) {
			qInfo() << family << "tool" << tool->geometry() << "buttons" << box->geometry()
				<< "button visible" << box->isVisibleTo(tool) << "hint" << box->sizeHint();
		}
		audit.require(!box->isHidden(), family + QStringLiteral(" hides its command buttons"));
		audit.require(box->height() >= box->sizeHint().height(),
			QStringLiteral("%1 collapses its command row (%2 < %3)")
				.arg(family).arg(box->height()).arg(box->sizeHint().height()));
		audit.require(tool->contentsRect().contains(box->geometry()),
			QStringLiteral("%1 places its command row outside the tool (row=%2,%3 %4x%5 tool=%6x%7)")
				.arg(family).arg(box->x()).arg(box->y()).arg(box->width()).arg(box->height())
				.arg(tool->contentsRect().width()).arg(tool->contentsRect().height()));
		for (QAbstractButton* button : box->findChildren<QAbstractButton*>()) {
			audit.require(!button->isHidden() && button->width() >= button->sizeHint().width()
				&& button->height() >= button->sizeHint().height(),
				QStringLiteral("%1 command '%2' is not fully reachable").arg(family, button->text()));
		}
	}
	audit.require(selector->count() == pages->count(), family + QStringLiteral(" selector/page counts differ"));
	for (int page = 0; page < pages->count(); ++page) {
		selector->setCurrentIndex(page);
		flushLayouts();
		const QString context = QStringLiteral("%1 page %2 (%3)").arg(family).arg(page).arg(selector->currentText());
		if (qEnvironmentVariableIsSet("FAIRYWRITER_UI_AUDIT_DEBUG")) {
			qInfo() << context << "tool" << tool->geometry()
				<< "commands" << (button_boxes.isEmpty() ? QRect() : button_boxes.constFirst()->geometry());
		}
		if (viewport) {
			audit.require(viewport->widget()->size() == viewport->viewport()->size(),
				QStringLiteral("%1 grows panel content beyond the viewport (content=%2x%3 viewport=%4x%5)")
					.arg(context).arg(viewport->widget()->width()).arg(viewport->widget()->height())
					.arg(viewport->viewport()->width()).arg(viewport->viewport()->height()));
			audit.require(viewport->widget()->contentsRect().contains(tool->geometry()),
				context + QStringLiteral(" escapes its fixed panel host"));
		}
		for (QDialogButtonBox* box : button_boxes) {
			audit.require(box->isVisibleTo(tool) && tool->contentsRect().contains(box->geometry()),
				context + QStringLiteral(" does not expose its command row"));
		}
		auditWidgetTree(audit, tool, tool, context);
		savePanelFrame(panel, family.toLower(), page);
	}

	audit.require(viewport != nullptr, family + QStringLiteral(" panel viewport is missing"));
	if (viewport) {
		if (qEnvironmentVariableIsSet("FAIRYWRITER_UI_AUDIT_DEBUG")) {
			qInfo() << family << "panel" << panel->geometry() << "scroll" << viewport->geometry()
				<< "viewport" << viewport->viewport()->size() << "content" << viewport->widget()->size();
		}
		audit.require(viewport->widget()->size() == viewport->viewport()->size()
			&& viewport->widget()->contentsRect().contains(tool->geometry()),
			QStringLiteral("%1 panel content is not viewport-sized (content=%2x%3 viewport=%4x%5)")
				.arg(family).arg(viewport->widget()->width()).arg(viewport->widget()->height())
				.arg(viewport->viewport()->width()).arg(viewport->viewport()->height()));
		audit.require(viewport->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
			family + QStringLiteral(" enables a whole-panel horizontal scrollbar"));
		audit.require(viewport->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
			family + QStringLiteral(" enables a whole-panel vertical scrollbar"));
		audit.require(viewport->horizontalScrollBar()->maximum() == 0,
			QStringLiteral("%1 content exceeds the panel width (range=%2 viewport=%3 content=%4)")
				.arg(family).arg(viewport->horizontalScrollBar()->maximum())
				.arg(viewport->viewport()->width()).arg(viewport->widget()->width()));
		audit.require(viewport->verticalScrollBar()->maximum() == 0,
			QStringLiteral("%1 content exceeds the panel height (range=%2 viewport=%3 content=%4)")
				.arg(family).arg(viewport->verticalScrollBar()->maximum())
				.arg(viewport->viewport()->height()).arg(viewport->widget()->height()));
	}
}

}

bool runUiLayoutAudit(Window* window, QString& report)
{
	Audit audit;
	audit.require(window != nullptr, QStringLiteral("main window is missing"));
	if (!window) {
		report = audit.report();
		return false;
	}

	window->showNormal();
	window->resize(1280, 800);
	flushLayouts();
	audit.require(window->size() == QSize(1280, 800), QStringLiteral("offscreen Steam Deck viewport is not 1280x800"));

	QToolBar* toolbar = window->findChild<QToolBar*>();
	QAction* show_toolbar = ActionManager::instance()->action(QStringLiteral("ShowToolbar"));
	audit.require(toolbar != nullptr && show_toolbar != nullptr,
		QStringLiteral("toolbar visibility action is missing"));
	if (toolbar && show_toolbar) {
		if (!toolbar->isVisible()) {
			show_toolbar->trigger();
			flushLayouts();
		}
		audit.require(toolbar->isVisible() && show_toolbar->isChecked(),
			QStringLiteral("toolbar and visibility action start out of sync"));
		audit.require(window->actions().contains(show_toolbar) && !show_toolbar->shortcut().isEmpty(),
			QStringLiteral("toolbar recovery shortcut is not registered on the main window"));

		show_toolbar->trigger();
		flushLayouts();
		audit.require(!toolbar->isVisible() && !show_toolbar->isChecked()
				&& !QSettings().value(QStringLiteral("Toolbar/Shown"), true).toBool(),
			QStringLiteral("hiding the toolbar does not synchronize its action and persisted state"));

		show_toolbar->trigger();
		flushLayouts();
		audit.require(toolbar->isVisible() && show_toolbar->isChecked()
				&& QSettings().value(QStringLiteral("Toolbar/Shown"), false).toBool(),
			QStringLiteral("toolbar recovery action does not restore the hidden toolbar"));
	}

	Stack* stack = window->findChild<Stack*>();
	Document* document = stack ? stack->currentDocument() : nullptr;
	QTextEdit* editor = document ? document->text() : nullptr;
	audit.require(stack != nullptr && document != nullptr && editor != nullptr,
		QStringLiteral("active document editor is missing"));
	QSize full_editor_size;
	QRect full_editor_in_stack;
	int full_line_count = 0;
	if (editor) {
		editor->setPlainText(QStringLiteral(
			"FairyWriter keeps every word inside the corrected SNES document surface while a side panel opens and closes. "
			"This deliberately long paragraph must wrap to the available writing grid instead of disappearing beneath the tool panel."));
		flushLayouts();
		full_editor_size = editor->viewport()->size();
		full_editor_in_stack = QRect(editor->mapTo(stack, QPoint()), editor->size());
		full_line_count = wrappedLineCount(editor);
		audit.require(document->contentsRect().contains(editor->geometry()),
			QStringLiteral("full-width editor escapes the document paper"));
		saveWindowFrame(window, QStringLiteral("document-full"));
	}

	const bool preferences_opened = QMetaObject::invokeMethod(window, "preferencesClicked", Qt::DirectConnection);
	audit.require(preferences_opened, QStringLiteral("Preferences action could not be invoked"));
	flushLayouts();
	SnesSidePanel* panel = window->findChild<SnesSidePanel*>();
	PreferencesDialog* preferences = window->findChild<PreferencesDialog*>();
	audit.require(panel != nullptr, QStringLiteral("SNES side panel is missing"));
	audit.require(preferences != nullptr, QStringLiteral("Preferences did not open in the side panel"));
	if (panel && preferences) {
		if (editor && stack) {
			const QRect editor_in_stack(editor->mapTo(stack, QPoint()), editor->size());
			audit.require(document->contentsRect().contains(editor->geometry()),
				QStringLiteral("panel editor escapes the resized document paper"));
			audit.require(!editor_in_stack.intersects(panel->geometry()),
				QStringLiteral("panel overlaps the document text viewport"));
			audit.require(editor->viewport()->width() < full_editor_size.width(),
				QStringLiteral("opening a panel does not synchronously narrow the editor"));
			audit.require(editor_in_stack.topLeft() != full_editor_in_stack.topLeft(),
				QStringLiteral("opening a panel does not move the text with the document"));
			audit.require(wrappedLineCount(editor) == full_line_count,
				QStringLiteral("panel scaling changes the fixed SNES writing grid"));
			saveWindowFrame(window, QStringLiteral("document-panel"));
		}
		auditPages(audit, panel, preferences,
			QStringLiteral("FairyWriterPreferencesCategory"),
			QStringLiteral("FairyWriterPreferencesPages"),
			QStringLiteral("Preferences"));
		preferences->reject();
		flushLayouts();
		if (editor) {
			audit.require(editor->viewport()->size() == full_editor_size,
				QStringLiteral("closing a panel does not restore the full editor grid"));
			audit.require(wrappedLineCount(editor) == full_line_count,
				QStringLiteral("closing a panel does not restore the original text reflow"));
		}
	}

	QAction* daily_action = nullptr;
	for (QAction* action : window->findChildren<QAction*>()) {
		if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Daily Progress")) {
			daily_action = action;
			break;
		}
	}
	audit.require(daily_action != nullptr, QStringLiteral("Daily Progress action is missing"));
	if (daily_action) {
		daily_action->trigger();
		flushLayouts();
		DailyProgressDialog* daily = window->findChild<DailyProgressDialog*>();
		audit.require(daily != nullptr, QStringLiteral("Daily Progress did not open in the side panel"));
		if (daily) {
			audit.require(daily->findChild<QLabel*>(QStringLiteral("DailyProgressValue")) != nullptr,
				QStringLiteral("Daily Progress today card is missing"));
			audit.require(daily->findChild<QProgressBar*>(QStringLiteral("DailyProgressMeter")) != nullptr,
				QStringLiteral("Daily Progress meter is missing"));
			QScrollArea* viewport = panel ? panel->findChild<QScrollArea*>(QStringLiteral("FairyWriterToolViewport")) : nullptr;
			if (viewport) {
				audit.require(viewport->horizontalScrollBar()->maximum() == 0,
					QStringLiteral("Daily Progress requires horizontal scrolling"));
				audit.require(viewport->widget()->contentsRect().contains(daily->geometry()),
					QStringLiteral("Daily Progress escapes its panel viewport"));
			}
			auditWidgetTree(audit, daily, daily, QStringLiteral("DailyProgress"));
			panel->closePanel();
			flushLayouts();
		}
	}

	const bool themes_opened = QMetaObject::invokeMethod(window, "themeClicked", Qt::DirectConnection);
	audit.require(themes_opened, QStringLiteral("Themes action could not be invoked"));
	flushLayouts();
	panel = window->findChild<SnesSidePanel*>();
	ThemeManager* themes = window->findChild<ThemeManager*>();
	audit.require(themes != nullptr, QStringLiteral("Themes did not open in the side panel"));
	if (panel && themes) {
		QComboBox* selector = themes->findChild<QComboBox*>(QStringLiteral("FairyWriterThemeSelector"));
		QWidget* preview = themes->findChild<QWidget*>(QStringLiteral("FairyWriterThemePreview"));
		QAbstractButton* new_theme = themes->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeNew"));
		QAbstractButton* edit_theme = themes->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeEdit"));
		QAbstractButton* duplicate_theme = themes->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeDuplicate"));
		QAbstractButton* more_themes = themes->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeMore"));
		QAction* delete_theme = themes->findChild<QAction*>(QStringLiteral("FairyWriterThemeDelete"));
		QAction* import_theme = themes->findChild<QAction*>(QStringLiteral("FairyWriterThemeImport"));
		QAction* export_theme = themes->findChild<QAction*>(QStringLiteral("FairyWriterThemeExport"));
		audit.require(selector && preview && new_theme && edit_theme && duplicate_theme && more_themes
				&& delete_theme && import_theme && export_theme,
			QStringLiteral("unified theme manager controls are missing"));
		audit.require(themes->findChild<QComboBox*>(QStringLiteral("FairyWriterThemeCategory")) == nullptr
				&& themes->findChild<QStackedWidget*>(QStringLiteral("FairyWriterThemePages")) == nullptr,
			QStringLiteral("theme manager still exposes default/custom category pages"));

		QScrollArea* viewport = panel->findChild<QScrollArea*>(QStringLiteral("FairyWriterToolViewport"));
		audit.require(viewport != nullptr, QStringLiteral("Themes panel viewport is missing"));
		if (viewport) {
			audit.require(viewport->widget()->size() == viewport->viewport()->size()
					&& viewport->widget()->contentsRect().contains(themes->geometry()),
				QStringLiteral("Themes panel content is not viewport-sized"));
			audit.require(viewport->horizontalScrollBar()->maximum() == 0
					&& viewport->verticalScrollBar()->maximum() == 0,
				QStringLiteral("Themes panel requires scrolling"));
		}
		auditWidgetTree(audit, themes, themes, QStringLiteral("Themes"));
		savePanelFrame(panel, QStringLiteral("themes"), 0);

		if (selector && new_theme && edit_theme && duplicate_theme && more_themes
				&& delete_theme && import_theme && export_theme) {
			audit.require(selector->count() >= 10, QStringLiteral("unified theme selector omits bundled themes"));
			audit.require(selector->isVisibleTo(themes) && selector->height() >= selector->sizeHint().height(),
				QStringLiteral("unified theme selector is not fully visible"));
			const int default_index = selector->findData(Theme::defaultId());
			audit.require(default_index >= 0, QStringLiteral("unified theme selector omits the default theme"));
			if (default_index >= 0) {
				selector->setCurrentIndex(default_index);
				flushLayouts();
				audit.require(!edit_theme->isEnabled() && duplicate_theme->isEnabled()
						&& !delete_theme->isEnabled() && !export_theme->isEnabled(),
					QStringLiteral("built-in theme actions do not preserve read-only behavior"));
			}
			audit.require(more_themes->isEnabled() && import_theme->isEnabled(),
				QStringLiteral("theme management commands are not directly available"));

			QObject::disconnect(themes, nullptr, window, nullptr);
			bool requested = false;
			bool is_new = false;
			Theme draft;
			QObject::connect(themes, &ThemeManager::editRequested, themes,
				[&](const Theme& theme, bool new_theme_requested) {
					requested = true;
					is_new = new_theme_requested;
					draft = theme;
				});
			new_theme->click();
			audit.require(requested && is_new && !draft.name().isEmpty()
					&& draft.wallpaperSource() == Theme::WallpaperSource::GeneratedCherryBlossom,
				QStringLiteral("New Theme does not directly open a named cherry-blossom draft"));
		}
		themes->reject();
		flushLayouts();
	}

	if (panel) {
		const int opening_magnification = SnesStyle::uiMagnification();
		for (const int magnification : {1, 2, 4}) {
			QMetaObject::invokeMethod(window, "setUiMagnification", Qt::DirectConnection, Q_ARG(int, magnification));
			flushLayouts();
			Theme audit_theme(Theme::defaultId(), true);
			ThemeDialog* theme_editor = new ThemeDialog(audit_theme, false, window);
			const QString document_before = editor ? editor->document()->toHtml() : QString();
			const bool modified_before = editor && editor->document()->isModified();
			const bool undo_before = editor && editor->document()->isUndoAvailable();
			bool canceled = false;
			QObject::connect(theme_editor, &ThemeDialog::previewChanged, stack, [stack](const Theme& draft) {
				if (stack->themePreviewActive()) stack->updateThemePreview(draft);
				else stack->beginThemePreview(draft);
			});
			QObject::connect(theme_editor, &ThemeDialog::previewCanceled, stack, [&] {
				canceled = true;
				stack->cancelThemePreview();
			});
			stack->beginThemePreview(theme_editor->draft());
			panel->showTool(theme_editor, QStringLiteral("Theme Editor"));
			flushLayouts();
			auditPages(audit, panel, theme_editor,
				QStringLiteral("FairyWriterThemeEditorCategory"),
				QStringLiteral("FairyWriterThemeEditorPages"),
				QStringLiteral("ThemeEditor-%1x").arg(magnification));

			if (magnification == 2) {
				QComboBox* editor_category = theme_editor->findChild<QComboBox*>(QStringLiteral("FairyWriterThemeEditorCategory"));
				QComboBox* source = theme_editor->findChild<QComboBox*>(QStringLiteral("FairyWriterThemeWallpaperSource"));
				QAbstractButton* undo = theme_editor->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeUndo"));
				QAbstractButton* redo = theme_editor->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeRedo"));
				QAbstractButton* reset_page = theme_editor->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeResetPage"));
				QAbstractButton* reset_all = theme_editor->findChild<QAbstractButton*>(QStringLiteral("FairyWriterThemeResetAll"));
				audit.require(editor_category && source && undo && redo && reset_page && reset_all,
					QStringLiteral("theme editor history controls are not addressable"));
				if (editor_category && source && undo && redo && reset_page && reset_all) {
					editor_category->setCurrentIndex(0);
					const int alternate_source = (source->currentIndex() + 1) % source->count();
					source->setCurrentIndex(alternate_source);
					flushLayouts();
					audit.require(!(theme_editor->draft() == audit_theme),
						QStringLiteral("theme editor controls do not update the authoritative draft"));
					undo->click();
					flushLayouts();
					audit.require(theme_editor->draft() == audit_theme,
						QStringLiteral("theme editor Undo does not restore the exact opening snapshot"));
					redo->click();
					flushLayouts();
					audit.require(theme_editor->draft().wallpaperSource()
							== static_cast<Theme::WallpaperSource>(alternate_source),
						QStringLiteral("theme editor Redo does not restore the exact changed snapshot"));
					reset_page->click();
					flushLayouts();
					audit.require(theme_editor->draft() == audit_theme,
						QStringLiteral("theme editor page reset does not restore its session-opening values"));
					source->setCurrentIndex(alternate_source);
					reset_all->click();
					flushLayouts();
					audit.require(theme_editor->draft() == audit_theme,
						QStringLiteral("theme editor Reset All does not restore the complete opening draft"));
				}

				Theme rapid_first = audit_theme;
				rapid_first.setWallpaperSource(Theme::WallpaperSource::SolidColor);
				rapid_first.setBackgroundColor(QColor(QStringLiteral("#ef94bd")));
				Theme rapid_last = audit_theme;
				rapid_last.setWallpaperSource(Theme::WallpaperSource::SolidColor);
				rapid_last.setBackgroundColor(QColor(QStringLiteral("#63bd84")));
				stack->updateThemePreview(rapid_first);
				stack->updateThemePreview(rapid_last);
				QEventLoop settle;
				QTimer::singleShot(80, &settle, &QEventLoop::quit);
				settle.exec(QEventLoop::ExcludeUserInputEvents);
				stack->waitForThemeBackground();
				flushLayouts();
				QRect expected_paper;
				const QImage expected = PresentationSurface::render(
					rapid_last, stack->size(), expected_paper, stack->devicePixelRatioF(), true, stack->panelSide());
				const QImage actual = stack->presentationBackground().toImage();
				audit.require(actual.convertToFormat(QImage::Format_RGB32)
						== expected.convertToFormat(QImage::Format_RGB32),
					QStringLiteral("rapid theme renders display a stale draft instead of the newest generation"));
				stack->updateThemePreview(theme_editor->draft());
			}
			audit.require(stack->themePreviewActive(), QStringLiteral("theme editor does not start a live preview session"));
			panel->closePanel();
			flushLayouts();
			audit.require(canceled && !stack->themePreviewActive(),
				QStringLiteral("closing the side panel does not cancel and roll back theme preview"));
			if (editor) {
				audit.require(editor->document()->toHtml() == document_before,
					QStringLiteral("theme preview mutates active document content or formats"));
				audit.require(editor->document()->isModified() == modified_before,
					QStringLiteral("theme preview changes active document modified state"));
				audit.require(editor->document()->isUndoAvailable() == undo_before,
					QStringLiteral("theme preview changes active document undo state"));
			}
			delete theme_editor;
		}
		QMetaObject::invokeMethod(window, "setUiMagnification", Qt::DirectConnection, Q_ARG(int, opening_magnification));
		flushLayouts();
	}

	if (stack && editor) {
		for (const QString& id : {QStringLiteral("bitterskies"), QStringLiteral("enchantment")}) {
			const Theme legacy_theme(id, true);
			stack->themeSelected(legacy_theme);
			stack->waitForThemeBackground();
			flushLayouts();
			audit.require(editor->palette().color(QPalette::Text) == legacy_theme.textColor(),
				QStringLiteral("%1 document text color is not applied to the editor").arg(legacy_theme.name()));
			const QPalette ui = QApplication::palette();
			audit.require(SnesStyle::contrastRatio(ui.color(QPalette::WindowText), ui.color(QPalette::Window)) >= 4.5,
				QStringLiteral("%1 window text is unreadable").arg(legacy_theme.name()));
			audit.require(SnesStyle::contrastRatio(ui.color(QPalette::ButtonText), ui.color(QPalette::Button)) >= 4.5,
				QStringLiteral("%1 button text is unreadable").arg(legacy_theme.name()));
			saveWindowFrame(window, QStringLiteral("legacy-%1").arg(id));
		}
	}

	report = audit.report();
	return audit.passed();
}
