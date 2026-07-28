#include "snes_machine.h"
#include "cartridge_image.h"
#include "document_bridge.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
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
#include <cstdint>
#include <memory>

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
	case Qt::Key_Space: return {0x29}; case Qt::Key_Return: return {0x5a};
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

class RecompPlayer final : public QOpenGLWidget
{
public:
	explicit RecompPlayer(const QByteArray& rom)
		: m_machine(fairy_snes_create(reinterpret_cast<const std::uint8_t*>(rom.constData()),
			static_cast<std::size_t>(rom.size())), fairy_snes_destroy)
	{
		if (!m_machine) {
			reportFatal(QStringLiteral("The SNES runtime rejected the built-in FairyWriter cartridge."));
			return;
		}
		resetMailboxSram();
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
		m_recovery_timer.setInterval(30000);
		connect(&m_recovery_timer, &QTimer::timeout, this, [this] { autosaveRecovery(); });
		m_recovery_timer.start();
	}

	bool isValid() const { return m_machine != nullptr; }

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
	// Closing the window used to discard everything typed since the last
	// 30-second autosave tick, with no prompt. Flushing recovery here means the
	// worst case is zero lost work, and the cartridge's own
	// "RECOVERY AVAILABLE / ENTER RESTORES" screen offers it back on next
	// launch -- so no host-drawn dialog is needed and the cartridge keeps
	// ownership of the visible UI.
	void closeEvent(QCloseEvent* event) override
	{
		m_recovery_timer.stop();
		autosaveRecovery();
		QOpenGLWidget::closeEvent(event);
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
		const QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
		const QFileInfoList files = directory.entryInfoList(QStringList{QStringLiteral("*.recovery.*")}, QDir::Files, QDir::Time);
		if (!files.isEmpty()) m_recovery_path = files.front().absoluteFilePath();
	}

	void autosaveRecovery()
	{
		const auto& engine = m_bridge.engine();
		if (!engine.isDirty()) {
			if (!m_recovery_path.isEmpty()) QFile::remove(m_recovery_path);
			m_recovery_path.clear();
			return;
		}
		QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
		if (directory.path().isEmpty() || !directory.mkpath(QStringLiteral("."))) return;
		const QString base = engine.filename().isEmpty() ? QStringLiteral("untitled") : QFileInfo(engine.filename()).completeBaseName();
		const QString format = engine.format().isEmpty() ? QStringLiteral("odt") : engine.format();
		const QString path = directory.filePath(base + QStringLiteral(".recovery.") + format);
		if (engine.writeRecovery(path)) m_recovery_path = path;
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
		autosaveRecovery();
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
				if (command.kind == FairyWriter::DocumentBridge::CommandListFiles || command.kind == FairyWriter::DocumentBridge::CommandOpenFile || command.kind == FairyWriter::DocumentBridge::CommandSaveAs || command.kind == FairyWriter::DocumentBridge::CommandSaveAsNew || command.kind == FairyWriter::DocumentBridge::CommandCreateDirectory || command.kind == FairyWriter::DocumentBridge::CommandStatistics || command.kind == FairyWriter::DocumentBridge::CommandRecentFiles || command.kind == FairyWriter::DocumentBridge::CommandListRoots || command.kind == FairyWriter::DocumentBridge::CommandListRecovery || command.kind == FairyWriter::DocumentBridge::CommandRecover || command.kind == FairyWriter::DocumentBridge::CommandListSessions || command.kind == FairyWriter::DocumentBridge::CommandSwitchSession) {
					const QByteArray payload(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size()));
					const QString id = QString::fromUtf8(payload);
					if (command.revision == m_bridge.engine().revision()) {
						if (command.kind == FairyWriter::DocumentBridge::CommandListSessions) m_bridge.listSessions();
						else if (command.kind == FairyWriter::DocumentBridge::CommandSwitchSession) m_bridge.switchSession(m_catalog, id);
						else if (command.kind == FairyWriter::DocumentBridge::CommandListFiles) m_bridge.listFiles(m_catalog, id, (command.flags & 1) != 0, ((command.flags >> 8) & 0xff) * FairyWriter::DocumentBridge::FilePageSize);
						else if (command.kind == FairyWriter::DocumentBridge::CommandOpenFile) m_bridge.openFile(m_catalog, id);
						else if (command.kind == FairyWriter::DocumentBridge::CommandSaveAs) m_bridge.saveAs(m_catalog, id, (command.flags & 1) != 0);
						else if (command.kind == FairyWriter::DocumentBridge::CommandSaveAsNew) {
							const int separator = payload.indexOf('\0');
							if (separator >= 0) m_bridge.saveAsNew(m_catalog, QString::fromUtf8(payload.left(separator)), QString::fromUtf8(payload.mid(separator + 1)));
						}
						else if (command.kind == FairyWriter::DocumentBridge::CommandStatistics) m_bridge.publishStatistics();
						else if (command.kind == FairyWriter::DocumentBridge::CommandRecentFiles) m_bridge.listRecentFiles(m_catalog, ((command.flags >> 8) & 0xff) * FairyWriter::DocumentBridge::FilePageSize);
						else if (command.kind == FairyWriter::DocumentBridge::CommandListRoots) m_bridge.listRoots(m_catalog, ((command.flags >> 8) & 0xff) * FairyWriter::DocumentBridge::FilePageSize);
						else if (command.kind == FairyWriter::DocumentBridge::CommandListRecovery) {
							if (!m_recovery_path.isEmpty() && QFileInfo::exists(m_recovery_path)) m_bridge.publishRecoveryAvailable(m_recovery_path.section(QLatin1Char('.'), -1));
						} else if (command.kind == FairyWriter::DocumentBridge::CommandRecover) {
							const QByteArray token(reinterpret_cast<const char*>(command.payload.data()), static_cast<qsizetype>(command.payload.size()));
							if (token == QByteArrayLiteral("current") && !m_recovery_path.isEmpty()) m_bridge.recover(m_recovery_path);
						}
						else {
							const int separator = payload.indexOf('\0');
							if (separator >= 0) m_bridge.createDirectory(m_catalog, QString::fromUtf8(payload.left(separator)), QString::fromUtf8(payload.mid(separator + 1)));
						}
					}
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
		for (std::size_t i = 0; i < eventBytes.size(); ++i) eventBytes[i] = sramByte(FairyWriter::MailboxLayout::EventOffset + i);
		const std::size_t eventRead = sram16(8);
		if (eventRead != m_eventRead) {
			m_eventRead = eventRead;
			if (!m_bridge.events().importRaw(eventBytes.data(), m_eventRead, m_eventWrite)) {
				m_bridge.events().clear();
				m_eventRead = 0;
				m_eventWrite = 0;
				setSram16(6, 0);
				setSram16(8, 0);
			}
		}
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
	FairyWriter::FileCatalog m_catalog{QDir::homePath()};
	FairyWriter::MailboxRing m_commands{FairyWriter::MailboxLayout::CommandBytes};
	std::size_t m_commandRead = 0;
	std::size_t m_eventRead = 0;
	std::size_t m_eventWrite = 0;
	bool m_capsLock = false;
	bool m_bootstrapViewportCommitPending = true;
	QString m_recovery_path;
	// A document named on the command line, held until the cartridge has booted
	// far enough to keep a committed viewport. See openDocument().
	QString m_pending_document;
	static constexpr std::uint64_t PendingDocumentFrame = 30;
	std::uint64_t m_frameCounter = 0;
};

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
