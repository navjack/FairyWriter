/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "stack.h"

#include "action_manager.h"
#include "alert.h"
#include "alert_layer.h"
#include "document.h"
#include "find_dialog.h"
#include "preferences.h"
#include "presentation_geometry.h"
#include "scene_list.h"
#include "scene_model.h"
#include "smart_quotes.h"
#include "snes_side_panel.h"
#include "snes_style.h"
#include "symbols_dialog.h"
#include "theme_renderer.h"
#include "theme_preview_document.h"
#include "wallpaper_motion_layer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDialog>
#include <QGridLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPageSetupDialog>
#include <QPainter>
#include <QPaintEvent>
#include <QPrinter>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>

#include <algorithm>

//-----------------------------------------------------------------------------

Stack::Stack(QWidget* parent)
	: QWidget(parent)
	, m_symbols_dialog(nullptr)
	, m_printer(nullptr)
	, m_current_document(nullptr)
	, m_theme_preview(nullptr)
	, m_theme_preview_active(false)
	, m_footer_margin(0)
	, m_header_margin(0)
	, m_footer_visible(0)
	, m_header_visible(0)
	, m_panel_side(QSettings().value("Presentation/PanelSide", "right").toString() == "left" ? PanelSide::Left : PanelSide::Right)
	, m_presentation_scale(1)
{
	setMouseTracking(true);

	m_contents = new QStackedWidget(this);

	// The animated wallpaper sits between the static background pixmap and
	// the transparent document contents; every other overlay stays above it.
	m_wallpaper_motion = new WallpaperMotionLayer(this);
	m_wallpaper_motion->stackUnder(m_contents);
	m_wallpaper_motion->setMotionPreference(Preferences::instance().wallpaperMotion());

	m_alerts = new AlertLayer(this);

	m_scenes = new SceneList(this);
	setScenesVisible(false);
	m_side_panel = new SnesSidePanel(this);
	connect(m_side_panel, &SnesSidePanel::panelVisibilityChanged, this, [this](bool visible) {
		Q_EMIT sidePanelVisible(visible);
		updateBackground();
		if (!visible && m_current_document) {
			m_current_document->text()->setFocus();
		}
	});

	m_menu = new QMenu(this);
	m_menu_group = new QActionGroup(this);
	m_menu_group->setExclusive(true);
	connect(m_menu_group, &QActionGroup::triggered, this, &Stack::actionTriggered);

	m_find_dialog = new FindDialog(this);
	connect(m_find_dialog, &FindDialog::findNextAvailable, this, &Stack::findNextAvailable);

	connect(ActionManager::instance(), &ActionManager::insertText, this, &Stack::insertSymbol);

	m_layout = new QGridLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);
	m_layout->setRowMinimumHeight(1, 6);
	m_layout->setRowMinimumHeight(4, 6);
	m_layout->setRowStretch(2, 1);
	m_layout->setColumnMinimumWidth(1, 6);
	m_layout->setColumnMinimumWidth(4, 6);
	m_layout->setColumnStretch(1, 1);
	m_layout->setColumnStretch(2, 1);
	m_layout->setColumnStretch(3, 1);
	// m_contents is positioned exclusively by PresentationGeometry. Registering
	// it in this inherited full-window grid lets QLayout overwrite that geometry
	// after every panel transition and leaves the editor outside its SNES paper.
	m_layout->addWidget(m_scenes, 1, 0, 4, 3);
	m_layout->addWidget(m_alerts, 3, 3);

	m_resize_timer = new QTimer(this);
	m_resize_timer->setInterval(50);
	m_resize_timer->setSingleShot(true);
	connect(m_resize_timer, &QTimer::timeout, this, qOverload<>(&Stack::updateBackground));

	m_theme_renderer = new ThemeRenderer(this);
	connect(m_theme_renderer, &ThemeRenderer::rendered, this, &Stack::applyRenderedBackground);

	setHeaderVisible(Preferences::instance().alwaysShowHeader());
	setFooterVisible(Preferences::instance().alwaysShowFooter());

	// Always draw background
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAutoFillBackground(false);
	updateBackground();
}

