/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "theme_manager.h"

#include "gzip.h"
#include "session.h"
#include "theme.h"
#include "theme_dialog.h"
#include "utils.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtConcurrentRun>
#include <QUrl>
#include <QWidget>
#include <QVBoxLayout>

#include <memory>

//-----------------------------------------------------------------------------

namespace
{

constexpr int ThemeDefaultRole = Qt::UserRole + 1;

}

class ThemePreview final : public QWidget
{
public:
	explicit ThemePreview(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumSize(1, 1);
	}

	void setIcon(const QIcon& icon)
	{
		m_icon = icon;
		update();
	}

	QSize sizeHint() const override { return QSize(258, 153); }
	QSize minimumSizeHint() const override { return QSize(86, 51); }

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)
		QPainter painter(this);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
		painter.fillRect(rect(), palette().base());
		if (m_icon.isNull()) {
			return;
		}
		const QPixmap preview = m_icon.pixmap(QSize(516, 306));
		QSize target = preview.size();
		target.scale(size(), Qt::KeepAspectRatio);
		const QRect target_rect(QPoint((width() - target.width()) / 2, (height() - target.height()) / 2), target);
		painter.drawPixmap(target_rect, preview, preview.rect());
	}

private:
	QIcon m_icon;
};

//-----------------------------------------------------------------------------

ThemeManager::ThemeManager(QSettings& settings, QWidget* parent)
	: QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_settings(settings)
{
	setWindowTitle(tr("Themes"));
	setProperty("fairywriter.panelFill", true);

	// Theme provenance is storage metadata, not a navigation hierarchy. Keep one
	// selection and preview path so applying a theme never depends on which page
	// happens to be open.
	m_themes = new QComboBox(this);
	m_themes->setObjectName(QStringLiteral("FairyWriterThemeSelector"));
	m_themes->setIconSize(QSize(64, 38));
	m_themes->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_themes->setMinimumContentsLength(8);
	m_themes->setMinimumHeight(m_themes->sizeHint().height());
	m_preview = new ThemePreview(this);
	m_preview->setObjectName(QStringLiteral("FairyWriterThemePreview"));
	addItem("pastelmeadow", true, tr("Pastel Meadow"));
	addItem("hunterterminal", true, tr("Hunter Terminal"));
	addItem("bitterskies", true, tr("Bitter Skies"));
	addItem("enchantment", true, tr("Enchantment"));
	addItem("gentleblues", true, tr("Gentle Blues"));
	addItem("oldschool", true, tr("Old School"));
	addItem("spacedreams", true, tr("Space Dreams"));
	addItem("spygames", true, tr("Spy Games"));
	addItem("tranquility", true, tr("Tranquility"));
	addItem("writingdesk", true, tr("Writing Desk"));
	QDir dir(Theme::path(), "*.theme");
	const QStringList themes = dir.entryList(QDir::Files, QDir::Name | QDir::IgnoreCase);
	for (const QString& theme : themes) {
		QString name = QSettings(dir.filePath(theme), QSettings::IniFormat).value("Name").toString();
		if (!name.isEmpty()) {
			addItem(QFileInfo(theme).completeBaseName(), false, name);
		} else {
			name = QUrl::fromPercentEncoding(QFileInfo(theme).completeBaseName().toUtf8());
			QSettings(dir.filePath(theme), QSettings::IniFormat).setValue("Name", name);

			const QString id = Theme::createId();
			dir.rename(theme, id + ".theme");
			dir.remove(QFileInfo(theme).completeBaseName() + ".png");

			QStringList sessions = QDir(Session::path(), "*.session").entryList(QDir::Files);
			sessions.prepend(QString());
			for (const QString& file : std::as_const(sessions)) {
				Session session(file);
				if ((session.theme() == name) && (session.themeDefault() == false)) {
					session.setTheme(id, false);
				}
			}

			addItem(id, false, name);
		}
	}

	m_new = new QPushButton(tr("New Theme"), this);
	m_edit = new QPushButton(tr("Edit"), this);
	m_duplicate = new QPushButton(tr("Duplicate"), this);
	m_more = new QPushButton(tr("More"), this);
	m_new->setObjectName(QStringLiteral("FairyWriterThemeNew"));
	m_edit->setObjectName(QStringLiteral("FairyWriterThemeEdit"));
	m_duplicate->setObjectName(QStringLiteral("FairyWriterThemeDuplicate"));
	m_more->setObjectName(QStringLiteral("FairyWriterThemeMore"));
	for (QPushButton* button : {m_new, m_edit, m_duplicate, m_more}) {
		button->setAutoDefault(false);
	}
	connect(m_new, &QPushButton::clicked, this, &ThemeManager::newTheme);
	connect(m_edit, &QPushButton::clicked, this, &ThemeManager::editTheme);
	connect(m_duplicate, &QPushButton::clicked, this, &ThemeManager::cloneTheme);
	QMenu* more_menu = new QMenu(m_more);
	m_import = more_menu->addAction(tr("Import"), this, &ThemeManager::importTheme);
	m_export = more_menu->addAction(tr("Export"), this, &ThemeManager::exportTheme);
	more_menu->addSeparator();
	m_delete = more_menu->addAction(tr("Delete"), this, &ThemeManager::deleteTheme);
	m_import->setObjectName(QStringLiteral("FairyWriterThemeImport"));
	m_export->setObjectName(QStringLiteral("FairyWriterThemeExport"));
	m_delete->setObjectName(QStringLiteral("FairyWriterThemeDelete"));
	m_more->setMenu(more_menu);

	QGridLayout* actions = new QGridLayout;
	actions->setContentsMargins(0, 0, 0, 0);
	actions->setSpacing(4);
	actions->addWidget(m_new, 0, 0, 1, 3);
	actions->addWidget(m_edit, 1, 0);
	actions->addWidget(m_duplicate, 1, 1);
	actions->addWidget(m_more, 1, 2);

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
	buttons->setMinimumHeight(buttons->sizeHint().height());
	connect(buttons, &QDialogButtonBox::rejected, this, &ThemeManager::reject);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(m_themes);
	layout->addWidget(m_preview, 1);
	layout->addLayout(actions);
	layout->addWidget(buttons);

	const QString theme = m_settings.value("ThemeManager/Theme").toString();
	const bool is_default = m_settings.value("ThemeManager/ThemeDefault", false).toBool();
	if (!selectItem(theme, is_default)) {
		selectItem(Theme::defaultId(), true);
	}
	connect(m_themes, &QComboBox::currentIndexChanged, this, &ThemeManager::currentThemeChanged);
	currentThemeChanged();

	resize(m_settings.value("ThemeManager/Size", sizeHint()).toSize());
}

