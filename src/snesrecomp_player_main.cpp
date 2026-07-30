#include "snes_machine.h"
#include "cartridge_image.h"
#include "document_bridge.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QClipboard>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QTextStream>
#include <QStandardPaths>

#include <deque>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <utility>

namespace {
struct Scan { std::uint8_t code = 0; bool extended = false; bool shifted = false; };

// Key-path tracing is a development diagnostic, not something a normal run
// should pay for: it used to open, append to and close a hard-coded
// /tmp/fairywriter-keypath.log on *every* key press and release. It is now off
// unless FAIRYWRITER_DEBUG_LOG is set, and writes beside the platform's own
// temporary directory so there is something to point testers at on Windows too.
bool debugLogEnabled()
{
	static const bool enabled = qEnvironmentVariableIsSet("FAIRYWRITER_DEBUG_LOG");
	return enabled;
}

QString debugLogPath()
{
	static const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
		.filePath(QStringLiteral("fairywriter-keypath.log"));
	return path;
}

void debugKeyLog(const QString& line)
{
	if (!debugLogEnabled()) return;
	QFile file(debugLogPath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
	QTextStream stream(&file);
	stream << line << '\n';
}

// The SNES machine failing mid-run used to be a bare qFatal, which aborts with
// nothing a tester could report back. Name the failure, then exit cleanly.
void reportFatal(const QString& detail)
{
	QMessageBox::critical(nullptr, QStringLiteral("FairyWriter"),
		QStringLiteral("FairyWriter has to close.\n\n%1\n\n"
			"Please report this, and re-run with FAIRYWRITER_DEBUG_LOG=1 set if you can "
			"reproduce it — that writes a trace to:\n%2").arg(detail, debugLogPath()));
}

Scan scanForKey(int key)
{
	switch (key) {
	case Qt::Key_A: return {0x1c}; case Qt::Key_B: return {0x32};
	case Qt::Key_C: return {0x21}; case Qt::Key_D: return {0x23};
	case Qt::Key_E: return {0x24}; case Qt::Key_F: return {0x2b};
	case Qt::Key_G: return {0x34}; case Qt::Key_H: return {0x33};
	case Qt::Key_I: return {0x43}; case Qt::Key_J: return {0x3b};
	case Qt::Key_K: return {0x42}; case Qt::Key_L: return {0x4b};
	case Qt::Key_M: return {0x3a}; case Qt::Key_N: return {0x31};
	case Qt::Key_O: return {0x44}; case Qt::Key_P: return {0x4d};
	case Qt::Key_Q: return {0x15}; case Qt::Key_R: return {0x2d};
	case Qt::Key_S: return {0x1b}; case Qt::Key_T: return {0x2c};
	case Qt::Key_U: return {0x3c}; case Qt::Key_V: return {0x2a};
	case Qt::Key_W: return {0x1d}; case Qt::Key_X: return {0x22};
	case Qt::Key_Y: return {0x35}; case Qt::Key_Z: return {0x1a};
	case Qt::Key_1: return {0x16}; case Qt::Key_2: return {0x1e};
	case Qt::Key_3: return {0x26}; case Qt::Key_4: return {0x25};
	case Qt::Key_5: return {0x2e}; case Qt::Key_6: return {0x36};
	case Qt::Key_7: return {0x3d}; case Qt::Key_8: return {0x3e};
	case Qt::Key_9: return {0x46}; case Qt::Key_0: return {0x45};
	case Qt::Key_Space: return {0x29}; case Qt::Key_Tab: return {0x0d};
	case Qt::Key_Return: case Qt::Key_Enter: return {0x5a};
	case Qt::Key_Backspace: return {0x66};
	case Qt::Key_Delete: return {0x71, true}; case Qt::Key_Home: return {0x6c, true};
	case Qt::Key_End: return {0x69, true};
	case Qt::Key_PageUp: return {0x7d, true}; case Qt::Key_PageDown: return {0x7a, true};
	case Qt::Key_Minus: return {0x4e}; case Qt::Key_Period: return {0x49};
	case Qt::Key_Comma: return {0x41}; case Qt::Key_Slash: return {0x4a};
	case Qt::Key_Apostrophe: return {0x52}; case Qt::Key_Semicolon: return {0x4c};
	case Qt::Key_Equal: return {0x55}; case Qt::Key_BracketLeft: return {0x54};
	case Qt::Key_BracketRight: return {0x5b}; case Qt::Key_Backslash: return {0x5d};
	case Qt::Key_QuoteLeft: return {0x0e};
	case Qt::Key_F1: case Qt::Key_Escape: return {0x05};
	// PS/2 set-2 F2. The cartridge maps it to key code 0x1c and opens its own
	// help card; see xbandScanMap in tools/fairywriter-rom/main.go.
	case Qt::Key_F2: return {0x06};
	case Qt::Key_F3: return {0x04};
	case Qt::Key_F4: return {0x0c};
	case Qt::Key_Exclam: return {0x16, false, true}; case Qt::Key_At: return {0x1e, false, true};
	case Qt::Key_NumberSign: return {0x26, false, true}; case Qt::Key_Dollar: return {0x25, false, true};
	case Qt::Key_Percent: return {0x2e, false, true}; case Qt::Key_AsciiCircum: return {0x36, false, true};
	case Qt::Key_Ampersand: return {0x3d, false, true}; case Qt::Key_Asterisk: return {0x3e, false, true};
	case Qt::Key_ParenLeft: return {0x46, false, true}; case Qt::Key_ParenRight: return {0x45, false, true};
	case Qt::Key_Underscore: return {0x4e, false, true}; case Qt::Key_Plus: return {0x55, false, true};
	case Qt::Key_Less: return {0x41, false, true}; case Qt::Key_Greater: return {0x49, false, true};
	case Qt::Key_Question: return {0x4a, false, true}; case Qt::Key_QuoteDbl: return {0x52, false, true};
	case Qt::Key_BraceLeft: return {0x54, false, true}; case Qt::Key_BraceRight: return {0x5b, false, true};
	case Qt::Key_Colon: return {0x4c, false, true}; case Qt::Key_Bar: return {0x5d, false, true};
	case Qt::Key_AsciiTilde: return {0x0e, false, true};
	case Qt::Key_Left: return {0x6b, true}; case Qt::Key_Down: return {0x72, true};
	case Qt::Key_Right: return {0x74, true}; case Qt::Key_Up: return {0x75, true};
	case Qt::Key_Shift: return {0x12};
	default: return {};
	}
}

std::uint8_t shiftedSymbolScan(std::uint8_t code)
{
	switch (code) {
	case 0x16: return 0x70; case 0x1e: return 0x7e; case 0x26: return 0x7d;
	case 0x25: return 0x7c; case 0x2e: return 0x7b; case 0x36: return 0x7a;
	case 0x3d: return 0x79; case 0x3e: return 0x78; case 0x46: return 0x77;
	case 0x45: return 0x76; case 0x4e: return 0x75; case 0x55: return 0x74;
	case 0x41: return 0x73; case 0x49: return 0x72; case 0x4a: return 0x71;
	case 0x52: return 0x6f; case 0x54: return 0x6e; case 0x5b: return 0x6d;
	case 0x4c: return 0x6c; case 0x5d: return 0x6b; case 0x0e: return 0x69;
	default: return 0;
	}
}
}

#ifdef FAIRYWRITER_PERSISTENCE_TESTING
int runPersistenceE2eChild(const QStringList& arguments)
{
	if (arguments.size() != 5) return 90;
	const QString operation = arguments.at(1);
	const QString path = QFileInfo(arguments.at(3)).absoluteFilePath();
	const QString recovery_root = QFileInfo(arguments.at(4)).absoluteFilePath();
	const QString expected = QStringLiteral("FairyWriter process lifecycle ")
		+ arguments.at(2);

	FairyWriter::DocumentBridge bridge(recovery_root);
	bridge.persistence().beginSession();
	FairyWriter::FileCatalog catalog(QFileInfo(path).absolutePath());

	if (operation == QLatin1String("create")) {
		FairyWriter::MailboxRecord insert;
		insert.kind = FairyWriter::DocumentEngine::InsertText;
		insert.revision = bridge.engine().revision();
		const QByteArray text = expected.toUtf8();
		insert.payload.assign(
			reinterpret_cast<const std::uint8_t*>(text.constData()),
			reinterpret_cast<const std::uint8_t*>(text.constData() + text.size()));
		if (!bridge.submit(insert) || !bridge.pump()) return 91;

		FairyWriter::MailboxRecord select;
		select.kind = FairyWriter::DocumentEngine::SelectAll;
		select.revision = bridge.engine().revision();
		if (!bridge.submit(select) || !bridge.pump()) return 92;
		FairyWriter::MailboxRecord bold;
		bold.kind = FairyWriter::DocumentEngine::ToggleBold;
		bold.revision = bridge.engine().revision();
		if (!bridge.submit(bold) || !bridge.pump()) return 93;

		if (!bridge.saveAsNew(catalog, QString(), QFileInfo(path).fileName())) return 94;
		bridge.persistence().markCleanShutdown();
		return QFileInfo(path).size() > 0 ? 0 : 95;
	}

	if (operation == QLatin1String("load")) {
		const QString id = catalog.registerPath(path);
		if (id.isEmpty() || !bridge.openFile(catalog, id)) return 96;
		if (bridge.engine().text() != expected || bridge.engine().isDirty()) return 97;
		if (arguments.at(2) != QLatin1String("crash")) {
			QTextCursor cursor(bridge.engine().document());
			cursor.setPosition(qMin(1, bridge.engine().document()->characterCount() - 1));
			if (cursor.charFormat().fontWeight() != QFont::Bold) return 98;
		}
		bridge.persistence().markCleanShutdown();
		return 0;
	}

	if (operation == QLatin1String("crash")) {
		FairyWriter::PersistenceSettings settings = bridge.persistence().settings();
		settings.mode = FairyWriter::PersistenceSettings::AutosaveMode::RecoveryOnly;
		settings.recovery_copies = 5;
		bridge.persistence().setSettings(settings);
		FairyWriter::MailboxRecord insert;
		insert.kind = FairyWriter::DocumentEngine::InsertText;
		insert.revision = bridge.engine().revision();
		const QByteArray text = expected.toUtf8();
		insert.payload.assign(
			reinterpret_cast<const std::uint8_t*>(text.constData()),
			reinterpret_cast<const std::uint8_t*>(text.constData() + text.size()));
		if (!bridge.submit(insert) || !bridge.pump()) return 99;
		const FairyWriter::PersistenceResult checkpoint =
			bridge.persistence().checkpoint(false);
		if (!checkpoint.succeeded()) return 100;
		QTextStream(stdout) << checkpoint.path << '\n';
		QTextStream(stdout).flush();
		std::_Exit(23);
	}

	if (operation == QLatin1String("restore")) {
		const QVector<FairyWriter::RecoveryRecord> candidates =
			bridge.persistence().recoveryPromptCandidates();
		if (candidates.isEmpty()
			|| !bridge.persistence().recover(candidates.front().path).succeeded()
			|| !bridge.engine().isDirty()
			|| bridge.engine().text() != expected) {
			return 101;
		}
		const FairyWriter::PersistenceResult saved =
			bridge.persistence().saveAs(path, false);
		if (!saved.succeeded()) return 102;
		bridge.persistence().markCleanShutdown();
		return 0;
	}

	return 103;
}
#endif

class RecompPlayer final : public QOpenGLWidget
{
public:
	explicit RecompPlayer(const QByteArray& rom,
		QString catalog_root = QDir::homePath(),
		QString recovery_root = QString())
		: m_machine(fairy_snes_create(reinterpret_cast<const std::uint8_t*>(rom.constData()),
			static_cast<std::size_t>(rom.size())), fairy_snes_destroy)
		, m_bridge(std::move(recovery_root))
		, m_catalog(std::move(catalog_root))
	{
		if (!m_machine) {
			reportFatal(QStringLiteral("The SNES runtime rejected the built-in FairyWriter cartridge."));
			return;
		}
		resetMailboxSram();
		m_bridge.publishPersistenceSettings();
		m_bridge.persistence().beginSession();
		discoverRecovery();
		setWindowTitle(QStringLiteral("FairyWriter"));
		setWindowIcon(QIcon(QStringLiteral(":/icons/fairywriter.png")));
		setMinimumSize(512, 448);
		resize(768, 672);
		// Centre on the primary screen. This used to hunt for a display whose
		// name contained "iPad" or "Sidecar" and move there, which is a useful
		// habit for one developer and simply the wrong monitor for everyone else.
		if (QScreen* screen = QApplication::primaryScreen()) {
			const QRect available = screen->availableGeometry();
			move(available.left() + qMax(0, (available.width() - width()) / 2),
				available.top() + qMax(0, (available.height() - height()) / 2));
		}
		setFocusPolicy(Qt::StrongFocus);
		// The desktop pointer is only the transport for controller-port 1.  The
		// cartridge reads a real SNES Mouse packet and decides what a click means.
		setMouseTracking(true);
		setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
		qApp->installEventFilter(this);
		advanceFrame();
		m_timer.setTimerType(Qt::PreciseTimer);
		m_timer.setInterval(17);
		connect(&m_timer, &QTimer::timeout, this, [this] { advanceFrame(); });
		m_timer.start();
		m_recovery_timer.setSingleShot(true);
		m_recovery_timer.setInterval(
			static_cast<int>(m_bridge.persistence().settings().interval_minutes)
			* 60 * 1000);
		connect(&m_recovery_timer, &QTimer::timeout, this,
			[this] { autosaveRecovery(false); });
		m_observed_document_id = m_bridge.engine().documentId();
		m_observed_content_generation = m_bridge.engine().contentGeneration();
	}