//-----------------------------------------------------------------------------

Stack::~Stack()
{
	m_theme_renderer->wait();

	delete m_printer;
}

//-----------------------------------------------------------------------------

void Stack::addDocument(Document* document)
{
	document->setSceneList(m_scenes);
	connect(document, &Document::alert, m_alerts, &AlertLayer::addAlert);
	connect(document, &Document::alignmentChanged, this, &Stack::updateFormatAlignmentActions);
	connect(document, &Document::changedName, this, &Stack::updateFormatActions);
	connect(document, &Document::scenesVisible, this, &Stack::setScenesVisible);
	connect(document, &Document::editorTyped, this, &Stack::editorTyped);
	connect(document, &Document::pointerMoved, this, [this](const QPoint& global) {
		const int y = mapFromGlobal(global).y();
		Q_EMIT pointerMoved(y, height(), presentationScale());
	});
	connect(document->text(), &QTextEdit::copyAvailable, this, &Stack::copyAvailable);
	connect(document->text(), &QTextEdit::redoAvailable, this, &Stack::redoAvailable);
	connect(document->text(), &QTextEdit::undoAvailable, this, &Stack::undoAvailable);
	connect(document->text(), &QTextEdit::currentCharFormatChanged, this, &Stack::updateFormatActions);

	m_documents.append(document);
	m_contents->addWidget(document);
	m_contents->setCurrentWidget(document);

	QAction* action = new QAction(this);
	action->setCheckable(true);
	action->setActionGroup(m_menu_group);
	m_document_actions.push_back(action);
	m_menu->addAction(action);
	updateMenuIndexes();

	document->loadTheme(m_theme, presentationScale());

	Q_EMIT documentAdded(document);
	Q_EMIT updateFormatActions();
}

//-----------------------------------------------------------------------------

void Stack::moveDocument(int from, int to)
{
	QAction* action = m_document_actions.at(from);
	QAction* before = m_document_actions.at(to);
	m_menu->removeAction(action);
	m_documents.move(from, to);
	m_document_actions.move(from, to);
	m_menu->insertAction(before, action);
	updateMenuIndexes();
}

//-----------------------------------------------------------------------------

void Stack::removeDocument(int index)
{
	Document* document = m_documents.takeAt(index);
	m_contents->removeWidget(document);

	QAction* action = m_document_actions.takeAt(index);
	m_menu->removeAction(action);
	delete action;
	updateMenuIndexes();

	Q_EMIT documentRemoved(document);
	document->close();
}

//-----------------------------------------------------------------------------

void Stack::updateDocument(int index)
{
	const Document* document = m_documents.at(index);
	QAction* action = m_document_actions.at(index);
	action->setText(document->title() + (document->isModified() ? "*" : QString()));
	action->setToolTip(QDir::toNativeSeparators(document->filename()));
}

//-----------------------------------------------------------------------------

void Stack::setCurrentDocument(int index)
{
	m_current_document = m_documents[index];
	m_contents->setCurrentWidget(m_current_document);
	m_scenes->setDocument(m_current_document);
	m_document_actions[index]->setChecked(true);

	Q_EMIT copyAvailable(!m_current_document->text()->textCursor().selectedText().isEmpty());
	Q_EMIT redoAvailable(m_current_document->text()->document()->isRedoAvailable());
	Q_EMIT undoAvailable(m_current_document->text()->document()->isUndoAvailable());
	Q_EMIT updateFormatActions();
}

//-----------------------------------------------------------------------------

void Stack::setMargins(int footer, int header)
{
	m_footer_margin = footer;
	m_header_margin = header;
	m_footer_visible = (m_footer_visible != 0) ? -m_footer_margin : 0;
	m_header_visible = (m_header_visible != 0) ? m_header_margin : 0;
	updateMargin();
	updateBackground();
	updateMask();
	showHeader();
}

//-----------------------------------------------------------------------------

void Stack::waitForThemeBackground()
{
	if (m_theme_renderer->isRunning()) {
		m_theme_renderer->wait();
		repaint();
	}
}

//-----------------------------------------------------------------------------