//-----------------------------------------------------------------------------

void ThemeManager::hideEvent(QHideEvent* event)
{
	m_settings.setValue("ThemeManager/Size", size());
	QDialog::hideEvent(event);
}

//-----------------------------------------------------------------------------

void ThemeManager::newTheme()
{
	Q_EMIT editRequested(Theme::neutralDraft(), true);
}

//-----------------------------------------------------------------------------

void ThemeManager::editTheme()
{
	const int index = m_themes->currentIndex();
	if (index < 0 || currentThemeIsDefault()) {
		return;
	}

	Q_EMIT editRequested(Theme(m_themes->itemData(index).toString(), false), false);
}

//-----------------------------------------------------------------------------

void ThemeManager::cloneTheme()
{
	const int source_index = m_themes->currentIndex();
	if (source_index < 0) {
		return;
	}

	const Theme source_theme(m_themes->itemData(source_index).toString(), currentThemeIsDefault());
	Theme draft = Theme::neutralDraft();
	draft.applyDesign(source_theme);
	const QStringList values = splitStringAtLastNumber(m_themes->itemText(source_index));
	int count = values.at(1).toInt();
	QString name;
	do {
		name = values.at(0) + QString::number(++count);
	} while (Theme::exists(name));
	draft.setName(name);
	Q_EMIT editRequested(draft, true);
}

//-----------------------------------------------------------------------------