	bool isValid() const { return m_machine != nullptr; }

#ifdef FAIRYWRITER_PERSISTENCE_TESTING
	bool persistenceTestFrames(int count)
	{
		if (!m_machine || count < 0) return false;
		for (int frame = 0; frame < count; ++frame) advanceFrame();
		return m_machine != nullptr;
	}

	bool persistenceTestScan(std::uint8_t code, bool pressed,
		bool extended = false)
	{
		return m_machine
			&& fairy_snes_key_event(m_machine.get(), code, pressed, extended);
	}

	bool persistenceTestMouse(std::int8_t dx, std::int8_t dy, bool left)
	{
		if (!m_machine) return false;
		fairy_snes_mouse_event(m_machine.get(), dx, dy, left, false);
		return true;
	}

	std::uint8_t persistenceTestWram(std::uint32_t address) const
	{
		return m_machine ? fairy_snes_debug_wram(m_machine.get(), address) : 0;
	}

	std::uint8_t persistenceTestBus(std::uint32_t address) const
	{
		return m_machine
			? fairy_snes_debug_bus_read(m_machine.get(), address) : 0;
	}

	FairyWriter::DocumentBridge& persistenceTestBridge() noexcept
	{
		return m_bridge;
	}
#endif

	// Opens a document named on the command line, which is also how a desktop
	// file association or a drag onto the app icon arrives. The catalog owns
	// path-to-opaque-id mapping, so the path is registered there first rather
	// than handed to the engine directly.
	void openDocument(const QString& path)
	{
		const QFileInfo info(path);
		if (!info.exists() || info.isDir()) {
			QMessageBox::warning(nullptr, QStringLiteral("FairyWriter"),
				QStringLiteral("There is no document at:\n%1").arg(path));
			return;
		}
		// Deferred rather than opened here: a viewport published before the
		// cartridge has finished its own boot is decoded and then overwritten by
		// the guest's initialization, so the document silently never appears.
		m_pending_document = info.absoluteFilePath();
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		Q_UNUSED(watched);
		if (!isVisible() || !isActiveWindow()) return QOpenGLWidget::eventFilter(watched, event);
		if (event->type() == QEvent::KeyPress) {
			if (debugLogEnabled()) {
				const auto* keyEvent = static_cast<QKeyEvent*>(event);
				debugKeyLog(QStringLiteral("filter press key=%1 auto=%2 mods=%3")
					.arg(keyEvent->key()).arg(keyEvent->isAutoRepeat() ? 1 : 0)
					.arg(static_cast<int>(keyEvent->modifiers())));
			}
			sendKey(static_cast<QKeyEvent*>(event), true);
			return true;
		}
		if (event->type() == QEvent::KeyRelease) {
			if (debugLogEnabled()) {
				const auto* keyEvent = static_cast<QKeyEvent*>(event);
				debugKeyLog(QStringLiteral("filter release key=%1 auto=%2 mods=%3")
					.arg(keyEvent->key()).arg(keyEvent->isAutoRepeat() ? 1 : 0)
					.arg(static_cast<int>(keyEvent->modifiers())));
			}
			sendKey(static_cast<QKeyEvent*>(event), false);
			return true;
		}
		return QOpenGLWidget::eventFilter(watched, event);
	}
	// A dirty close is a document transition, exactly like New, Open, Recent,
	// session switching, and Recovery. The cartridge owns the shared
	// Checkpoint/Save/Discard/Cancel decision; this event is accepted only after
	// that decision has produced its required durable result.
	void closeEvent(QCloseEvent* event) override
	{
		m_recovery_timer.stop();
		if (m_allow_close || !m_bridge.engine().isDirty()) {
			m_bridge.persistence().markCleanShutdown();
			event->accept();
			return;
		}
		event->ignore();
		if (!m_transition_requested) {
			m_transition_requested = true;
			m_pending_close = true;
			m_bridge.publishTransitionRequired();
		}
		return;
	}
	void showEvent(QShowEvent* event) override
	{
		QOpenGLWidget::showEvent(event);
		activateWindow();
		raise();
		setFocus(Qt::ActiveWindowFocusReason);
	}
	void paintGL() override
	{
		QPainter painter(this);
		painter.fillRect(rect(), Qt::black);
		if (m_frame.isNull()) return;
		const int scale = qMax(1, qMin(width() / 256, height() / 224));
		const QSize size(256 * scale, 224 * scale);
		painter.drawImage(QRect(QPoint((width()-size.width())/2, (height()-size.height())/2), size), m_frame);
	}
	void keyPressEvent(QKeyEvent* event) override { sendKey(event, true); }
	void keyReleaseEvent(QKeyEvent* event) override { sendKey(event, false); }
	void mouseMoveEvent(QMouseEvent* event) override { sendMouse(event); }
	void mousePressEvent(QMouseEvent* event) override { sendMouse(event); setFocus(); }
	void mouseReleaseEvent(QMouseEvent* event) override { sendMouse(event); }

private:
	void discoverRecovery()
	{
		bool had_corrupt = false;
		m_recovery_records =
			m_bridge.persistence().recoveryPromptCandidates(&had_corrupt);
		m_recovery_path = m_recovery_records.isEmpty()
			? QString() : m_recovery_records.front().path;
		if (!m_recovery_records.isEmpty()) {
			m_bridge.publishRecoveryAvailable(
				FairyWriter::documentFormatName(
					m_recovery_records.front().snapshot.format));
		}
		if (had_corrupt) {
			FairyWriter::PersistenceResult warning;
			warning.error = FairyWriter::PersistenceError::CorruptRecovery;
			warning.detail = QStringLiteral(
				"One or more recovery generations were corrupt; the newest valid generation remains available.");
			m_bridge.publishPersistenceFailure(warning);
		}
	}

