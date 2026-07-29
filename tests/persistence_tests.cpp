#include "document_engine.h"
#include "document_persistence.h"
#include "document_writer.h"
#include "markdown_codec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextDocument>

#include <cstdio>
#include <cstdlib>

namespace {

void expect(bool condition, const char* message)
{
	if (condition) return;
	std::fprintf(stderr, "FAIL: %s\n", message);
	std::exit(1);
}

QByteArray readAll(const QString& path)
{
	QFile file(path);
	expect(file.open(QIODevice::ReadOnly), "test fixture opens for reading");
	return file.readAll();
}

void writeAll(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
		"test fixture opens for writing");
	expect(file.write(bytes) == bytes.size(), "test fixture writes completely");
	expect(file.flush(), "test fixture flushes");
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
	QGuiApplication application(argc, argv);
	QCoreApplication::setOrganizationName(QStringLiteral("FairyWriterTests"));
	QCoreApplication::setApplicationName(QStringLiteral("Persistence"));
	QStandardPaths::setTestModeEnabled(true);

	QTemporaryDir temporary;
	expect(temporary.isValid(), "temporary persistence root exists");
	const QString recovery_root = temporary.filePath(QStringLiteral("recovery"));

	FairyWriter::DocumentEngine engine;
	FairyWriter::DocumentPersistence persistence(engine, recovery_root);
	expect(!engine.isDirty(), "new empty document starts clean");
	expect(engine.format() == QLatin1String("odt"),
		"new documents default to ODT");
	expect(engine.insertText(engine.revision(), QStringLiteral("durable alpha")),
		"fixture text inserts");
	const std::uint64_t edited_generation = engine.contentGeneration();
	expect(engine.isDirty() && edited_generation == 1,
		"content edit advances content generation and marks dirty");

	FairyWriter::PersistenceResult result = persistence.save();
	expect(result.error == FairyWriter::PersistenceError::NeedsSaveAs,
		"first Save explicitly requests Save As");

	const QString odt = temporary.filePath(QStringLiteral("Story.odt"));
	result = persistence.saveAs(odt, false);
	expect(result.succeeded() && QFileInfo(odt).size() > 0 && !engine.isDirty(),
		"Save As New durably creates a non-empty ODT and marks it clean");

	const std::uint64_t clean_generation = engine.contentGeneration();
	const std::uint64_t clean_revision = engine.revision();
	expect(engine.moveCursor(clean_revision, QTextCursor::Start),
		"caret movement succeeds");
	expect(engine.revision() != clean_revision
			&& engine.contentGeneration() == clean_generation
			&& !engine.isDirty(),
		"caret movement changes viewport revision only");
	expect(engine.selectAll(engine.revision())
			&& engine.contentGeneration() == clean_generation
			&& !engine.isDirty(),
		"selection changes neither content generation nor dirty state");
	expect(engine.findNext(engine.revision(), QStringLiteral("alpha"))
			&& engine.contentGeneration() == clean_generation
			&& !engine.isDirty(),
		"Find changes viewport state without dirtying the document");

	const QString missing = temporary.filePath(
		QStringLiteral("missing/subdirectory/never-created.odt"));
	result = persistence.saveAs(missing, false);
	expect(result.error == FairyWriter::PersistenceError::WriteFailed
			&& !QFileInfo::exists(missing),
		"failed Save As New leaves no empty placeholder");
	result = persistence.saveAs(odt, false);
	expect(result.error == FairyWriter::PersistenceError::AlreadyExists,
		"Save As New never overwrites an existing target");