void ThemeManager::deleteTheme()
{
	const int index = m_themes->currentIndex();
	if (index < 0 || currentThemeIsDefault() || (QMessageBox::question(this,
			tr("Question"),
			tr("Delete theme '%1'?").arg(m_themes->itemText(index)),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No)) {
		return;
	}

	const QString id = m_themes->itemData(index).toString();
	QFile::remove(Theme::filePath(id));
	Theme::removeIcon(id, false);
	m_themes->removeItem(index);
	updateActions();
}

//-----------------------------------------------------------------------------

void ThemeManager::importTheme()
{
	// Find file to import
	QSettings settings;
	QString path = settings.value("ThemeManager/Location").toString();
	if (path.isEmpty() || !QFile::exists(path)) {
		path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	}
	const QString filename = QFileDialog::getOpenFileName(this, tr("Import Theme"), path, tr("Themes (%1)").arg("*.fwtz *.theme"));
	if (filename.isEmpty()) {
		return;
	}
	settings.setValue("ThemeManager/Location", QFileInfo(filename).absolutePath());

	const QString id = Theme::createId();

	// Uncompress theme
	const QString theme_filename = Theme::filePath(id);
	const QByteArray theme_data = gunzip(filename);
	{
		QFile file(theme_filename);
		if (file.open(QFile::WriteOnly)) {
			file.write(theme_data);
			file.close();
		}
	}

	// Find theme name
	QSettings theme_ini(theme_filename, QSettings::IniFormat);
	QString name = theme_ini.value("Name", QFileInfo(filename).completeBaseName()).toString();
	{
		theme_ini.setValue("Name", id);
		const QStringList values = splitStringAtLastNumber(name);
		int count = values.at(1).toInt();
		while (Theme::exists(name)) {
			++count;
			name = values.at(0) + QString::number(count);
		}
		theme_ini.setValue("Name", name);
	}

	// Extract and use background image
	const QByteArray image_data = QByteArray::fromBase64(theme_ini.value("Data/Image").toByteArray());
	const QString image_file = theme_ini.value("Background/ImageFile").toString();
	theme_ini.remove("Background/ImageFile");
	theme_ini.remove("Data/Image");
	theme_ini.sync();

	if (!image_data.isEmpty()) {
		QTemporaryFile file(QDir::tempPath() + "/XXXXXX-" + image_file);
		if (file.open()) {
			file.write(image_data);
			file.close();
		}

		Theme theme(id, false);
		theme.setBackgroundImage(file.fileName());
		theme.saveChanges();
	}

	theme_ini.sync();
	theme_ini.remove("Background/Image");

	const int index = addItem(id, false, name);
	m_themes->setCurrentIndex(index);
}

//-----------------------------------------------------------------------------

void ThemeManager::exportTheme()
{
	const int index = m_themes->currentIndex();
	if (index < 0 || currentThemeIsDefault()) {
		return;
	}

	// Find export file name
	QSettings settings;
	QString path = settings.value("ThemeManager/Location").toString();
	if (path.isEmpty() || !QFile::exists(path)) {
		path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	}
	path = path + "/" + m_themes->itemText(index) + ".fwtz";
	QString filename = QFileDialog::getSaveFileName(this, tr("Export Theme"), path, tr("Themes (%1)").arg("*.fwtz"), 0, QFileDialog::DontResolveSymlinks);
	if (filename.isEmpty()) {
		return;
	}
	if (!filename.endsWith(".fwtz")) {
		filename += ".fwtz";
	}
	settings.setValue("ThemeManager/Location", QFileInfo(filename).absolutePath());

	// Copy theme
	QFile::remove(filename);
	QFile::copy(Theme::filePath(m_themes->itemData(index).toString()), filename);

	// Store image in export file
	{
		QSettings theme_ini(filename, QSettings::IniFormat);
		theme_ini.remove("Background/Image");

		const QString image = theme_ini.value("Background/ImageFile").toString();
		if (!image.isEmpty()) {
			QFile file(Theme::path() + "/Images/" + image);
			if (file.open(QFile::ReadOnly)) {
				theme_ini.setValue("Data/Image", file.readAll().toBase64());
				file.close();
			}
		}
	}

	// Compress theme
	gzip(filename);
}

//-----------------------------------------------------------------------------

void ThemeManager::currentThemeChanged()
{
	const int index = m_themes->currentIndex();
	if (index < 0) {
		m_preview->setIcon(QIcon());
		updateActions();
		return;
	}

	const bool is_default = currentThemeIsDefault();
	m_preview->setIcon(m_themes->itemIcon(index));
	updateActions();

	const QString id = m_themes->itemData(index).toString();
	m_settings.setValue("ThemeManager/Theme", id);
	m_settings.setValue("ThemeManager/ThemeDefault", is_default);
	Q_EMIT themeSelected(Theme(id, is_default));
}

//-----------------------------------------------------------------------------

int ThemeManager::addItem(const QString& id, bool is_default, const QString& name)
{
	const qreal pixelratio = devicePixelRatioF();
	const QString icon_path = Theme::iconPath(id, is_default, pixelratio);
	const bool needs_icon = !QFile::exists(icon_path)
		|| QImageReader(icon_path).size() != (QSize(258, 153) * pixelratio);

	// Insert immediately so opening the panel never blocks on rendering;
	// missing icons start as a flat load-color placeholder.
	QIcon icon;
	if (needs_icon) {
		QPixmap placeholder(QSize(258, 153) * pixelratio);
		placeholder.setDevicePixelRatio(pixelratio);
		placeholder.fill(Theme(id, is_default).loadColor());
		icon = QIcon(placeholder);
	} else {
		icon = QIcon(icon_path);
	}
	m_themes->addItem(icon, name, id);
	const int index = m_themes->count() - 1;
	m_themes->setItemData(index, is_default, ThemeDefaultRole);
	m_themes->setItemData(index, name, Qt::ToolTipRole);
	if (needs_icon) {
		scheduleIconGeneration(id, is_default);
	}
	return index;
}

//-----------------------------------------------------------------------------

void ThemeManager::scheduleIconGeneration(const QString& id, bool is_default)
{
	const qreal pixelratio = devicePixelRatioF();
	Theme theme(id, is_default);

	// Find load color in separate thread
	if (!theme.isDefault() && (theme.loadColor() == theme.backgroundColor())) {
		auto* load_watcher = new QFutureWatcher<QColor>(this);
		connect(load_watcher, &QFutureWatcher<QColor>::finished, this, [this, id, is_default, load_watcher] {
			if (load_watcher->future().resultCount()) {
				Theme theme(id, is_default);
				theme.setLoadColor(load_watcher->result());
				theme.saveChanges();
			}
			load_watcher->deleteLater();
		});
		load_watcher->setFuture(theme.calculateLoadColor());
	}

	// The presentation render is thread-safe, but renderText builds widgets
	// and must stay on the UI thread; only the heavy pass moves off it.
	auto* render_watcher = new QFutureWatcher<QImage>(this);
	auto foreground = std::make_shared<QRect>();
	connect(render_watcher, &QFutureWatcher<QImage>::finished, this,
			[this, id, is_default, pixelratio, foreground, render_watcher] {
		const QImage background = render_watcher->result();
		render_watcher->deleteLater();
		int index = -1;
		for (int i = 0; i < m_themes->count(); ++i) {
			if (m_themes->itemData(i).toString() == id
					&& m_themes->itemData(i, ThemeDefaultRole).toBool() == is_default) {
				index = i;
				break;
			}
		}
		if (index < 0) {
			return;
		}
		QImage icon;
		Theme(id, is_default).renderText(background, *foreground, pixelratio, nullptr, &icon);
		icon.save(Theme::iconPath(id, is_default, pixelratio));
		m_themes->setItemIcon(index, QIcon(Theme::iconPath(id, is_default, pixelratio)));
		if (index == m_themes->currentIndex()) {
			m_preview->setIcon(m_themes->itemIcon(index));
		}
	});
	render_watcher->setFuture(QtConcurrent::run([theme, pixelratio, foreground] {
		return theme.render(QSize(1920, 1080), *foreground, 0, pixelratio);
	}));
}

//-----------------------------------------------------------------------------

bool ThemeManager::selectItem(const QString& id, bool is_default)
{
	for (int index = 0; index < m_themes->count(); ++index) {
		if (m_themes->itemData(index).toString() == id
				&& m_themes->itemData(index, ThemeDefaultRole).toBool() == is_default) {
			m_themes->setCurrentIndex(index);
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------

bool ThemeManager::currentThemeIsDefault() const
{
	const int index = m_themes->currentIndex();
	return index >= 0 && m_themes->itemData(index, ThemeDefaultRole).toBool();
}

//-----------------------------------------------------------------------------

void ThemeManager::updateActions()
{
	const bool has_theme = m_themes->currentIndex() >= 0;
	const bool can_modify = has_theme && !currentThemeIsDefault();
	m_edit->setEnabled(can_modify);
	m_duplicate->setEnabled(has_theme);
	m_delete->setEnabled(can_modify);
	m_export->setEnabled(can_modify);
}

//-----------------------------------------------------------------------------