	void publishRecoveryHistory(std::size_t offset)
	{
		m_recovery_records = m_bridge.persistence().recoveryRecords();
		m_recovery_tokens.clear();
		QVector<FairyWriter::FileEntry> entries;
		entries.reserve(m_recovery_records.size());
		for (const FairyWriter::RecoveryRecord& record : m_recovery_records) {
			const QByteArray digest = QCryptographicHash::hash(
				record.path.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
			const QString token = QString::fromLatin1(digest);
			m_recovery_tokens.insert(token, record.path);

			QString title = record.snapshot.filename.isEmpty()
				? QStringLiteral("UNTITLED")
				: QFileInfo(record.snapshot.filename).completeBaseName();
			QString resident_title;
			resident_title.reserve(8);
			for (const QChar character : title.toUpper()) {
				if (resident_title.size() == 8) break;
				const ushort scalar = character.unicode();
				resident_title += scalar >= 0x20 && scalar <= 0x7e
					? character : QLatin1Char('?');
			}
			if (resident_title.isEmpty()) resident_title = QStringLiteral("UNTITLED");

			QString status;
			if (record.matches_primary) {
				status = QStringLiteral("SAVED");
			} else if (record.transition_resolved) {
				status = QStringLiteral("HISTORY");
			} else {
				status = record.snapshot.filename.isEmpty()
					|| record.primary_fingerprint.sameContent(
						record.current_primary_fingerprint)
					? QStringLiteral("UNSAVED")
					: QStringLiteral("CONFLICT");
			}

			FairyWriter::FileEntry entry;
			entry.id = token;
			entry.name = QStringLiteral("%1 %2 %3")
				.arg(record.created.toLocalTime().toString(
					QStringLiteral("MM-dd HH:mm")),
					resident_title, status).left(29);
			entry.writable = true;
			entry.size = record.snapshot.rich_html.size()
				+ record.snapshot.markdown_source.size();
			entry.modified = record.created;
			entries.push_back(std::move(entry));
		}
		m_bridge.publishRecoveryPage(entries, offset);
	}

	void autosaveRecovery(bool manual)
	{
		if (!m_bridge.engine().isDirty()) return;
		const FairyWriter::PersistenceResult result = manual
			? m_bridge.persistence().checkpoint(true)
			: m_bridge.persistence().timedAutosave();
		if (!result.succeeded()
			&& result.error != FairyWriter::PersistenceError::Disabled) {
			m_bridge.publishPersistenceFailure(result);
		}
		m_recovery_records = m_bridge.persistence().recoveryRecords();
		m_recovery_path = m_recovery_records.isEmpty()
			? QString() : m_recovery_records.front().path;
		m_observed_content_generation = m_bridge.engine().contentGeneration();
	}

	void armAutosave()
	{
		const auto& engine = m_bridge.engine();
		if (engine.documentId() != m_observed_document_id) {
			m_recovery_timer.stop();
			m_observed_document_id = engine.documentId();
			m_observed_content_generation = engine.contentGeneration();
			return;
		}
		if (engine.contentGeneration() == m_observed_content_generation) return;
		m_observed_content_generation = engine.contentGeneration();
		if (m_bridge.persistence().settings().recovery_copies == 0
			|| m_recovery_timer.isActive()) {
			return;
		}
		m_recovery_timer.setInterval(
			static_cast<int>(m_bridge.persistence().settings().interval_minutes)
			* 60 * 1000);
		m_recovery_timer.start();
	}

	static bool replacesDocument(std::uint16_t kind)
	{
		return kind == FairyWriter::DocumentEngine::NewDocument
			|| kind == FairyWriter::DocumentBridge::CommandOpenFile
			|| kind == FairyWriter::DocumentBridge::CommandSwitchSession
			|| kind == FairyWriter::DocumentBridge::CommandRecover;
	}

	void requestTransition(const FairyWriter::MailboxRecord& command)
	{
		m_pending_transition = command;
		m_transition_requested = true;
		m_bridge.publishTransitionRequired();
	}

	void finishTransition()
	{
		m_waiting_transition_save = false;
		m_transition_requested = false;
		if (m_pending_close) {
			m_pending_close = false;
			m_allow_close = true;
			m_bridge.persistence().markCleanShutdown();
			QTimer::singleShot(0, this, [this] { close(); });
			return;
		}
		if (m_pending_transition.has_value()) {
			const FairyWriter::MailboxRecord command = *m_pending_transition;
			m_pending_transition.reset();
			m_transition_bypass = true;
			m_commands.push(command);
		}
	}

	void handleTransitionDecision(const FairyWriter::MailboxRecord& command)
	{
		if (!m_transition_requested || command.payload.size() != 1
			|| command.payload[0] > 3) {
			return;
		}
		const std::uint8_t decision = command.payload[0];
		if (decision == 3) {
			m_pending_transition.reset();
			m_pending_close = false;
			m_waiting_transition_save = false;
			m_transition_requested = false;
			armAutosave();
			return;
		}
		if (decision == 0) {
			const FairyWriter::PersistenceResult checkpoint =
				m_bridge.persistence().checkpoint(true);
			if (!checkpoint.succeeded()) {
				m_bridge.publishPersistenceFailure(checkpoint);
				return;
			}
			finishTransition();
			return;
		}
		if (decision == 1) {
			const FairyWriter::PersistenceResult saved =
				m_bridge.persistence().save();
			if (!saved.succeeded()) {
				if (saved.error == FairyWriter::PersistenceError::NeedsSaveAs) {
					m_waiting_transition_save = true;
				}
				m_bridge.publishPersistenceFailure(saved);
				return;
			}
			finishTransition();
			return;
		}
		// Discard retains prior generations as explicit history, but resolves
		// their startup-prompt state before the document is replaced or closed.
		const FairyWriter::PersistenceResult discarded =
			m_bridge.persistence().discardRecoveryCandidates();
		if (!discarded.succeeded()) {
			m_bridge.publishPersistenceFailure(discarded);
			return;
		}
		finishTransition();
	}

	void continueTransitionAfterSave()
	{
		if (m_waiting_transition_save && !m_bridge.engine().isDirty()) {
			finishTransition();
		}
	}

	struct PendingKey {
		std::uint8_t scancode;
		bool pressed;
		bool extended;
	};

	void sendMouse(QMouseEvent* event)
	{
		if (!m_machine || m_frame.isNull()) return;
		const int scale = qMax(1, qMin(width() / 256, height() / 224));
		const QSize size(256 * scale, 224 * scale);
		const QPoint origin((width() - size.width()) / 2, (height() - size.height()) / 2);
		const QPoint local = event->position().toPoint() - origin;
		const int x = qBound(0, local.x() / scale, 255);
		const int y = qBound(0, local.y() / scale, 223);
		const bool left = event->buttons().testFlag(Qt::LeftButton)
			|| (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton);
		const bool right = event->buttons().testFlag(Qt::RightButton)
			|| (event->type() == QEvent::MouseButtonPress && event->button() == Qt::RightButton);
		// The cartridge pointer begins at (128,112).  Its first host packet must
		// therefore bridge from that known state, not discard the first position
		// and make the initial click hit the stale center row.
		const int dx = m_mouseValid ? qBound(-127, x - m_mouse.x(), 127)
			: qBound(-127, x - 128, 127);
		const int dy = m_mouseValid ? qBound(-127, y - m_mouse.y(), 127)
			: qBound(-127, y - 112, 127);
		m_mouse = QPoint(x, y);
		m_mouseValid = true;
		fairy_snes_mouse_event(m_machine.get(), static_cast<std::int8_t>(dx),
			static_cast<std::int8_t>(dy), left, right);
	}

	// The cartridge derives case from a single shift-state byte that already
	// tracks the physical Shift key, so a letter needs no help from the host to
	// arrive capitalized. Only Caps Lock has no scancode of its own here, and it
	// is the one case worth synthesizing: Shift and Caps Lock cancel on letters
	// the way a typewriter behaves, and Caps Lock never reaches punctuation
	// because shifted symbols were already folded into their own scancodes. The
	// override is undone immediately after the letter's make code so that the
	// shift state still reflects the physical key, and a Shift+arrow typed after
	// a capital keeps extending the selection.
	void queueScan(const Scan& scan, bool pressed, bool letter)
	{
		const bool uppercase = letter && (scan.shifted != m_capsLock);
		const bool override_shift = pressed && letter && m_capsLock;
		if (override_shift) m_input.push_back({0x12, uppercase, false});
		m_input.push_back({scan.code, pressed, scan.extended});
		if (override_shift) m_input.push_back({0x12, scan.shifted, false});
	}

	static bool isLetterKey(int key) { return key >= Qt::Key_A && key <= Qt::Key_Z; }

	void drainInput()
	{
		while (!m_input.empty()) {
			const PendingKey& key = m_input.front();
			if (!fairy_snes_key_event(m_machine.get(), key.scancode, key.pressed, key.extended)) {
				if (debugLogEnabled()) {
					debugKeyLog(QStringLiteral("drain blocked code=%1 pressed=%2 ext=%3 queue-size=%4")
						.arg(key.scancode).arg(key.pressed ? 1 : 0).arg(key.extended ? 1 : 0)
						.arg(static_cast<qulonglong>(m_input.size())));
				}
				return;
			}
			if (debugLogEnabled()) {
				debugKeyLog(QStringLiteral("drain sent code=%1 pressed=%2 ext=%3 queue-size=%4")
					.arg(key.scancode).arg(key.pressed ? 1 : 0).arg(key.extended ? 1 : 0)
					.arg(static_cast<qulonglong>(m_input.size())));
			}
			m_input.pop_front();
		}
	}

	// A frame that does not complete is unrecoverable, but it must not take the
	// user's unsaved work with it: flush recovery, tell them what happened, and
	// stop the clock instead of aborting the process.
	bool runFrame()
	{
		if (fairy_snes_run_frame(m_machine.get())) return true;
		m_timer.stop();
		m_recovery_timer.stop();
		autosaveRecovery(true);
		reportFatal(QStringLiteral("The SNES runtime stopped unexpectedly.\n"
			"Any unsaved work has been written to a recovery file and will be "
			"offered the next time FairyWriter starts."));
		QApplication::exit(1);
		return false;
	}

	void advanceFrame()
	{
		drainInput();
		if (m_bootstrapViewportCommitPending) {
			if (!runFrame()) return;
			// Bootstrap handshake: publish the first mailbox/viewport commit only
			// after the guest has executed at least one frame, so the commit byte
			// cannot be mistaken for an already-initialized steady state.
			pumpMailbox();
			m_bootstrapViewportCommitPending = false;
		} else {
			pumpMailbox();
			if (!runFrame()) return;
		}
		const auto* words = fairy_snes_framebuffer(m_machine.get());
		if (m_frame.size() != QSize(256, 224) || m_frame.format() != QImage::Format_ARGB32) {
			m_frame = QImage(256, 224, QImage::Format_ARGB32);
		}
		for (int y = 0; y < 224; ++y) {
			auto* dst = reinterpret_cast<QRgb*>(m_frame.scanLine(y));
			for (int x = 0; x < 256; ++x) {
				const std::uint32_t source = words ? words[y * 256 + x] : 0;
				dst[x] = static_cast<QRgb>(0xff000000u | source);
			}
		}
		++m_frameCounter;
		if (!m_pending_document.isEmpty() && (m_frameCounter >= PendingDocumentFrame)) {
			const QString path = m_pending_document;
			m_pending_document.clear();
			const QString id = m_catalog.registerPath(path);
			if (id.isEmpty() || !m_bridge.openFile(m_catalog, id)) {
				QMessageBox::warning(this, QStringLiteral("FairyWriter"),
					QStringLiteral("FairyWriter could not open:\n%1").arg(path));
			}
		}
		armAutosave();
		if (debugLogEnabled() && ((m_frameCounter % 120) == 0)) {
			debugKeyLog(QStringLiteral("frame sample p0=%1 p1=%2 p2=%3 cursor=%4 len=%5 c0=%6 mode=%7 key=%8 cmdP=%9 cmdC=%10")
				.arg(words ? words[0] : 0).arg(words ? words[1] : 0).arg(words ? words[2] : 0)
				.arg(fairy_snes_debug_wram(m_machine.get(), 0x00))
				.arg(fairy_snes_debug_wram(m_machine.get(), 0x08))
				.arg(fairy_snes_debug_wram(m_machine.get(), 0x0500))
				.arg(fairy_snes_debug_wram(m_machine.get(), 0x031d))
				.arg(fairy_snes_debug_wram(m_machine.get(), 0x000f))
				.arg(sram16(2))
				.arg(sram16(4)));
		}
		update();
	}

	static constexpr std::uint32_t Sram = 0x700000;
	std::uint8_t sramByte(std::size_t offset) const { return fairy_snes_debug_bus_read(m_machine.get(), Sram + static_cast<std::uint32_t>(offset)); }
	void setSramByte(std::size_t offset, std::uint8_t value) { fairy_snes_debug_bus_write(m_machine.get(), Sram + static_cast<std::uint32_t>(offset), value); }
	std::uint16_t sram16(std::size_t offset) const { return sramByte(offset) | (std::uint16_t(sramByte(offset + 1)) << 8); }
	void setSram16(std::size_t offset, std::uint16_t value) { setSramByte(offset, value); setSramByte(offset + 1, value >> 8); }
	void resetMailboxSram()
	{
		// The SNES cartridge mailbox is a volatile host/guest transport. Clearing
		// its full 32 KiB region at process start prevents stale persisted SRAM
		// ring indices/events from prior runs from forcing incorrect startup modes.
		for (std::size_t i = 0; i < FairyWriter::MailboxLayout::TotalBytes; ++i) {
			setSramByte(i, 0);
		}
	}

	void pumpMailbox()
	{
		// The host owns event bytes and the producer index; the cartridge owns
		// only the consumer index. Retire acknowledged complete records before
		// processing commands that may publish more events. Importing SRAM event
		// bytes here would replay old records and could overwrite events produced
		// during this frame.
		const std::size_t eventConsumer = sram16(8);
		if (eventConsumer != m_eventRead) {
			if (m_bridge.events().consumeTo(eventConsumer)) {
				m_eventRead = eventConsumer;
			} else {
				std::array<std::uint8_t, FairyWriter::MailboxLayout::EventBytes> currentEvents{};
				std::size_t currentRead = 0;
				std::size_t currentWrite = 0;
				m_bridge.events().exportRaw(currentEvents.data(), currentEvents.size(),
					currentRead, currentWrite);
				m_eventRead = currentRead;
				m_eventWrite = currentWrite;
				setSram16(8, static_cast<std::uint16_t>(currentRead));
			}
		}

		std::array<std::uint8_t, FairyWriter::MailboxLayout::CommandBytes> commandBytes{};
		for (std::size_t i = 0; i < commandBytes.size(); ++i) commandBytes[i] = sramByte(FairyWriter::MailboxLayout::CommandOffset + i);
		const std::size_t producer = sram16(2);
		if (producer != m_commandRead) {
			if (!m_commands.importRaw(commandBytes.data(), m_commandRead, producer)) {
				// SRAM is external state. A torn or invalid index must not bring down
				// the editor or reach DocumentEngine; drop the malformed ring.
				m_commands.clear();
				m_commandRead = 0;
				setSram16(4, 0);
			}
			FairyWriter::MailboxRecord command;
			while (m_commands.pop(command)) {
				if (command.kind
					== FairyWriter::DocumentBridge::CommandTransitionDecision) {
					handleTransitionDecision(command);
					continue;
				}
				if (replacesDocument(command.kind)
					&& m_bridge.engine().isDirty()
					&& !m_transition_bypass) {
					if (!m_transition_requested) requestTransition(command);
					continue;
				}
				if (m_transition_bypass) m_transition_bypass = false;
				if (command.kind == FairyWriter::DocumentBridge::CommandListFiles || command.kind == FairyWriter::DocumentBridge::CommandOpenFile || command.kind == FairyWriter::DocumentBridge::CommandSaveAs || command.kind == FairyWriter::DocumentBridge::CommandSaveAsNew || command.kind == FairyWriter::DocumentBridge::CommandCreateDirectory || command.kind == FairyWriter::DocumentBridge::CommandStatistics || command.kind == FairyWriter::DocumentBridge::CommandRecentFiles || command.kind == FairyWriter::DocumentBridge::CommandListRoots || command.kind == FairyWriter::DocumentBridge::CommandListRecovery || command.kind == FairyWriter::DocumentBridge::CommandRecover || command.kind == FairyWriter::DocumentBridge::CommandListSessions || command.kind == FairyWriter::DocumentBridge::CommandSwitchSession) {
					const QByteArray payload(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size()));
					const QString id = QString::fromUtf8(payload);
					if (command.revision == m_bridge.engine().revision()) {
						if (command.kind == FairyWriter::DocumentBridge::CommandListSessions) m_bridge.listSessions();
						else if (command.kind == FairyWriter::DocumentBridge::CommandSwitchSession) m_bridge.switchSession(m_catalog, id);
						else if (command.kind == FairyWriter::DocumentBridge::CommandListFiles) {
							std::optional<FairyWriter::DocumentFormat> format;
							switch ((command.flags >> 1) & 0x7) {
							case 1: format = FairyWriter::DocumentFormat::Odt; break;
							case 2: format = FairyWriter::DocumentFormat::Docx; break;
							case 3: format = FairyWriter::DocumentFormat::Rtf; break;
							case 4: format = FairyWriter::DocumentFormat::Markdown; break;
							default: break;
							}
							m_bridge.listFiles(m_catalog, id, (command.flags & 1) != 0,
								((command.flags >> 8) & 0xff)
									* FairyWriter::DocumentBridge::FilePageSize,
								format);
						}
						else if (command.kind == FairyWriter::DocumentBridge::CommandOpenFile) m_bridge.openFile(m_catalog, id);
						else if (command.kind == FairyWriter::DocumentBridge::CommandSaveAs) m_bridge.saveAs(m_catalog, id, (command.flags & 1) != 0);
						else if (command.kind == FairyWriter::DocumentBridge::CommandSaveAsNew) {
							const int separator = payload.indexOf('\0');
							if (separator >= 0) {
								const bool saved = m_bridge.saveAsNew(m_catalog,
									QString::fromUtf8(payload.left(separator)),
									QString::fromUtf8(payload.mid(separator + 1)));
#ifdef FAIRYWRITER_PERSISTENCE_TESTING
								if (!saved) {
									qWarning("Save As New rejected parent '%s' name '%s'",
										payload.left(separator).constData(),
										payload.mid(separator + 1).constData());
								}
#endif
							}
						}
						else if (command.kind == FairyWriter::DocumentBridge::CommandStatistics) m_bridge.publishStatistics();
						else if (command.kind == FairyWriter::DocumentBridge::CommandRecentFiles) m_bridge.listRecentFiles(m_catalog, ((command.flags >> 8) & 0xff) * FairyWriter::DocumentBridge::FilePageSize);
						else if (command.kind == FairyWriter::DocumentBridge::CommandListRoots) m_bridge.listRoots(m_catalog, ((command.flags >> 8) & 0xff) * FairyWriter::DocumentBridge::FilePageSize);
						else if (command.kind == FairyWriter::DocumentBridge::CommandListRecovery) {
							publishRecoveryHistory(
								((command.flags >> 8) & 0xff)
									* FairyWriter::DocumentBridge::FilePageSize);
						} else if (command.kind == FairyWriter::DocumentBridge::CommandRecover) {
							const QByteArray token(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size()));
							const QString selected = QString::fromUtf8(token);
							if (token == QByteArrayLiteral("current")
								&& !m_recovery_path.isEmpty()) {
								m_bridge.recover(m_recovery_path);
							} else if (m_recovery_tokens.contains(selected)) {
								m_bridge.recover(m_recovery_tokens.value(selected));
							}
						}
						else {
							const int separator = payload.indexOf('\0');
							if (separator >= 0) m_bridge.createDirectory(m_catalog, QString::fromUtf8(payload.left(separator)), QString::fromUtf8(payload.mid(separator + 1)));
						}
						if (command.kind == FairyWriter::DocumentBridge::CommandSaveAs
							|| command.kind == FairyWriter::DocumentBridge::CommandSaveAsNew) {
							continueTransitionAfterSave();
						}
					}
#ifdef FAIRYWRITER_PERSISTENCE_TESTING
					else if (command.kind
						== FairyWriter::DocumentBridge::CommandSaveAsNew) {
						qWarning("Save As New revision mismatch guest=%llu host=%llu",
							static_cast<unsigned long long>(command.revision),
							static_cast<unsigned long long>(
								m_bridge.engine().revision()));
					}
#endif
				} else {
					m_bridge.submit(command);
					m_bridge.pump();
				}
			}
			std::size_t commandRead = 0, commandWrite = 0;
			m_commands.exportRaw(commandBytes.data(), commandBytes.size(), commandRead, commandWrite);
			m_commandRead = commandRead;
			setSram16(4, static_cast<std::uint16_t>(m_commandRead));
		}

