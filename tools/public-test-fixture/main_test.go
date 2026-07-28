package main

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"
)

func TestFixtureIsDeterministicSubstantialODT(t *testing.T) {
	first, err := buildFixture()
	if err != nil {
		t.Fatal(err)
	}
	second, err := buildFixture()
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(first, second) {
		t.Fatal("fixture generator is not deterministic")
	}
	if !bytes.Contains(first[:77], []byte("mimetype"+mimeType)) {
		t.Fatal("ODT mimetype is not the first uncompressed archive entry")
	}

	reader, err := zip.NewReader(bytes.NewReader(first), int64(len(first)))
	if err != nil {
		t.Fatal(err)
	}
	var content string
	for _, file := range reader.File {
		if file.Name != "content.xml" {
			continue
		}
		stream, openErr := file.Open()
		if openErr != nil {
			t.Fatal(openErr)
		}
		data, readErr := io.ReadAll(stream)
		closeErr := stream.Close()
		if readErr != nil {
			t.Fatal(readErr)
		}
		if closeErr != nil {
			t.Fatal(closeErr)
		}
		content = string(data)
	}
	if len(content) < 38_000 {
		t.Fatalf("fixture content is too small: %d bytes", len(content))
	}
	if strings.Count(content, "Chapter I") != 3 {
		t.Fatal("fixture must contain exactly three chapter markers")
	}
	if strings.Count(content, "private writing") != 156 {
		t.Fatal("fixture did not contain the expected generated paragraphs")
	}
}
