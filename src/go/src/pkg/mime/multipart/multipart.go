// Copyright 2010 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//

/*
Package multipart implements MIME multipart parsing, as defined in RFC
2046.

The implementation is sufficient for HTTP (RFC 2388) and the multipart
bodies generated by popular browsers.
*/
package multipart

import (
	"bufio"
	"bytes"
	"io"
	"mime"
	"net/textproto"
	"os"
	"regexp"
	"strings"
)

var headerRegexp *regexp.Regexp = regexp.MustCompile("^([a-zA-Z0-9\\-]+): *([^\r\n]+)")

// Reader is an iterator over parts in a MIME multipart body.
// Reader's underlying parser consumes its input as needed.  Seeking
// isn't supported.
type Reader interface {
	// NextPart returns the next part in the multipart, or (nil,
	// nil) on EOF.  An error is returned if the underlying reader
	// reports errors, or on truncated or otherwise malformed
	// input.
	NextPart() (*Part, os.Error)
}

// A Part represents a single part in a multipart body.
type Part struct {
	// The headers of the body, if any, with the keys canonicalized
	// in the same fashion that the Go http.Request headers are.
	// i.e. "foo-bar" changes case to "Foo-Bar"
	Header textproto.MIMEHeader

	buffer *bytes.Buffer
	mr     *multiReader
}

// FormName returns the name parameter if p has a Content-Disposition
// of type "form-data".  Otherwise it returns the empty string.
func (p *Part) FormName() string {
	// See http://tools.ietf.org/html/rfc2183 section 2 for EBNF
	// of Content-Disposition value format.
	v := p.Header.Get("Content-Disposition")
	if v == "" {
		return ""
	}
	d, params := mime.ParseMediaType(v)
	if d != "form-data" {
		return ""
	}
	return params["name"]
}

// NewReader creates a new multipart Reader reading from r using the
// given MIME boundary.
func NewReader(reader io.Reader, boundary string) Reader {
	return &multiReader{
		boundary:     boundary,
		dashBoundary: "--" + boundary,
		endLine:      "--" + boundary + "--",
		bufReader:    bufio.NewReader(reader),
	}
}

// Implementation ....

type devNullWriter bool

func (*devNullWriter) Write(p []byte) (n int, err os.Error) {
	return len(p), nil
}

var devNull = devNullWriter(false)

func newPart(mr *multiReader) (bp *Part, err os.Error) {
	bp = new(Part)
	bp.Header = make(map[string][]string)
	bp.mr = mr
	bp.buffer = new(bytes.Buffer)
	if err = bp.populateHeaders(); err != nil {
		bp = nil
	}
	return
}

func (bp *Part) populateHeaders() os.Error {
	for {
		line, err := bp.mr.bufReader.ReadString('\n')
		if err != nil {
			return err
		}
		if line == "\n" || line == "\r\n" {
			return nil
		}
		if matches := headerRegexp.FindStringSubmatch(line); len(matches) == 3 {
			bp.Header.Add(matches[1], matches[2])
			continue
		}
		return os.NewError("Unexpected header line found parsing multipart body")
	}
	panic("unreachable")
}

// Read reads the body of a part, after its headers and before the
// next part (if any) begins.
func (bp *Part) Read(p []byte) (n int, err os.Error) {
	for {
		if bp.buffer.Len() >= len(p) {
			// Internal buffer of unconsumed data is large enough for
			// the read request.  No need to parse more at the moment.
			break
		}
		if !bp.mr.ensureBufferedLine() {
			return 0, io.ErrUnexpectedEOF
		}
		if bp.mr.bufferedLineIsBoundary() {
			// Don't consume this line
			break
		}

		// Write all of this line, except the final CRLF
		s := *bp.mr.bufferedLine
		if strings.HasSuffix(s, "\r\n") {
			bp.mr.consumeLine()
			if !bp.mr.ensureBufferedLine() {
				return 0, io.ErrUnexpectedEOF
			}
			if bp.mr.bufferedLineIsBoundary() {
				// The final \r\n isn't ours.  It logically belongs
				// to the boundary line which follows.
				bp.buffer.WriteString(s[0 : len(s)-2])
			} else {
				bp.buffer.WriteString(s)
			}
			break
		}
		if strings.HasSuffix(s, "\n") {
			bp.buffer.WriteString(s)
			bp.mr.consumeLine()
			continue
		}
		return 0, os.NewError("multipart parse error during Read; unexpected line: " + s)
	}
	return bp.buffer.Read(p)
}