	expect(engine.insertText(engine.revision(), QStringLiteral(" changed")),
		"external-conflict fixture becomes dirty");
	const QDateTime original_modified = QFileInfo(odt).lastModified();
	writeAll(odt, QByteArrayLiteral("external replacement with different bytes"));
	QFile external(odt);
	expect(external.open(QIODevice::ReadWrite), "external-conflict file reopens");
	expect(external.setFileTime(original_modified, QFileDevice::FileModificationTime),
		"external replacement restores the old modification timestamp");
	external.close();
	result = persistence.save();
	expect(result.error == FairyWriter::PersistenceError::ExternalConflict,
		"SHA-256 detects an external replacement even when mtime is unchanged");
	expect(readAll(odt) == QByteArrayLiteral("external replacement with different bytes"),
		"conflict preserves the externally replaced file");

	const QString precommit_race =
		temporary.filePath(QStringLiteral("PrecommitRace.odt"));
	writeAll(precommit_race, QByteArrayLiteral("primary before encode"));
	const FairyWriter::FileFingerprint precommit_expected =
		FairyWriter::FileFingerprint::read(precommit_race);
	QTextDocument staged_document;
	staged_document.setPlainText(QStringLiteral("fully encoded staged replacement"));
	DocumentWriter staged_writer;
	staged_writer.setDocument(&staged_document);
	staged_writer.setFileName(precommit_race);
	staged_writer.setType(QStringLiteral("odt"));
	staged_writer.setPreCommitCheck([&] {
		writeAll(precommit_race,
			QByteArrayLiteral("external winner during staged encode"));
		return precommit_expected.sameContent(
			FairyWriter::FileFingerprint::read(precommit_race));
	});
	expect(!staged_writer.write()
			&& readAll(precommit_race)
				== QByteArrayLiteral("external winner during staged encode")
			&& QDir(temporary.path()).entryList(
				QStringList{QStringLiteral(".PrecommitRace.odt.*")},
				QDir::Files | QDir::Hidden).isEmpty(),
		"the immediate pre-commit SHA check preserves a racing external write "
		"and removes the staged file");

	const QString corrupt_odt = temporary.filePath(QStringLiteral("Corrupt.odt"));
	writeAll(corrupt_odt, QByteArrayLiteral("not an ODT container"));
	const QString active_before_failed_load = engine.text();
	result = persistence.load(corrupt_odt);
	expect(result.error == FairyWriter::PersistenceError::LoadFailed
			&& engine.text() == active_before_failed_load
			&& engine.filename() == odt,
		"a corrupt load is parsed before and cannot replace the active document");

	const QString unicode_path = temporary.filePath(
		QString::fromUtf8("Résumé 雪.odt"));
	FairyWriter::DocumentEngine unicode_engine;
	FairyWriter::DocumentPersistence unicode_persistence(unicode_engine,
		temporary.filePath(QStringLiteral("unicode-recovery")));
	expect(unicode_engine.insertText(unicode_engine.revision(),
			QString::fromUtf8("Unicode snow 雪"))
			&& unicode_persistence.saveAs(unicode_path, false).succeeded(),
		"Unicode paths save through the native filesystem API");
	FairyWriter::DocumentEngine unicode_loaded;
	FairyWriter::DocumentPersistence unicode_loader(unicode_loaded,
		temporary.filePath(QStringLiteral("unicode-load-recovery")));
	expect(unicode_loader.load(unicode_path).succeeded()
			&& unicode_loaded.text() == QString::fromUtf8("Unicode snow 雪"),
		"Unicode paths load in a fresh document engine");

	const QString read_only_path = temporary.filePath(QStringLiteral("ReadOnly.odt"));
	FairyWriter::DocumentEngine read_only_fixture;
	FairyWriter::DocumentPersistence read_only_writer(read_only_fixture,
		temporary.filePath(QStringLiteral("readonly-recovery")));
	expect(read_only_fixture.insertText(read_only_fixture.revision(),
			QStringLiteral("original read-only bytes"))
			&& read_only_writer.saveAs(read_only_path, false).succeeded(),
		"read-only fixture saves");
	const QByteArray read_only_bytes = readAll(read_only_path);
	expect(QFile::setPermissions(read_only_path,
			QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther),
		"read-only fixture permissions apply");
	result = unicode_persistence.saveAs(read_only_path, true);
	expect(result.error == FairyWriter::PersistenceError::ReadOnly
			&& readAll(read_only_path) == read_only_bytes,
		"read-only replacement fails visibly and preserves the original");
	QFile::setPermissions(read_only_path,
		QFileDevice::ReadOwner | QFileDevice::WriteOwner);