		std::array<std::uint8_t, FairyWriter::MailboxLayout::EventBytes> eventBytes{};
		std::size_t read = 0, write = 0;
		m_bridge.events().exportRaw(eventBytes.data(), eventBytes.size(), read, write);
		for (std::size_t i = 0; i < eventBytes.size(); ++i) setSramByte(FairyWriter::MailboxLayout::EventOffset + i, eventBytes[i]);
		m_eventRead = read; m_eventWrite = write;
		setSram16(6, static_cast<std::uint16_t>(m_eventWrite));
		const auto& slot = m_bridge.viewports().active();
		for (std::size_t slotIndex = 0; slotIndex < 2; ++slotIndex) {
			const std::size_t slotOffset = FairyWriter::MailboxLayout::ViewportOffset + FairyWriter::MailboxLayout::ViewportSlotBytes * slotIndex;
			for (std::size_t i = 0; i < slot.size(); ++i) setSramByte(slotOffset + i, slot[i]);
		}
		setSramByte(0x0a, m_bridge.viewports().activeIndex());
	}

	void sendKey(QKeyEvent* event, bool pressed)
	{
		// Qt owns the desktop key-repeat clock, but the repeated make-code still
		// enters the same XBAND queue as a physical key. Shortcuts remain
		// edge-triggered below, so holding Ctrl-S or Ctrl-Z cannot replay a
		// command while ordinary text/navigation keys repeat naturally.
		if (event->isAutoRepeat()) {
			Scan scan = scanForKey(event->key());
			if (event->key() != Qt::Key_Shift && (event->modifiers() & Qt::ShiftModifier)) scan.shifted = true;
			if (scan.shifted) {
				if (const std::uint8_t symbol = shiftedSymbolScan(scan.code)) { scan.code = symbol; scan.shifted = false; }
			}
			if (pressed && scan.code) queueScan(scan, true, isLetterKey(event->key()));
			drainInput();
			event->accept();
			return;
		}
		if (!event->isAutoRepeat()) {
			const Qt::KeyboardModifiers shortcutModifiers = Qt::ControlModifier | Qt::MetaModifier;
			if (pressed && (event->modifiers() & shortcutModifiers) && (event->key() == Qt::Key_A || event->key() == Qt::Key_B || event->key() == Qt::Key_C || event->key() == Qt::Key_F || event->key() == Qt::Key_I || event->key() == Qt::Key_N || event->key() == Qt::Key_U || event->key() == Qt::Key_X || event->key() == Qt::Key_V || event->key() == Qt::Key_S || event->key() == Qt::Key_Z || event->key() == Qt::Key_Y || event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight || ((event->modifiers() & Qt::ShiftModifier) && (event->key() == Qt::Key_E || event->key() == Qt::Key_L || event->key() == Qt::Key_R)))) {
				if (event->key() == Qt::Key_A) {
					FairyWriter::MailboxRecord command;
					command.kind = FairyWriter::DocumentEngine::SelectAll;
					command.revision = m_bridge.engine().revision();
					m_bridge.submit(command);
					m_bridge.pump();
				} else if ((event->modifiers() & Qt::ShiftModifier) && (event->key() == Qt::Key_E || event->key() == Qt::Key_L || event->key() == Qt::Key_R)) {
					FairyWriter::MailboxRecord command;
					command.kind = event->key() == Qt::Key_L ? FairyWriter::DocumentEngine::AlignLeft : event->key() == Qt::Key_R ? FairyWriter::DocumentEngine::AlignRight : FairyWriter::DocumentEngine::AlignCenter;
					command.revision = m_bridge.engine().revision();
					m_bridge.submit(command);
					m_bridge.pump();
				} else if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight) {
					FairyWriter::MailboxRecord command;
					command.kind = event->key() == Qt::Key_BracketLeft ? FairyWriter::DocumentEngine::IndentDecrease : FairyWriter::DocumentEngine::IndentIncrease;
					command.revision = m_bridge.engine().revision();
					m_bridge.submit(command);
					m_bridge.pump();
				} else if (event->key() == Qt::Key_F) {
					FairyWriter::MailboxRecord command;
					command.kind = FairyWriter::DocumentEngine::FindNext;
					command.revision = m_bridge.engine().revision();
					const QByteArray query = QApplication::clipboard()->text().toUtf8();
					command.payload.assign(reinterpret_cast<const std::uint8_t*>(query.constData()), reinterpret_cast<const std::uint8_t*>(query.constData() + query.size()));
					m_bridge.submit(command);
					m_bridge.pump();
				} else if (event->key() == Qt::Key_B || event->key() == Qt::Key_I || event->key() == Qt::Key_N || event->key() == Qt::Key_U) {
					FairyWriter::MailboxRecord command;
					command.kind = event->key() == Qt::Key_B ? FairyWriter::DocumentEngine::ToggleBold :
						event->key() == Qt::Key_I ? FairyWriter::DocumentEngine::ToggleItalic :
						event->key() == Qt::Key_U ? FairyWriter::DocumentEngine::ToggleUnderline : FairyWriter::DocumentEngine::NewDocument;
					command.revision = m_bridge.engine().revision();
					m_bridge.submit(command);
					m_bridge.pump();
				} else if (event->key() == Qt::Key_C || event->key() == Qt::Key_X) {
					QApplication::clipboard()->setText(m_bridge.engine().selectedText());
					if (event->key() == Qt::Key_X) {
						FairyWriter::MailboxRecord command;
						command.kind = FairyWriter::DocumentEngine::DeleteSelection;
						command.revision = m_bridge.engine().revision();
						m_bridge.submit(command);
						m_bridge.pump();
					}
				} else if (event->key() == Qt::Key_V) {
					FairyWriter::MailboxRecord command;
					command.kind = FairyWriter::DocumentEngine::PasteText;
					command.revision = m_bridge.engine().revision();
					const QByteArray text = QApplication::clipboard()->text().toUtf8();
					command.payload.assign(reinterpret_cast<const std::uint8_t*>(text.constData()), reinterpret_cast<const std::uint8_t*>(text.constData() + text.size()));
					m_bridge.submit(command);
					m_bridge.pump();
				} else if (event->key() == Qt::Key_Z || event->key() == Qt::Key_Y) {
					FairyWriter::MailboxRecord command;
					command.kind = event->key() == Qt::Key_Z ? FairyWriter::DocumentEngine::Undo : FairyWriter::DocumentEngine::Redo;
					command.revision = m_bridge.engine().revision();
					m_bridge.submit(command);
					m_bridge.pump();
				} else {
					FairyWriter::MailboxRecord command;
					command.kind = FairyWriter::DocumentEngine::Save;
					command.revision = m_bridge.engine().revision();
					m_bridge.submit(command);
					m_bridge.pump();
				}
				event->accept();
				return;
			}
			if (event->key() == Qt::Key_CapsLock) {
				if (pressed) m_capsLock = !m_capsLock;
				event->accept();
				return;
			}
			Scan scan = scanForKey(event->key());
			if (event->key() != Qt::Key_Shift && (event->modifiers() & Qt::ShiftModifier)) scan.shifted = true;
			if (scan.shifted) {
				if (const std::uint8_t symbol = shiftedSymbolScan(scan.code)) { scan.code = symbol; scan.shifted = false; }
			}
			if (scan.code) {
				if (debugLogEnabled()) {
					debugKeyLog(QStringLiteral("sendKey key=%1 pressed=%2 code=%3 ext=%4 shifted=%5 caps=%6")
						.arg(event->key()).arg(pressed ? 1 : 0).arg(scan.code).arg(scan.extended ? 1 : 0)
						.arg(scan.shifted ? 1 : 0).arg(m_capsLock ? 1 : 0));
				}
				queueScan(scan, pressed, isLetterKey(event->key()));
				if (debugLogEnabled()) {
					debugKeyLog(QStringLiteral("queue-size=%1").arg(static_cast<qulonglong>(m_input.size())));
				}
				drainInput();
			}
		}
		event->accept();
	}
	std::unique_ptr<FairySnesMachine, decltype(&fairy_snes_destroy)> m_machine{nullptr, fairy_snes_destroy};
	std::deque<PendingKey> m_input;
	QImage m_frame;
	QPoint m_mouse;
	bool m_mouseValid = false;
	QTimer m_timer;
	QTimer m_recovery_timer;
	FairyWriter::DocumentBridge m_bridge;
	FairyWriter::FileCatalog m_catalog;
	FairyWriter::MailboxRing m_commands{FairyWriter::MailboxLayout::CommandBytes};
	std::size_t m_commandRead = 0;
	std::size_t m_eventRead = 0;
	std::size_t m_eventWrite = 0;
	bool m_capsLock = false;
	bool m_allow_close = false;
	bool m_pending_close = false;
	bool m_transition_requested = false;
	bool m_transition_bypass = false;
	bool m_waiting_transition_save = false;
	std::optional<FairyWriter::MailboxRecord> m_pending_transition;
	bool m_bootstrapViewportCommitPending = true;
	QString m_recovery_path;
	QVector<FairyWriter::RecoveryRecord> m_recovery_records;
	QHash<QString, QString> m_recovery_tokens;
	QUuid m_observed_document_id;
	std::uint64_t m_observed_content_generation = 0;
	// A document named on the command line, held until the cartridge has booted
	// far enough to keep a committed viewport. See openDocument().
	QString m_pending_document;
	static constexpr std::uint64_t PendingDocumentFrame = 30;
	std::uint64_t m_frameCounter = 0;
};