func (bp *Part) Close() os.Error {
	io.Copy(&devNull, bp)
	return nil
}

type multiReader struct {
	boundary     string
	dashBoundary string // --boundary
	endLine      string // --boundary--

	bufferedLine *string

	bufReader   *bufio.Reader
	currentPart *Part
	partsRead   int
}

func (mr *multiReader) eof() bool {
	return mr.bufferedLine == nil &&
		!mr.readLine()
}

func (mr *multiReader) readLine() bool {
	line, err := mr.bufReader.ReadString('\n')
	if err != nil {
		// TODO: care about err being EOF or not?
		return false
	}
	mr.bufferedLine = &line
	return true
}

func (mr *multiReader) bufferedLineIsBoundary() bool {
	return strings.HasPrefix(*mr.bufferedLine, mr.dashBoundary)
}

func (mr *multiReader) ensureBufferedLine() bool {
	if mr.bufferedLine == nil {
		return mr.readLine()
	}
	return true
}

func (mr *multiReader) consumeLine() {
	mr.bufferedLine = nil
}

func (mr *multiReader) NextPart() (*Part, os.Error) {
	if mr.currentPart != nil {
		mr.currentPart.Close()
	}

	for {
		if mr.eof() {
			return nil, io.ErrUnexpectedEOF
		}

		if isBoundaryDelimiterLine(*mr.bufferedLine, mr.dashBoundary) {
			mr.consumeLine()
			mr.partsRead++
			bp, err := newPart(mr)
			if err != nil {
				return nil, err
			}
			mr.currentPart = bp
			return bp, nil
		}

		if hasPrefixThenNewline(*mr.bufferedLine, mr.endLine) {
			mr.consumeLine()
			// Expected EOF (no error)
			return nil, nil
		}

		if mr.partsRead == 0 {
			// skip line
			mr.consumeLine()
			continue
		}

		return nil, os.NewError("Unexpected line in Next().")
	}
	panic("unreachable")
}

func isBoundaryDelimiterLine(line, dashPrefix string) bool {
	// http://tools.ietf.org/html/rfc2046#section-5.1
	//   The boundary delimiter line is then defined as a line
	//   consisting entirely of two hyphen characters ("-",
	//   decimal value 45) followed by the boundary parameter
	//   value from the Content-Type header field, optional linear
	//   whitespace, and a terminating CRLF.
	if !strings.HasPrefix(line, dashPrefix) {
		return false
	}
	if strings.HasSuffix(line, "\r\n") {
		return onlyHorizontalWhitespace(line[len(dashPrefix) : len(line)-2])
	}
	// Violate the spec and also support newlines without the
	// carriage return...
	if strings.HasSuffix(line, "\n") {
		return onlyHorizontalWhitespace(line[len(dashPrefix) : len(line)-1])
	}
	return false
}

func onlyHorizontalWhitespace(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] != ' ' && s[i] != '\t' {
			return false
		}
	}
	return true
}

func hasPrefixThenNewline(s, prefix string) bool {
	return strings.HasPrefix(s, prefix) &&
		(len(s) == len(prefix)+1 && strings.HasSuffix(s, "\n") ||
			len(s) == len(prefix)+2 && strings.HasSuffix(s, "\r\n"))
}