	const QString concurrent_path = temporary.filePath(
		QStringLiteral("Concurrent.odt"));
	FairyWriter::DocumentEngine first_instance;
	FairyWriter::DocumentPersistence first_persistence(first_instance,
		temporary.filePath(QStringLiteral("concurrent-first")));
	expect(first_instance.insertText(first_instance.revision(), QStringLiteral("base"))
			&& first_persistence.saveAs(concurrent_path, false).succeeded(),
		"concurrent fixture saves");
	FairyWriter::DocumentEngine second_instance;
	FairyWriter::DocumentPersistence second_persistence(second_instance,
		temporary.filePath(QStringLiteral("concurrent-second")));
	expect(second_persistence.load(concurrent_path).succeeded(),
		"second instance loads the same primary file");
	expect(first_instance.insertText(first_instance.revision(), QStringLiteral(" first"))
			&& first_persistence.save().succeeded(),
		"first instance commits a new generation");
	expect(second_instance.insertText(second_instance.revision(), QStringLiteral(" second")),
		"second instance edits its stale generation");
	result = second_persistence.save();
	expect(result.error == FairyWriter::PersistenceError::ExternalConflict,
		"a concurrent stale instance cannot overwrite the newer primary");

	FairyWriter::DocumentEngine recovery_engine;
	FairyWriter::DocumentPersistence recovery(recovery_engine, recovery_root);
	FairyWriter::PersistenceSettings settings = recovery.settings();
	settings.mode = FairyWriter::PersistenceSettings::AutosaveMode::RecoveryOnly;
	settings.interval_minutes = 1;
	settings.recovery_copies = 2;
	recovery.setSettings(settings);
	recovery.beginSession();

	expect(recovery_engine.insertText(recovery_engine.revision(), QStringLiteral("A")),
		"recovery state A inserts");
	const FairyWriter::PersistenceResult state_a = recovery.checkpoint();
	expect(state_a.succeeded(), "recovery state A commits");
	expect(recovery.checkpoint().error == FairyWriter::PersistenceError::NoChange,
		"adjacent identical recovery state is deduplicated");
	expect(recovery_engine.insertText(recovery_engine.revision(), QStringLiteral("B")),
		"recovery state B inserts");
	expect(recovery.checkpoint().succeeded(), "recovery state B commits");
	expect(recovery_engine.undo(recovery_engine.revision()),
		"recovery state returns from B to A through real Undo");
	expect(recovery.checkpoint().succeeded(),
		"A-B-A remains meaningful temporal recovery history");
	expect(recovery.recoveryRecords().size() == 2,
		"recovery rotation retains exactly the configured count");
	const std::uint64_t recovery_generation = recovery_engine.contentGeneration();
	expect(recovery_engine.moveCursor(recovery_engine.revision(), QTextCursor::Start),
		"recovery cursor-only change succeeds");
	expect(recovery_engine.contentGeneration() == recovery_generation
			&& recovery.checkpoint().error == FairyWriter::PersistenceError::NoChange,
		"cursor-only activity produces no recovery generation");
	FairyWriter::DocumentEngine formatting_engine;
	FairyWriter::DocumentPersistence formatting_persistence(formatting_engine,
		temporary.filePath(QStringLiteral("formatting-recovery")));
	FairyWriter::PersistenceSettings formatting_settings =
		formatting_persistence.settings();
	formatting_settings.mode =
		FairyWriter::PersistenceSettings::AutosaveMode::RecoveryOnly;
	formatting_settings.recovery_copies = 5;
	formatting_persistence.setSettings(formatting_settings);
	expect(formatting_engine.insertText(formatting_engine.revision(),
			QStringLiteral("formatting"))
			&& formatting_persistence.checkpoint().succeeded(),
		"format-only fixture writes its baseline");
	const std::uint64_t formatting_generation =
		formatting_engine.contentGeneration();
	FairyWriter::MailboxRecord format_only;
	format_only.kind = FairyWriter::DocumentEngine::ToggleBold;
	expect(formatting_engine.selectAll(formatting_engine.revision()),
		"format-only fixture selects the document");
	expect(formatting_engine.cursor().hasSelection(),
		"format-only fixture retains its selection");
	expect(!formatting_engine.isReadOnly(),
		"format-only fixture remains writable");
	format_only.revision = formatting_engine.revision();
	expect(formatting_engine.apply(format_only),
		"format-only recovery edit applies bold");
	expect(formatting_engine.contentGeneration() == formatting_generation + 1
			&& formatting_persistence.checkpoint().succeeded(),
		"formatting-only edits advance content generation and checkpoint");