bool Stack::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::MouseMove) {
		mouseMoveEvent(static_cast<QMouseEvent*>(event));
	}
	return QWidget::eventFilter(watched, event);
}

//-----------------------------------------------------------------------------

int Stack::presentationScale() const
{
	return PresentationGeometry::calculate({ width(), height() }, m_side_panel->isVisible(), m_panel_side).scale;
}

//-----------------------------------------------------------------------------

void Stack::showInSidePanel(QWidget* tool, const QString& title)
{
	if (QDialog* dialog = qobject_cast<QDialog*>(tool)) {
		disconnect(dialog, &QDialog::finished, m_side_panel, nullptr);
		connect(dialog, &QDialog::finished, m_side_panel, &SnesSidePanel::closePanel);
	}
	m_side_panel->showTool(tool, title);
}

//-----------------------------------------------------------------------------

void Stack::closeSidePanel()
{
	m_side_panel->closePanel();
}

//-----------------------------------------------------------------------------

void Stack::setPanelSide(PanelSide side)
{
	if (m_panel_side == side) {
		return;
	}
	m_panel_side = side;
	QSettings().setValue("Presentation/PanelSide", side == PanelSide::Left ? "left" : "right");
	updateBackground();
}

//-----------------------------------------------------------------------------

void Stack::alignCenter()
{
	m_current_document->setRichText(true);
	m_current_document->text()->setAlignment(Qt::AlignCenter);
}

//-----------------------------------------------------------------------------

void Stack::alignJustify()
{
	m_current_document->setRichText(true);
	m_current_document->text()->setAlignment(Qt::AlignJustify);
}

//-----------------------------------------------------------------------------

void Stack::alignLeft()
{
	m_current_document->setRichText(true);
	m_current_document->text()->setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
}

//-----------------------------------------------------------------------------

void Stack::alignRight()
{
	m_current_document->setRichText(true);
	m_current_document->text()->setAlignment(Qt::AlignRight | Qt::AlignAbsolute);
}

//-----------------------------------------------------------------------------

void Stack::autoCache()
{
	for (Document* document : std::as_const(m_documents)) {
		if (document->isModified()) {
			document->cache();
		}
	}
}

//-----------------------------------------------------------------------------

void Stack::checkSpelling()
{
	m_current_document->checkSpelling();
}

//-----------------------------------------------------------------------------

void Stack::cut()
{
	m_current_document->text()->cut();
}

//-----------------------------------------------------------------------------

void Stack::copy()
{
	m_current_document->text()->copy();
}

//-----------------------------------------------------------------------------

void Stack::decreaseIndent()
{
	m_current_document->setRichText(true);
	QTextCursor cursor = m_current_document->text()->textCursor();
	QTextBlockFormat format = cursor.blockFormat();
	format.setIndent(std::max(0, format.indent() - 1));
	cursor.setBlockFormat(format);
	Q_EMIT updateFormatActions();
}

//-----------------------------------------------------------------------------

void Stack::find()
{
	showInSidePanel(m_find_dialog, tr("Find"));
	m_find_dialog->showFindMode();
}

//-----------------------------------------------------------------------------

void Stack::findNext()
{
	m_find_dialog->findNext();
}

//-----------------------------------------------------------------------------

void Stack::findPrevious()
{
	m_find_dialog->findPrevious();
}

//-----------------------------------------------------------------------------

void Stack::increaseIndent()
{
	m_current_document->setRichText(true);
	QTextCursor cursor = m_current_document->text()->textCursor();
	QTextBlockFormat format = cursor.blockFormat();
	format.setIndent(format.indent() + 1);
	cursor.setBlockFormat(format);
	Q_EMIT updateFormatActions();
}

//-----------------------------------------------------------------------------

void Stack::paste()
{
	m_current_document->text()->paste();
}

//-----------------------------------------------------------------------------

void Stack::pasteUnformatted()
{
	const QString text = QApplication::clipboard()->text(QClipboard::Clipboard);
	m_current_document->text()->insertPlainText(text);
}

//-----------------------------------------------------------------------------

void Stack::pageSetup()
{
	initPrinter();
	QPageSetupDialog dialog(m_printer, this);
	dialog.exec();
}