#ifdef FAIRYWRITER_PERSISTENCE_TESTING
namespace {

bool persistenceTap(RecompPlayer& player, std::uint8_t code,
	bool extended = false)
{
	return player.persistenceTestScan(code, true, extended)
		&& player.persistenceTestFrames(5)
		&& player.persistenceTestScan(code, false, extended)
		&& player.persistenceTestFrames(2);
}

bool persistenceType(RecompPlayer& player, const QString& text)
{
	for (const QChar character : text) {
		int key = 0;
		if (character >= QLatin1Char('a') && character <= QLatin1Char('z')) {
			key = Qt::Key_A + character.unicode() - QLatin1Char('a').unicode();
		} else if (character == QLatin1Char(' ')) {
			key = Qt::Key_Space;
		} else {
			return false;
		}
		const Scan scan = scanForKey(key);
		if (!scan.code || !persistenceTap(player, scan.code, scan.extended)) {
			return false;
		}
	}
	return true;
}

bool persistenceSelectAllAndBold(RecompPlayer& player)
{
	// Select the single cartridge line with the physical Shift+Home sequence,
	// then click the cartridge's [B] toolbar button. Both commands cross the
	// XBAND/mouse -> guest mailbox path used by an ordinary session.
	if (!player.persistenceTestScan(0x12, true)
		|| !player.persistenceTestFrames(2)
		|| !persistenceTap(player, 0x6c, true)
		|| !player.persistenceTestScan(0x12, false)
		|| !player.persistenceTestFrames(2)
		|| !player.persistenceTestMouse(51, -101, false)
		|| !player.persistenceTestFrames(4)
		|| !player.persistenceTestMouse(0, 0, true)
		|| !player.persistenceTestFrames(8)
		|| !player.persistenceTestMouse(0, 0, false)
		|| !player.persistenceTestFrames(3)) {
		return false;
	}
	return true;
}

bool persistenceOpenSaveMenu(RecompPlayer& player)
{
	if (!persistenceTap(player, 0x05)
		|| player.persistenceTestWram(0x031d) != 1) {
		qWarning("F1 save menu failed mode=%u key=%u",
			player.persistenceTestWram(0x031d),
			player.persistenceTestWram(0x000f));
		return false;
	}
	int selected = player.persistenceTestWram(0x031e);
	while (selected > 2) {
		if (!persistenceTap(player, 0x75, true)) return false;
		--selected;
	}
	while (selected < 2) {
		if (!persistenceTap(player, 0x72, true)) return false;
		++selected;
	}
	return persistenceTap(player, 0x5a);
}

bool persistenceSaveAsNew(RecompPlayer& player, int format_row,
	const QString& base_name)
{
	if (!persistenceOpenSaveMenu(player)) return false;
	if (player.persistenceTestWram(0x031d) != 0x11) {
		qWarning("first Save did not reach format picker mode=%u",
			player.persistenceTestWram(0x031d));
		return false;
	}
	for (int row = 0; row < format_row; ++row) {
		if (!persistenceTap(player, 0x72, true)) return false;
	}
	if (!persistenceTap(player, 0x5a)
		|| player.persistenceTestWram(0x031d) != 6) {
		qWarning("format selection failed mode=%u",
			player.persistenceTestWram(0x031d));
		return false;
	}
	if (!persistenceTap(player, 0x5a)
		|| player.persistenceTestWram(0x031d) != 6) {
		qWarning("save root selection failed mode=%u",
			player.persistenceTestWram(0x031d));
		return false;
	}
	if (!persistenceTap(player, 0x31)
		|| player.persistenceTestWram(0x031d) != 0x0a) {
		qWarning("new filename mode failed mode=%u",
			player.persistenceTestWram(0x031d));
		return false;
	}
	if (!persistenceType(player, base_name)
		|| !persistenceTap(player, 0x5a)
		|| !player.persistenceTestFrames(5)) return false;
	return true;
}

bool persistenceSaveNamed(RecompPlayer& player)
{
	return persistenceOpenSaveMenu(player)
		&& player.persistenceTestFrames(5)
		&& !player.persistenceTestBridge().engine().isDirty();
}

} // namespace