	const QString corrupt_path = recovery.recoveryRecords().front().path;
	writeAll(corrupt_path, QByteArrayLiteral("corrupt newest recovery"));
	bool had_corrupt = false;
	const QVector<FairyWriter::RecoveryRecord> valid =
		recovery.recoveryRecords(&had_corrupt);
	expect(had_corrupt && !valid.isEmpty(),
		"corrupt newest recovery is skipped while an older valid state survives");
	FairyWriter::DocumentEngine restored;
	FairyWriter::DocumentPersistence restore_persistence(restored,
		temporary.filePath(QStringLiteral("restore-session")));
	expect(restore_persistence.recover(valid.front().path).succeeded(),
		"valid older recovery restores");
	expect(restored.isDirty()
			&& restored.contentHash() == valid.front().snapshot.content_hash,
		"restored recovery preserves content and is always dirty");

	settings.recovery_copies = 0;
	recovery.setSettings(settings);
	expect(recovery.checkpoint().error == FairyWriter::PersistenceError::Disabled,
		"zero retained copies disables timed checkpoints");
	const FairyWriter::PersistenceResult manual = recovery.checkpoint(true);
	expect(manual.succeeded() && recovery.recoveryRecords().size() == 1,
		"explicit Checkpoint creates one pinned generation when timed saving is off");

	for (const int retained : {1, 5, 255}) {
		FairyWriter::DocumentEngine rotation_engine;
		FairyWriter::DocumentPersistence rotation(rotation_engine,
			temporary.filePath(QStringLiteral("rotation-%1").arg(retained)));
		FairyWriter::PersistenceSettings rotation_settings = rotation.settings();
		rotation_settings.mode =
			FairyWriter::PersistenceSettings::AutosaveMode::RecoveryOnly;
		rotation_settings.recovery_copies = static_cast<std::uint8_t>(retained);
		rotation.setSettings(rotation_settings);
		for (int generation = 0; generation < retained + 2; ++generation) {
			expect(rotation_engine.insertText(rotation_engine.revision(),
					QStringLiteral("x")),
				"rotation generation inserts");
			expect(rotation.checkpoint().succeeded(),
				"rotation generation commits");
		}
		expect(rotation.recoveryRecords().size() == retained,
			"recovery rotation retains the exact configured count");
	}

