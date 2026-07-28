/*
	SPDX-FileCopyrightText: 2008 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "locale_dialog.h"
#include "cartridge_image.h"
#include "paths.h"
#include "snes_style.h"
#include "theme.h"
#include "ui_layout_audit.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPalette>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>

#include <KDSingleApplication>

#include <cstdio>
#include <cstring>
#include <memory>

int main(int argc, char** argv)
{
	// Version queries are terminal commands, not GUI sessions. Handle them
	// before QApplication so package validation and shell use never depend on
	// a window-system plugin being present or selected.
	bool verify_ui_layout = false;
	for (int i = 1; i < argc; ++i) {
		if ((std::strcmp(argv[i], "--version") == 0) || (std::strcmp(argv[i], "-v") == 0)) {
			std::fputs("FairyWriter " VERSIONSTR "\n", stdout);
			return 0;
		}
		verify_ui_layout |= std::strcmp(argv[i], "--verify-ui-layout") == 0;
	}
	if (FairyWriter::cartridgeImage() == nullptr || FairyWriter::cartridgeImageSize() == 0) {
		std::fputs("Embedded FairyWriter cartridge is missing.\n", stderr);
		return 1;
	}

	QApplication app(argc, argv);
	app.setStyle(new SnesStyle);
	app.setApplicationName("FairyWriter");
	app.setApplicationVersion(VERSIONSTR);
	app.setApplicationDisplayName(Window::tr("FairyWriter"));
	app.setOrganizationDomain("io.github.navjack");
	app.setOrganizationName("navjack");
	std::unique_ptr<QTemporaryDir> audit_directory;
	if (verify_ui_layout) {
		audit_directory = std::make_unique<QTemporaryDir>();
		if (!audit_directory->isValid()) {
			std::fputs("Unable to create the isolated UI audit profile.\n", stderr);
			return 1;
		}
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, audit_directory->filePath("Settings"));
	}

	// Establish a nonblack native clear color before FairyWriter creates or
	// displays a window. PresentationSurface will replace this with the active
	// theme fallback once settings are loaded.
	QPalette application_palette = app.palette();
	application_palette.setColor(QPalette::Window, QColor(189, 230, 238));
	app.setPalette(application_palette);

	QFontDatabase::addApplicationFont(":/fonts/Blasphemous.otf");
	const QStringList font_families = QFontDatabase::families();
	const QString preferred_ui_font = font_families.contains("SMW2: Yoshi's Island")
		? QStringLiteral("SMW2: Yoshi's Island") : QStringLiteral("Blasphemous");
	app.setFont(QFont(preferred_ui_font, 18));
	SnesStyle::setUiMagnification(QSettings().value("Presentation/UiMagnification", 2).toInt());
#ifndef Q_OS_MAC
	app.setWindowIcon(QIcon::fromTheme("fairywriter", QIcon(":/fairywriter.png")));
	app.setDesktopFileName("fairywriter");
#endif

	app.setAttribute(Qt::AA_DontUseNativeMenuBar);

#ifndef Q_OS_MAC
	app.setAttribute(Qt::AA_DontShowIconsInMenus, !QSettings().value("Window/MenuIcons", false).toBool());
#else
	app.setAttribute(Qt::AA_DontShowIconsInMenus, true);
#endif

	// Allow passing Theme as signal parameter
	qRegisterMetaType<Theme>("Theme");

	// Find application data
	const QString appdir = app.applicationDirPath();
	const QString datadir = QDir::cleanPath(appdir + "/" + FAIRYWRITER_DATADIR);

	// Handle portability
	QString userdir;
	if (verify_ui_layout) {
		userdir = audit_directory->filePath("Data");
	}
#ifdef Q_OS_MAC
	const QFileInfo portable(appdir + "/../../../Data");
#else
	const QFileInfo portable(appdir + "/Data");
#endif
	if (!verify_ui_layout && portable.exists() && portable.isWritable()) {
		userdir = portable.absoluteFilePath();
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, userdir + "/Settings");
	}

	// Load application language
	LocaleDialog::loadTranslator("focuswriter_", datadir);

	// Handle commandline
	QCommandLineParser parser;
	parser.setApplicationDescription(Window::tr("A simple fullscreen word processor"));
	parser.addHelpOption();
	parser.addVersionOption();
	QCommandLineOption layout_audit_option(QStringLiteral("verify-ui-layout"),
		QStringLiteral("Verify every panel page at a 1280x800 Steam Deck viewport."));
	parser.addOption(layout_audit_option);
	parser.addPositionalArgument("files", QCoreApplication::translate("main", "Files to open in current session."), "[files]");
	parser.process(app);
	const QStringList files = parser.positionalArguments();

	// Force single instance
	const QString instance_id = verify_ui_layout
		? QStringLiteral("io.github.navjack.FairyWriter.LayoutAudit.%1").arg(QCoreApplication::applicationPid())
		: QStringLiteral("io.github.navjack.FairyWriter");
	KDSingleApplication kdsa(instance_id);
	if (!kdsa.isPrimaryInstance()) {
		const QString list = files.join(QLatin1String("\n"));
		kdsa.sendMessage(list.toUtf8());
		return 0;
	}

	// Load paths
	Paths::load(appdir, userdir, datadir);

	// FairyWriter owns an isolated profile. Offer a one-time, copy-only import
	// from the legacy FocusWriter data directory; the source is never modified.
	QSettings profile_settings;
	if (!verify_ui_layout && !profile_settings.value("Profile/LegacyImportOffered", false).toBool()) {
		profile_settings.setValue("Profile/LegacyImportOffered", true);
		const QString legacy = Paths::legacyFocusWriterPath();
		if (!legacy.isEmpty() && legacy != userdir) {
			const auto answer = QMessageBox::question(nullptr,
				Window::tr("Import FocusWriter profile?"),
				Window::tr("Copy your FocusWriter themes, dictionaries, sessions, recovery cache, and progress into FairyWriter? The original profile will remain unchanged."),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes);
			if (answer == QMessageBox::Yes) {
				Paths::copyLegacyProfile(legacy, userdir);
			}
		}
	}

	// Create theme from old settings
	if (QDir(Theme::path(), "*.theme").entryList(QDir::Files).isEmpty()) {
		QSettings settings;
		Theme theme(QString(), false);

		theme.setBackgroundType(settings.value("Background/Position", theme.backgroundType()).toInt());
		theme.setBackgroundColor(settings.value("Background/Color", theme.backgroundColor()).toString());
		theme.setBackgroundImage(settings.value("Background/Image").toString());
		settings.remove("Background");

		theme.setForegroundColor(settings.value("Page/Color", theme.foregroundColor()).toString());
		theme.setForegroundWidth(settings.value("Page/Width", theme.foregroundWidth().value()).toInt());
		theme.setForegroundOpacity(settings.value("Page/Opacity", theme.foregroundOpacity().value()).toInt());
		settings.remove("Page");

		theme.setTextColor(settings.value("Text/Color", theme.textColor()).toString());
		theme.setTextFont(settings.value("Text/Font", theme.textFont()).value<QFont>());
		settings.remove("Text");

		if (theme.isChanged()) {
			theme.saveChanges();
			settings.setValue("ThemeManager/Theme", theme.name());
		}
	}

	// Create main window
	Window* window = new Window(files);
	QPointer<Window> window_guard(window);
	Window::connect(&kdsa, &KDSingleApplication::messageReceived, window, qOverload<const QByteArray&>(&Window::addDocuments), Qt::QueuedConnection);
	if (verify_ui_layout) {
		QTimer::singleShot(0, window, [window] {
			QString report;
			const bool passed = runUiLayoutAudit(window, report);
			std::fputs(report.toUtf8().constData(), passed ? stdout : stderr);
			QCoreApplication::exit(passed ? 0 : 1);
		});
	}

	const int exit_code = app.exec();
	// QApplication owns process-global style, font, palette, and platform
	// services used by every widget destructor. Explicitly finish any surviving
	// top-level window while those services are still alive; QPointer is already
	// null when the user closed the WA_DeleteOnClose window normally.
	delete window_guard.data();
	return exit_code;
}