//-----------------------------------------------------------------------------

void Stack::print()
{
	initPrinter();
	m_current_document->print(m_printer);
}

//-----------------------------------------------------------------------------

void Stack::redo()
{
	m_current_document->text()->redo();
}

//-----------------------------------------------------------------------------

void Stack::reload()
{
	m_current_document->reload();
}

//-----------------------------------------------------------------------------

void Stack::replace()
{
	showInSidePanel(m_find_dialog, tr("Replace"));
	m_find_dialog->showReplaceMode();
}

//-----------------------------------------------------------------------------

void Stack::save()
{
	m_current_document->save();
}

//-----------------------------------------------------------------------------

void Stack::saveAs()
{
	m_current_document->saveAs();
}

//-----------------------------------------------------------------------------

void Stack::selectAll()
{
	m_current_document->text()->selectAll();
}

//-----------------------------------------------------------------------------

void Stack::selectScene()
{
	m_current_document->sceneModel()->selectScene();
}

//-----------------------------------------------------------------------------

void Stack::setFocusMode(QAction* action)
{
	const int focus_mode = action->data().toInt();
	for (Document* document : std::as_const(m_documents)) {
		document->setFocusMode(focus_mode);
	}
}

//-----------------------------------------------------------------------------

void Stack::setBlockHeading(int heading)
{
	m_current_document->setRichText(true);
	QTextCursor cursor = m_current_document->text()->textCursor();
	QTextBlockFormat block_format = cursor.blockFormat();
	block_format.setHeadingLevel(heading);
	cursor.setBlockFormat(block_format);
}

//-----------------------------------------------------------------------------

void Stack::setFontBold(bool bold)
{
	m_current_document->setRichText(true);
	m_current_document->text()->setFontWeight(bold ? QFont::Bold : QFont::Normal);
}

//-----------------------------------------------------------------------------

void Stack::setFontItalic(bool italic)
{
	m_current_document->setRichText(true);
	m_current_document->text()->setFontItalic(italic);
}

//-----------------------------------------------------------------------------

void Stack::setFontStrikeOut(bool strikeout)
{
	m_current_document->setRichText(true);
	QTextCharFormat format;
	format.setFontStrikeOut(strikeout);
	m_current_document->text()->mergeCurrentCharFormat(format);
}

//-----------------------------------------------------------------------------

void Stack::setFontUnderline(bool underline)
{
	m_current_document->setRichText(true);
	m_current_document->text()->setFontUnderline(underline);
}

//-----------------------------------------------------------------------------

void Stack::setFontSuperScript(bool super)
{
	m_current_document->setRichText(true);
	QTextCharFormat format;
	format.setVerticalAlignment(super ? QTextCharFormat::AlignSuperScript : QTextCharFormat::AlignNormal);
	m_current_document->text()->mergeCurrentCharFormat(format);
}

//-----------------------------------------------------------------------------

