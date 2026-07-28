// public-test-fixture generates a deterministic, openly publishable ODT used
// by FairyWriter's real document import, editing, and round-trip tests.
package main

import (
	"archive/zip"
	"bytes"
	"fmt"
	"os"
	"strings"
)

const (
	mimeType  = "application/vnd.oasis.opendocument.text"
	paragraph = "The writer crossed the quiet workshop and checked each page, " +
		"marking the margin where a sentence wrapped across the narrow screen. " +
		"Every revision kept its place, every saved chapter reopened cleanly, " +
		"and every deliberate keystroke moved through the cartridge mailbox " +
		"before the document engine committed it to durable storage."
)

func addFile(archive *zip.Writer, name string, method uint16, data string) error {
	header := &zip.FileHeader{Name: name, Method: method}
	writer, err := archive.CreateHeader(header)
	if err != nil {
		return err
	}
	_, err = writer.Write([]byte(data))
	return err
}

func contentXML() string {
	var body strings.Builder
	body.WriteString(`<text:p>FairyWriter public regression document</text:p>`)
	for chapterIndex, chapter := range []string{"Chapter I", "Chapter II", "Chapter III"} {
		body.WriteString(`<text:p>`)
		body.WriteString(chapter)
		body.WriteString(`</text:p>`)
		for paragraphIndex := 0; paragraphIndex < 52; paragraphIndex++ {
			fmt.Fprintf(&body,
				`<text:p>%s Chapter sample %d.%d remains intentionally fictional and contains no private writing.</text:p>`,
				paragraph, chapterIndex+1, paragraphIndex+1)
		}
	}

	return `<?xml version="1.0" encoding="UTF-8"?>` +
		`<office:document-content office:version="1.3"` +
		` xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"` +
		` xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">` +
		`<office:body><office:text>` + body.String() +
		`</office:text></office:body></office:document-content>`
}

func buildFixture() ([]byte, error) {
	var output bytes.Buffer
	archive := zip.NewWriter(&output)

	// ODF requires this to be the first archive entry and stored without
	// compression. FairyWriter also checks that exact on-disk signature.
	if err := addFile(archive, "mimetype", zip.Store, mimeType); err != nil {
		return nil, err
	}
	if err := addFile(archive, "content.xml", zip.Deflate, contentXML()); err != nil {
		return nil, err
	}
	if err := addFile(archive, "styles.xml", zip.Deflate,
		`<?xml version="1.0" encoding="UTF-8"?>`+
			`<office:document-styles office:version="1.3"`+
			` xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"`+
			` xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0">`+
			`<office:styles><style:default-style style:family="paragraph"/></office:styles>`+
			`</office:document-styles>`); err != nil {
		return nil, err
	}
	if err := addFile(archive, "META-INF/manifest.xml", zip.Deflate,
		`<?xml version="1.0" encoding="UTF-8"?>`+
			`<manifest:manifest manifest:version="1.3"`+
			` xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">`+
			`<manifest:file-entry manifest:full-path="/" manifest:media-type="`+
			mimeType+`"/>`+
			`<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>`+
			`<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>`+
			`</manifest:manifest>`); err != nil {
		return nil, err
	}
	if err := archive.Close(); err != nil {
		return nil, err
	}
	return output.Bytes(), nil
}

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: public-test-fixture OUTPUT.odt")
		os.Exit(2)
	}
	fixture, err := buildFixture()
	if err == nil {
		err = os.WriteFile(os.Args[1], fixture, 0o644)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
