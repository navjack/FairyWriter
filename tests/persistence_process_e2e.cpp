#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {

struct ChildResult final {
	int code = -1;
	QProcess::ExitStatus status = QProcess::CrashExit;
	QByteArray output;
	QByteArray error;
};

ChildResult runChild(const QString& executable, const QStringList& arguments)
{
	QProcess child;
	child.setProcessChannelMode(QProcess::SeparateChannels);
	child.start(executable, arguments);
	if (!child.waitForStarted(10000) || !child.waitForFinished(30000)) {
		child.kill();
		child.waitForFinished(5000);
		return {-1, QProcess::CrashExit, child.readAllStandardOutput(),
			child.readAllStandardError()};
	}
	return {child.exitCode(), child.exitStatus(), child.readAllStandardOutput(),
		child.readAllStandardError()};
}

void expect(bool condition, const char* message, const ChildResult* child = nullptr)
{
	if (condition) return;
	std::fprintf(stderr, "FAIL: %s\n", message);
	if (child) {
		std::fprintf(stderr, "child code=%d status=%d\nstdout:\n%s\nstderr:\n%s\n",
			child->code, int(child->status), child->output.constData(),
			child->error.constData());
	}
	std::exit(1);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	expect(argc == 2, "process test receives the production executable");
	const QString executable = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
	expect(QFileInfo::exists(executable), "production executable exists");

	QTemporaryDir temporary;
	expect(temporary.isValid(), "process test temporary root exists");
	const QString recovery = temporary.filePath(QStringLiteral("recovery"));

	const QStringList formats = {
		QStringLiteral("odt"),
		QStringLiteral("docx"),
		QStringLiteral("rtf"),
		QStringLiteral("md")
	};
	for (const QString& format : formats) {
		const QString path = temporary.filePath(
			QStringLiteral("life%1.%1").arg(format));
		ChildResult created = runChild(executable, {
			QStringLiteral("--persistence-cartridge-e2e-child"),
			QStringLiteral("create"), format, path, recovery
		});
		expect(created.status == QProcess::NormalExit && created.code == 0
				&& QFileInfo(path).size() > 0,
			"fresh process creates and durably saves a real document", &created);

		ChildResult loaded = runChild(executable, {
			QStringLiteral("--persistence-cartridge-e2e-child"),
			QStringLiteral("load"), format, path, recovery
		});
		expect(loaded.status == QProcess::NormalExit && loaded.code == 0,
			"second fresh process loads saved text and formatting", &loaded);
	}

	const QString crash_path = temporary.filePath(QStringLiteral("recovered.odt"));
	ChildResult crashed = runChild(executable, {
		QStringLiteral("--persistence-cartridge-e2e-child"),
		QStringLiteral("crash"), QStringLiteral("crash"), crash_path, recovery
	});
	expect(crashed.code == 23 && !crashed.output.trimmed().isEmpty(),
		"child exits without a clean-shutdown marker after a durable checkpoint",
		&crashed);

	ChildResult restored = runChild(executable, {
		QStringLiteral("--persistence-cartridge-e2e-child"),
		QStringLiteral("restore"), QStringLiteral("crash"), crash_path, recovery
	});
	expect(restored.status == QProcess::NormalExit && restored.code == 0
			&& QFileInfo(crash_path).size() > 0,
		"fresh process discovers, restores as dirty, and saves crash recovery",
		&restored);

	ChildResult reopened = runChild(executable, {
		QStringLiteral("--persistence-cartridge-e2e-child"),
		QStringLiteral("load"), QStringLiteral("crash"), crash_path, recovery
	});
	expect(reopened.status == QProcess::NormalExit && reopened.code == 0,
		"third fresh process reopens the recovered primary document", &reopened);

	std::puts("FairyWriter cartridge-input process save/exit/relaunch tests passed.");
	return 0;
}