void Stack::setFontSubScript(bool sub)
{
	m_current_document->setRichText(true);
	QTextCharFormat format;
	format.setVerticalAlignment(sub ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
	m_current_document->text()->mergeCurrentCharFormat(format);
}

//-----------------------------------------------------------------------------

void Stack::setTextDirectionLTR()
{
	if (m_current_document) {
		m_current_document->setRichText(true);
		QTextCursor cursor = m_current_document->text()->textCursor();
		QTextBlockFormat format = cursor.blockFormat();
		format.setLayoutDirection(Qt::LeftToRight);
		format.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
		cursor.mergeBlockFormat(format);
		Q_EMIT updateFormatAlignmentActions();
	}
}

//-----------------------------------------------------------------------------

void Stack::setTextDirectionRTL()
{
	if (m_current_document) {
		m_current_document->setRichText(true);
		QTextCursor cursor = m_current_document->text()->textCursor();
		QTextBlockFormat format = cursor.blockFormat();
		format.setLayoutDirection(Qt::RightToLeft);
		format.setAlignment(Qt::AlignRight | Qt::AlignAbsolute);
		cursor.mergeBlockFormat(format);
		Q_EMIT updateFormatAlignmentActions();
	}
}

//-----------------------------------------------------------------------------

void Stack::showSymbols()
{
	// Load symbols dialog on demand
	if (!m_symbols_dialog) {
		window()->setCursor(Qt::WaitCursor);
		m_symbols_dialog = new SymbolsDialog(this);
		m_symbols_dialog->setInsertEnabled(!m_current_document->isReadOnly());
		m_symbols_dialog->setPreviewFont(m_current_document->text()->font());
		connect(m_symbols_dialog, &SymbolsDialog::insertText, this, &Stack::insertSymbol);
		window()->unsetCursor();
	}

	showInSidePanel(m_symbols_dialog, tr("Symbols"));
}

//-----------------------------------------------------------------------------

void Stack::themeSelected(const Theme& theme)
{
	if (m_theme_preview_active) {
		updateThemePreview(theme);
		return;
	}
	m_theme = theme;
	SnesStyle::applyTheme(theme);

	if (m_symbols_dialog) {
		m_symbols_dialog->setPreviewFont(m_theme.textFont());
	}

	updateMargin();
	for (Document* document : std::as_const(m_documents)) {
		document->loadTheme(theme, presentationScale());
	}
	updateBackground();
}

//-----------------------------------------------------------------------------

void Stack::beginThemePreview(const Theme& theme)
{
	if (!m_theme_preview_active) {
		m_theme_before_preview = m_theme;
		m_theme_preview_active = true;
		m_theme_renderer->setCacheLimit(2);
		if (!m_theme_preview) {
			m_theme_preview = new ThemePreviewDocument(this);
		}
		m_theme_preview->cloneFrom(m_current_document ? m_current_document->text()->document() : nullptr);
		m_contents->hide();
		m_theme_preview->show();
		m_theme_preview->raise();
	}
	updateThemePreview(theme);
}

//-----------------------------------------------------------------------------

void Stack::updateThemePreview(const Theme& theme)
{
	if (!m_theme_preview_active) {
		beginThemePreview(theme);
		return;
	}
	m_theme = theme;
	SnesStyle::applyTheme(theme);
	if (m_theme_preview) {
		m_theme_preview->applyTheme(theme, presentationScale());
	}
	updateMargin();
	updatePanelGeometry();
	updateBackground();
}

//-----------------------------------------------------------------------------

void Stack::commitThemePreview(const Theme& theme)
{
	if (m_theme_preview_active) {
		m_theme_preview_active = false;
		if (m_theme_preview) {
			m_theme_preview->hide();
		}
		m_contents->show();
		m_theme_renderer->setCacheLimit(10);
	}
	themeSelected(theme);
}

//-----------------------------------------------------------------------------

void Stack::cancelThemePreview()
{
	if (!m_theme_preview_active) {
		return;
	}
	const Theme restore = m_theme_before_preview;
	m_theme_preview_active = false;
	if (m_theme_preview) {
		m_theme_preview->hide();
	}
	m_contents->show();
	m_theme_renderer->setCacheLimit(10);
	m_theme = restore;
	SnesStyle::applyTheme(restore);
	// Real documents were never rethemed during the draft. Snap the stored
	// presentation scale to the now-current geometry so cancellation cannot
	// rewrite their QTextDocument block formats merely to restore appearance.
	m_presentation_scale = presentationScale();
	updateMargin();
	updatePanelGeometry();
	updateBackground();
}

//-----------------------------------------------------------------------------

void Stack::undo()
{
	m_current_document->text()->undo();
}

//-----------------------------------------------------------------------------

void Stack::updateSmartQuotes()
{
	SmartQuotes::replace(m_current_document->text(), 0, m_current_document->text()->document()->characterCount());
	m_current_document->centerCursor(true);
}

//-----------------------------------------------------------------------------

void Stack::updateSmartQuotesSelection()
{
	const QTextCursor cursor = m_current_document->text()->textCursor();
	SmartQuotes::replace(m_current_document->text(), cursor.selectionStart(), cursor.selectionEnd());
}

//-----------------------------------------------------------------------------

void Stack::setFooterVisible(bool visible)
{
	const int footer_visible = visible * -m_footer_margin;
	if (m_footer_visible != footer_visible) {
		Q_EMIT footerVisible(visible);
		m_footer_visible = footer_visible;
		updateMask();
	}
}

//-----------------------------------------------------------------------------

void Stack::setHeaderVisible(bool visible)
{
	const int header_visible = visible * m_header_margin;
	if (m_header_visible != header_visible) {
		Q_EMIT headerVisible(visible);
		m_header_visible = header_visible;
		updateMask();
	}
}

//-----------------------------------------------------------------------------

void Stack::setScenesVisible(bool visible)
{
	if (!visible && !m_scenes->scenesVisible()) {
		m_scenes->hide();
	} else {
		m_scenes->show();
	}
}

//-----------------------------------------------------------------------------

void Stack::showHeader()
{
	const QPoint point = mapFromGlobal(QCursor::pos());
	setHeaderVisible(window()->rect().contains(point) && point.y() <= m_header_margin);
}

//-----------------------------------------------------------------------------

void Stack::mouseMoveEvent(QMouseEvent* event)
{
	const int y = mapFromGlobal(event->globalPosition()).y();
	Q_EMIT pointerMoved(y, height(), presentationScale());
	const bool chrome_region = y <= m_header_margin || y >= (height() - m_footer_margin);
	setScenesVisible(false);

	if (m_current_document) {
		if (chrome_region) {
			m_current_document->setScrollBarVisible(false);
		} else {
			m_current_document->mouseMoveEvent(event);
		}
	}
}

//-----------------------------------------------------------------------------

void Stack::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	const qreal pixelratio = devicePixelRatioF();
	const QRectF rect(event->rect().topLeft() * pixelratio, event->rect().size() * pixelratio);
	painter.drawPixmap(event->rect(), m_background, rect);
	painter.end();
}