	const QString clean_root = temporary.filePath(QStringLiteral("clean-session"));
	{
		FairyWriter::DocumentEngine clean_engine;
		FairyWriter::DocumentPersistence clean(clean_engine, clean_root);
		FairyWriter::PersistenceSettings clean_settings = clean.settings();
		clean_settings.mode =
			FairyWriter::PersistenceSettings::AutosaveMode::SaveAndRecovery;
		clean_settings.recovery_copies = 5;
		clean.setSettings(clean_settings);
		clean.beginSession();
		const QString clean_primary =
			temporary.filePath(QStringLiteral("CleanPrimary.odt"));
		expect(clean_engine.insertText(clean_engine.revision(), QStringLiteral("clean"))
				&& clean.saveAs(clean_primary, false).succeeded()
				&& clean_engine.insertText(clean_engine.revision(),
					QStringLiteral(" saved"))
				&& clean.timedAutosave().succeeded(),
			"clean-session checkpoint and identical primary save commit");
		clean.markCleanShutdown();
	}
	{
		FairyWriter::DocumentEngine clean_probe_engine;
		FairyWriter::DocumentPersistence clean_probe(clean_probe_engine, clean_root);
		expect(clean_probe.recoveryPromptCandidates().isEmpty(),
			"a clean shutdown suppresses startup prompts for history matching "
			"the saved primary");
	}

	const QString clean_newer_root =
		temporary.filePath(QStringLiteral("clean-newer-session"));
	{
		FairyWriter::DocumentEngine clean_newer_engine;
		FairyWriter::DocumentPersistence clean_newer(
			clean_newer_engine, clean_newer_root);
		clean_newer.beginSession();
		expect(clean_newer_engine.insertText(clean_newer_engine.revision(),
				QStringLiteral("newer automatic checkpoint"))
				&& clean_newer.checkpoint().succeeded(),
			"clean transition recovery-only checkpoint commits");
		clean_newer.markCleanShutdown();
	}
	{
		FairyWriter::DocumentEngine clean_newer_probe_engine;
		FairyWriter::DocumentPersistence clean_newer_probe(
			clean_newer_probe_engine, clean_newer_root);
		expect(clean_newer_probe.recoveryPromptCandidates().size() == 1,
			"a checkpoint newer than the primary remains a startup candidate "
			"after a clean transition");
	}

	const QString manual_root = temporary.filePath(QStringLiteral("manual-session"));
	{
		FairyWriter::DocumentEngine manual_engine;
		FairyWriter::DocumentPersistence pinned(manual_engine, manual_root);
		pinned.beginSession();
		expect(manual_engine.insertText(manual_engine.revision(), QStringLiteral("pinned"))
				&& pinned.checkpoint(true).succeeded(),
			"manual checkpoint commits");
		pinned.markCleanShutdown();
	}
	{
		FairyWriter::DocumentEngine manual_probe_engine;
		FairyWriter::DocumentPersistence manual_probe(manual_probe_engine, manual_root);
		expect(manual_probe.recoveryPromptCandidates().size() == 1,
			"a newer explicit checkpoint remains startup-recoverable after clean exit");
	}

	const QString autosaved_primary = temporary.filePath(
		QStringLiteral("Autosaved.odt"));
	const QString autosaved_root = temporary.filePath(
		QStringLiteral("autosaved-history"));
	{
		FairyWriter::DocumentEngine autosaved_engine;
		FairyWriter::DocumentPersistence autosaved(autosaved_engine, autosaved_root);
		FairyWriter::PersistenceSettings autosaved_settings = autosaved.settings();
		autosaved_settings.mode =
			FairyWriter::PersistenceSettings::AutosaveMode::SaveAndRecovery;
		autosaved_settings.recovery_copies = 5;
		autosaved.setSettings(autosaved_settings);
		expect(autosaved_engine.insertText(autosaved_engine.revision(),
				QStringLiteral("primary"))
				&& autosaved.saveAs(autosaved_primary, false).succeeded()
				&& autosaved_engine.insertText(autosaved_engine.revision(),
					QStringLiteral(" autosaved")),
			"Save plus Recovery fixture becomes dirty");
		result = autosaved.timedAutosave();
		const QVector<FairyWriter::RecoveryRecord> history =
			autosaved.recoveryRecords();
		expect(result.succeeded() && !autosaved_engine.isDirty()
				&& history.size() == 1 && history.front().matches_primary,
			"timed Save plus Recovery checkpoints first, saves identical primary, "
			"and retains matching history");
		autosaved.markCleanShutdown();
	}
	{
		FairyWriter::DocumentEngine autosaved_probe_engine;
		FairyWriter::DocumentPersistence autosaved_probe(
			autosaved_probe_engine, autosaved_root);
		expect(autosaved_probe.recoveryRecords().size() == 1
				&& autosaved_probe.recoveryPromptCandidates().isEmpty(),
			"post-save recovery history survives without causing a startup prompt");
	}