int runPersistenceCartridgeE2eChild(const QStringList& arguments)
{
	if (arguments.size() != 5) return 110;
	const QString operation = arguments.at(1);
	const QString format = arguments.at(2);
	const QFileInfo requestedPath(arguments.at(3));
	const QString canonicalParent = requestedPath.dir().canonicalPath();
	const QString path = QDir(canonicalParent.isEmpty()
			? requestedPath.absolutePath()
			: canonicalParent)
		.filePath(requestedPath.fileName());
	const QString recovery_root = QFileInfo(arguments.at(4)).absoluteFilePath();
	const QString catalog_root = QFileInfo(path).absolutePath();
	const QByteArray rom(
		reinterpret_cast<const char*>(FairyWriter::cartridgeImage()),
		static_cast<qsizetype>(FairyWriter::cartridgeImageSize()));

	int format_row = -1;
	if (format == QLatin1String("odt") || format == QLatin1String("crash")) {
		format_row = 0;
	} else if (format == QLatin1String("docx")) {
		format_row = 1;
	} else if (format == QLatin1String("rtf")) {
		format_row = 2;
	} else if (format == QLatin1String("md")) {
		format_row = 3;
	}
	if (format_row < 0) return 111;

	const QString expected = format == QLatin1String("crash")
		? QStringLiteral("fairywriter cartridge recovery")
		: QStringLiteral("fairywriter cartridge lifecycle ") + format;

	if (operation == QLatin1String("create")) {
		RecompPlayer player(rom, catalog_root, recovery_root);
		if (!player.isValid()) return 112;
		if (!player.persistenceTestFrames(6)) return 122;
		if (!persistenceType(player, expected)) return 123;
		if (!persistenceSelectAllAndBold(player)) return 124;
		if (!persistenceSaveAsNew(player, format_row,
				QFileInfo(path).completeBaseName())) {
			qWarning("cartridge save-as failed in mode %u, menu row %u",
				player.persistenceTestWram(0x031d),
				player.persistenceTestWram(0x031e));
			qWarning("command producer=%u consumer=%u last kind=%02x%02x count=%u",
				player.persistenceTestBus(0x700002)
					| (player.persistenceTestBus(0x700003) << 8),
				player.persistenceTestBus(0x700004)
					| (player.persistenceTestBus(0x700005) << 8),
				player.persistenceTestBus(0x700103),
				player.persistenceTestBus(0x700102),
				player.persistenceTestBus(0x700104)
					| (player.persistenceTestBus(0x700105) << 8));
				qWarning("event producer=%u consumer=%u count=%u/%u base=%u/%u",
					player.persistenceTestBus(0x700006)
						| (player.persistenceTestBus(0x700007) << 8),
					player.persistenceTestBus(0x700008)
						| (player.persistenceTestBus(0x700009) << 8),
					player.persistenceTestWram(0x0323),
					player.persistenceTestWram(0x0324),
					player.persistenceTestWram(0x032a),
					player.persistenceTestWram(0x032b));
			qWarning("engine filename '%s' dirty=%d text=%lld",
				qPrintable(player.persistenceTestBridge().engine().filename()),
				player.persistenceTestBridge().engine().isDirty() ? 1 : 0,
				static_cast<long long>(
					player.persistenceTestBridge().engine().text().size()));
			QByteArray tail;
			for (std::uint32_t at = 700; at < 795; ++at) {
				tail.push_back(static_cast<char>(
					player.persistenceTestBus(0x700100 + at)));
				}
				qWarning("command tail %s", tail.toHex().constData());
				QByteArray eventHead;
				for (std::uint32_t at = 0; at < 80; ++at) {
					eventHead.push_back(static_cast<char>(
						player.persistenceTestBus(0x702100 + at)));
				}
				qWarning("event head %s", eventHead.toHex().constData());
				return 125;
		}
		if (player.persistenceTestBridge().engine().isDirty()) return 126;
		if (QFileInfo(path).size() <= 0) return 127;

		if (format == QLatin1String("md")) {
			// Exercise the cartridge-owned Rendered/Source setting, edit exact
			// UTF-8 source, then save the named primary through the menu.
			if (!persistenceTap(player, 0x04)) return 113;
			for (int row = 0; row < 4; ++row) {
				if (!persistenceTap(player, 0x72, true)) return 113;
			}
			if (!persistenceTap(player, 0x74, true)
				|| !player.persistenceTestBridge().engine().markdownSourceMode()
				|| !persistenceTap(player, 0x04)
				|| !persistenceTap(player, 0x69, true)
				|| !persistenceTap(player, 0x5a)
				|| !persistenceType(player, QStringLiteral("source"))
				|| !persistenceSaveNamed(player)
				|| !player.persistenceTestBridge().engine().markdownSource()
					.contains(QByteArrayLiteral("source"))) {
				return 113;
			}
		}
		player.persistenceTestBridge().persistence().markCleanShutdown();
		return 0;
	}

	if (operation == QLatin1String("load")) {
		RecompPlayer player(rom, catalog_root, recovery_root);
		if (!player.isValid()) return 114;
		player.openDocument(path);
		if (!player.persistenceTestFrames(40)
			|| player.persistenceTestBridge().engine().filename() != path
			|| player.persistenceTestBridge().engine().isDirty()
			|| !player.persistenceTestBridge().engine().text().contains(expected)) {
			qWarning("cartridge load failed file='%s' expected='%s' actual='%s' dirty=%d",
				qPrintable(player.persistenceTestBridge().engine().filename()),
				qPrintable(expected),
				qPrintable(player.persistenceTestBridge().engine().text()),
				player.persistenceTestBridge().engine().isDirty() ? 1 : 0);
			return 115;
		}
		const bool rich_format = format == QLatin1String("odt")
			|| format == QLatin1String("docx")
			|| format == QLatin1String("rtf");
		if (rich_format) {
			QTextCursor cursor(
				player.persistenceTestBridge().engine().document());
			cursor.setPosition(qMin(1,
				player.persistenceTestBridge().engine().document()
					->characterCount() - 1));
			if (cursor.charFormat().fontWeight() != QFont::Bold) {
				qWarning("cartridge rich-format load lost bold metadata for '%s'",
					qPrintable(format));
				return 116;
			}
		}
		if (format == QLatin1String("md")
			&& !player.persistenceTestBridge().engine().text()
				.contains(QStringLiteral("source"))) {
			return 117;
		}
		player.persistenceTestBridge().persistence().markCleanShutdown();
		return 0;
	}

	// The user's own way back into a saved document: F1, Open, the browser, the
	// row the file actually occupies, Enter. "load" above calls openDocument()
	// like a desktop file association does, which never touches the cartridge
	// browser, so it cannot prove that browsing to a document opens it.
	if (operation == QLatin1String("browse")) {
		RecompPlayer player(rom, catalog_root, recovery_root);
		if (!player.isValid() || !player.persistenceTestFrames(6)) return 128;
		if (!persistenceTap(player, 0x05)
			|| player.persistenceTestWram(0x031d) != 1) {
			qWarning("F1 did not open the menu before Open: mode=%u",
				player.persistenceTestWram(0x031d));
			return 129;
		}
		if (!persistenceTap(player, 0x72, true)
			|| player.persistenceTestWram(0x031e) != 1
			|| !persistenceTap(player, 0x5a)
			|| !player.persistenceTestFrames(8)
			|| player.persistenceTestWram(0x031d) != 5) {
			qWarning("Open did not reach a ready root browser: mode=%u row=%u",
				player.persistenceTestWram(0x031d),
				player.persistenceTestWram(0x031e));
			return 130;
		}
		if (!persistenceTap(player, 0x5a) || !player.persistenceTestFrames(8)
			|| player.persistenceTestWram(0x031d) != 5) {
			qWarning("entering the catalog root did not list its files: mode=%u",
				player.persistenceTestWram(0x031d));
			return 131;
		}
		// Find the saved document among the rows the host actually published.
		// Folders sort ahead of documents, so this is never the first row.
		const QString wanted = QFileInfo(path).fileName();
		const int rows = player.persistenceTestWram(0x031f);
		int target = -1;
		for (int row = 0; row < rows && target < 0; ++row) {
			QByteArray name;
			const int length = player.persistenceTestWram(
				0x17f0 + static_cast<std::uint32_t>(row));
			for (int at = 0; at < length; ++at) {
				name.push_back(static_cast<char>(player.persistenceTestWram(
					0x1700 + static_cast<std::uint32_t>(row * 30 + at))));
			}
			if (QString::fromUtf8(name) == wanted) target = row;
		}
		if (target < 1) {
			qWarning("'%s' was not a listed row below the first: rows=%u row=%d",
				qPrintable(wanted), rows, target);
			return 132;
		}
		for (int row = 0; row < target; ++row) {
			if (!persistenceTap(player, 0x72, true)) return 133;
		}
		if (player.persistenceTestWram(0x0320) != target) {
			qWarning("the browser selection did not reach row %d: row=%u",
				target, player.persistenceTestWram(0x0320));
			return 134;
		}
		if (!persistenceTap(player, 0x5a) || !player.persistenceTestFrames(12)
			|| player.persistenceTestWram(0x031d) != 0
			|| player.persistenceTestBridge().engine().filename() != path
			|| player.persistenceTestBridge().engine().isDirty()
			|| !player.persistenceTestBridge().engine().text().contains(expected)) {
			qWarning("browsing to row %d did not open it: mode=%u file='%s' text='%s'",
				target, player.persistenceTestWram(0x031d),
				qPrintable(player.persistenceTestBridge().engine().filename()),
				qPrintable(player.persistenceTestBridge().engine().text().left(60)));
			return 135;
		}
		player.persistenceTestBridge().persistence().markCleanShutdown();
		return 0;
	}

	if (operation == QLatin1String("crash")) {
		RecompPlayer player(rom, catalog_root, recovery_root);
		if (!player.isValid() || !player.persistenceTestFrames(6)
			|| !persistenceType(player, expected)) {
			return 118;
		}
		FairyWriter::PersistenceSettings settings =
			player.persistenceTestBridge().persistence().settings();
		settings.mode =
			FairyWriter::PersistenceSettings::AutosaveMode::RecoveryOnly;
		settings.recovery_copies = 5;
		player.persistenceTestBridge().persistence().setSettings(settings);
		const FairyWriter::PersistenceResult checkpoint =
			player.persistenceTestBridge().persistence().checkpoint(false);
		if (!checkpoint.succeeded()) return 119;
		QTextStream(stdout) << checkpoint.path << '\n';
		QTextStream(stdout).flush();
		std::_Exit(23);
	}

	if (operation == QLatin1String("restore")) {
		RecompPlayer player(rom, catalog_root, recovery_root);
		if (!player.isValid() || !player.persistenceTestFrames(8)
			|| player.persistenceTestWram(0x031d) != 0x0b
			|| player.persistenceTestWram(0x0337) != 0x04
			|| !persistenceTap(player, 0x5a)
			|| !player.persistenceTestFrames(5)
			|| !player.persistenceTestBridge().engine().isDirty()
			|| !player.persistenceTestBridge().engine().text().contains(expected)
			|| !persistenceTap(player, 0x5a)
			|| !persistenceSaveAsNew(player, 0,
				QFileInfo(path).completeBaseName())
			|| player.persistenceTestBridge().engine().isDirty()
			|| QFileInfo(path).size() <= 0) {
			return 120;
		}
		player.persistenceTestBridge().persistence().markCleanShutdown();
		return 0;
	}

	return 121;
}
#endif