//-----------------------------------------------------------------------------

void Stack::resizeEvent(QResizeEvent* event)
{
	updateMask();
	m_resize_timer->start();
	updateBackground();
	QWidget::resizeEvent(event);
}

//-----------------------------------------------------------------------------

void Stack::actionTriggered(const QAction* action)
{
	Q_EMIT documentSelected(action->data().toInt());
}

//-----------------------------------------------------------------------------

void Stack::insertSymbol(const QString& text)
{
	m_current_document->text()->insertPlainText(text);
}

//-----------------------------------------------------------------------------

void Stack::updateBackground()
{
	const int margin = m_layout->rowMinimumHeight(0);
	const qreal pixelratio = devicePixelRatioF();
	updatePanelGeometry();

	// Create temporary background
	const bool panel_visible = m_side_panel->isVisible();
	const QRectF foreground = m_theme.foregroundRect(size(), margin, pixelratio, panel_visible, m_panel_side);

	// The synchronous frame only bridges the async presentation render, so it
	// stays flat and opaque: no alpha blend and no antialiased rounding may
	// flash before the quantized SNES frame replaces it.
	QImage image(size() * pixelratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(pixelratio);
	image.fill(m_theme.loadColor().rgb());
	if (m_theme.foregroundOpacity() == 100) {
		QPainter painter(&image);
		painter.setPen(Qt::NoPen);
		painter.fillRect(foreground, m_theme.foregroundColor());
	}

	applyRenderedBackground(image, foreground.toRect(), m_theme);

	// Create proper background
	if (!m_resize_timer->isActive()) {
		m_theme_renderer->create(m_theme, size(), margin, pixelratio, panel_visible, m_panel_side);
	}
}

//-----------------------------------------------------------------------------

void Stack::applyRenderedBackground(const QImage& image, const QRect& foreground, const Theme& theme)
{
	// A render may finish after a panel move, resize, or theme switch. Never
	// allow stale presentation state to replace the synchronous frame.
	const QRect expected_foreground = m_theme.foregroundRect(
		size(), m_layout->rowMinimumHeight(0), devicePixelRatioF(),
		m_side_panel->isVisible(), m_panel_side);
	if (!(theme == m_theme)
			|| foreground != expected_foreground
			|| image.size() != (size() * devicePixelRatioF())) {
		return;
	}

	// Load background
	m_background = QPixmap::fromImage(image, Qt::AutoColor | Qt::AvoidDither);
	m_background.setDevicePixelRatio(devicePixelRatioF());

	update();
}

//-----------------------------------------------------------------------------

void Stack::updateMargin()
{
	int margin = std::max(m_theme.foregroundMargin().value(), 1);
	if (Preferences::instance().alwaysShowFooter()) {
		margin = std::max(m_footer_margin, margin);
	}
	if (Preferences::instance().alwaysShowHeader()) {
		margin = std::max(m_header_margin, margin);
	}
	m_layout->setRowMinimumHeight(0, margin);
	m_layout->setRowMinimumHeight(5, margin);
	m_layout->setColumnMinimumWidth(0, margin);
	m_layout->setColumnMinimumWidth(5, margin);

	const int minimum_size = (margin * 2) + (m_theme.foregroundPadding() * 2) + 100;
	window()->setMinimumSize(minimum_size, minimum_size);
}

//-----------------------------------------------------------------------------

void Stack::updatePanelGeometry()
{
	const bool panel_visible = m_side_panel->isVisible();
	const PresentationLayout layout = PresentationGeometry::calculate(
		{ width(), height() }, panel_visible, m_panel_side);
	const int frame = (m_theme.shellMode() == 1 ? 7 : 6) * layout.scale;
	const bool scale_changed = m_presentation_scale != layout.scale;
	if (scale_changed) {
		m_presentation_scale = layout.scale;
		if (m_theme_preview_active && m_theme_preview) {
			m_theme_preview->applyTheme(m_theme, layout.scale);
		} else {
			for (Document* document : std::as_const(m_documents)) {
				document->loadTheme(m_theme, layout.scale);
			}
		}
		Q_EMIT presentationScaleChanged(layout.scale);
	}
	const QRect document(layout.document.x, layout.document.y, layout.document.width, layout.document.height);
	const QRect paper = document.adjusted(frame, frame, -frame, -frame);
	const bool editor_resized = m_contents->size() != paper.size();
	// PresentationSurface paints the six-pixel frame. The document stack owns
	// exactly the paper interior, and Document owns only its page padding.
	// This makes panel geometry and text reflow one synchronous state change.
	m_contents->setGeometry(paper);
	if (m_theme_preview) {
		m_theme_preview->setGeometry(paper);
	}
	if (editor_resized && !scale_changed) {
		for (Document* document_widget : std::as_const(m_documents)) {
			document_widget->centerCursor(true);
		}
	}
	const QRect panel(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height);
	if (panel_visible) {
		m_side_panel->setGeometry(panel.adjusted(frame, frame, -frame, -frame));
		m_side_panel->raise();
	}
	m_wallpaper_motion->setGeometry(rect());
	m_wallpaper_motion->setPresentationState(m_theme, document, panel, panel_visible, m_panel_side);
}

//-----------------------------------------------------------------------------

void Stack::setWallpaperMotionEnabled(bool enabled)
{
	m_wallpaper_motion->setMotionPreference(enabled);
}

//-----------------------------------------------------------------------------

void Stack::updateMask()
{
	clearMask();
	raise();

	if (m_scenes->isVisible()) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		m_scenes->update();
		m_scenes->clearFocus();
		m_scenes->setFocus();
	}
	if (m_header_visible || m_footer_visible) {
		setMask(rect().adjusted(0, m_header_visible, 0, m_footer_visible));
	}
}

//-----------------------------------------------------------------------------

void Stack::updateMenuIndexes()
{
	for (int i = 0, count = m_document_actions.count(); i < count; ++i) {
		m_document_actions[i]->setData(i);
	}
}

//-----------------------------------------------------------------------------

void Stack::initPrinter()
{
	if (m_printer) {
		return;
	}

	m_printer = new QPrinter(QPrinter::HighResolution);
	m_printer->setPageSize(QPageSize(QPageSize::Letter));
	m_printer->setPageOrientation(QPageLayout::Portrait);
	m_printer->setPageMargins(QMarginsF(1.0, 1.0, 1.0, 1.0), QPageLayout::Inch);
}

//-----------------------------------------------------------------------------