	const QString resolved_history_root =
		temporary.filePath(QStringLiteral("resolved-history"));
	const QString resolved_history_primary =
		temporary.filePath(QStringLiteral("ResolvedHistory.odt"));
	{
		FairyWriter::DocumentEngine resolved_engine;
		FairyWriter::DocumentPersistence resolved(
			resolved_engine, resolved_history_root);
		FairyWriter::PersistenceSettings resolved_settings = resolved.settings();
		resolved_settings.mode =
			FairyWriter::PersistenceSettings::AutosaveMode::RecoveryOnly;
		resolved_settings.recovery_copies = 5;
		resolved.setSettings(resolved_settings);
		expect(resolved_engine.insertText(resolved_engine.revision(),
				QStringLiteral("base"))
				&& resolved.saveAs(resolved_history_primary, false).succeeded()
				&& resolved_engine.insertText(resolved_engine.revision(),
					QStringLiteral(" checkpoint one"))
				&& resolved.checkpoint().succeeded()
				&& resolved_engine.insertText(resolved_engine.revision(),
					QStringLiteral(" checkpoint two"))
				&& resolved.checkpoint().succeeded()
				&& resolved_engine.insertText(resolved_engine.revision(),
					QStringLiteral(" saved without a matching checkpoint"))
				&& resolved.save().succeeded(),
			"a primary save resolves every older recovery generation");
		const QVector<FairyWriter::RecoveryRecord> history =
			resolved.recoveryRecords();
		expect(history.size() == 2
				&& history[0].transition_resolved
				&& history[1].transition_resolved
				&& resolved.recoveryPromptCandidates().isEmpty(),
			"resolved generations remain in history without stale startup prompts");
		expect(resolved_engine.insertText(resolved_engine.revision(),
				QStringLiteral(" newer"))
				&& resolved.checkpoint().succeeded()
				&& resolved.recoveryPromptCandidates().size() == 1,
			"a checkpoint after the primary save becomes the sole startup candidate");
		expect(resolved.discardRecoveryCandidates().succeeded()
				&& resolved.recoveryRecords().size() == 3
				&& resolved.recoveryPromptCandidates().isEmpty(),
			"Discard preserves history while resolving its startup candidates");
	}