int fairywriter_run_embedded_snes(int argc, char** argv)
{
	QApplication application(argc, argv);
	// Set explicitly rather than inheriting the executable's basename, so that
	// AppDataLocation -- which owns recovery files, sessions and the recent list
	// -- resolves to the same identity on macOS, Linux and Windows.
	QApplication::setOrganizationName(QStringLiteral("navjack"));
	QApplication::setApplicationName(QStringLiteral("FairyWriter"));
	QApplication::setApplicationVersion(QStringLiteral(VERSIONSTR));
	if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--version")) {
		QTextStream(stdout) << "FairyWriter " VERSIONSTR "\n";
		return 0;
	}
#ifdef FAIRYWRITER_PERSISTENCE_TESTING
	if (argc >= 2
		&& QString::fromLocal8Bit(argv[1]) == QStringLiteral("--persistence-e2e-child")) {
		return runPersistenceE2eChild(application.arguments().mid(1));
	}
	if (argc >= 2
		&& QString::fromLocal8Bit(argv[1])
			== QStringLiteral("--persistence-cartridge-e2e-child")) {
		return runPersistenceCartridgeE2eChild(
			application.arguments().mid(1));
	}
#endif
	// One optional document path. Dragging a file onto the app or opening one
	// through a file association used to make the process exit with usage text.
	QString document;
	if (argc == 2) {
		document = QString::fromLocal8Bit(argv[1]);
	} else if (argc > 2) {
		qCritical("usage: FairyWriter [document]");
		return 2;
	}
	const QByteArray rom(reinterpret_cast<const char*>(FairyWriter::cartridgeImage()),
		static_cast<qsizetype>(FairyWriter::cartridgeImageSize()));
	RecompPlayer player(rom);
	if (!player.isValid()) return 1;
	if (!document.isEmpty()) player.openDocument(document);
	player.show();
	return application.exec();
}