	const QByteArray markdown =
		QByteArrayLiteral("---\ntitle: Durable Markdown\n---\n\n"
			"# Heading\n\n"
			"*emphasis* **strong** ~~strike~~\n\n"
			"> quoted\n\n"
			"- bullet\n- [x] task\n\n1. ordered\n\n"
			"`inline code`\n\n```cpp\nint value = 1;\n```\n\n"
			"| A | B |\n| - | - |\n| 1 | 2 |\n\n"
			"<https://example.invalid/path>\n\n---\n\n"
			"<script>never execute</script>\n\n"
			"![alt](file:///tmp/never-load.png) [link](javascript:alert(1))\n");
	const QString markdown_path = temporary.filePath(QStringLiteral("Source.markdown"));
	writeAll(markdown_path, markdown);
	FairyWriter::DocumentEngine markdown_engine;
	FairyWriter::DocumentPersistence markdown_persistence(markdown_engine,
		temporary.filePath(QStringLiteral("markdown-recovery")));
	expect(markdown_persistence.load(markdown_path).succeeded()
			&& markdown_engine.format() == QLatin1String("md")
			&& FairyWriter::MarkdownCodec::validate(markdown),
		"Markdown loads through the pinned GFM parser");
	const QString safe_html = FairyWriter::MarkdownCodec::safeHtml(markdown);
	expect(safe_html.contains(QStringLiteral("<table>"))
			&& safe_html.contains(QStringLiteral("<em>emphasis</em>"))
			&& safe_html.contains(QStringLiteral("<strong>strong</strong>"))
			&& safe_html.contains(QStringLiteral("<del>strike</del>"))
			&& safe_html.contains(QStringLiteral("<blockquote>"))
			&& safe_html.contains(QStringLiteral("<ol>"))
			&& safe_html.contains(QStringLiteral("<code>inline code</code>"))
			&& !safe_html.contains(QStringLiteral("<script>"))
			&& !safe_html.contains(QStringLiteral("javascript:")),
		"GFM reference rendering covers the supported semantic structures while "
		"keeping active content inert");
	const QString markdown_copy = temporary.filePath(QStringLiteral("Copy.md"));
	expect(markdown_persistence.saveAs(markdown_copy, false).succeeded()
			&& readAll(markdown_copy) == markdown,
		"untouched Markdown Save As is byte-for-byte stable");
	expect(markdown_engine.setMarkdownSourceMode(true),
		"Markdown source mode opens");
	expect(markdown_engine.moveCursor(markdown_engine.revision(), QTextCursor::End)
			&& markdown_engine.insertText(markdown_engine.revision(),
				QStringLiteral("\nsource edit\n"))
			&& markdown_engine.setMarkdownSourceMode(false),
		"source edit reparses into rendered mode");
	expect(markdown_persistence.save().succeeded(),
		"edited Markdown saves through the same persistence path");
	const QByteArray edited_markdown = readAll(markdown_copy);
	expect(edited_markdown.contains(QByteArrayLiteral("source edit"))
			&& edited_markdown.contains(QByteArrayLiteral("<script>never execute</script>"))
			&& FairyWriter::MarkdownCodec::validate(edited_markdown),
		"source editing preserves inert raw HTML and remains valid GFM");
	expect(markdown_engine.moveCursor(markdown_engine.revision(), QTextCursor::End)
			&& markdown_engine.insertText(markdown_engine.revision(),
				QStringLiteral("\nrendered edit\n"))
			&& markdown_persistence.save().succeeded(),
		"rendered Markdown editing saves through semantic projection");
	const QByteArray rendered_edit = readAll(markdown_copy);
	expect(rendered_edit.contains(QByteArrayLiteral("rendered edit"))
			&& rendered_edit.contains(
				QByteArrayLiteral("<script>never execute</script>"))
			&& rendered_edit.startsWith(
				QByteArrayLiteral("---\ntitle: Durable Markdown\n---"))
			&& FairyWriter::MarkdownCodec::validate(rendered_edit),
		"rendered edits preserve front matter and inert raw HTML while remaining GFM");
	expect(markdown_engine.findNext(markdown_engine.revision(),
			QStringLiteral("Heading"))
			&& markdown_engine.insertText(markdown_engine.revision(),
				QStringLiteral("Renamed"))
			&& markdown_persistence.save().succeeded(),
		"a rendered edit can replace a semantic literal in the middle");
	const QByteArray minimal_rendered_edit = readAll(markdown_copy);
	QByteArray expected_minimal_edit = rendered_edit;
	expected_minimal_edit.replace(
		expected_minimal_edit.indexOf(QByteArrayLiteral("Heading")),
		QByteArrayLiteral("Heading").size(), QByteArrayLiteral("Renamed"));
	expect(minimal_rendered_edit == expected_minimal_edit,
		"a unique rendered literal replacement rewrites only its exact UTF-8 "
		"source span");

	std::puts("All FairyWriter persistence tests passed.");
	return 0;
}
