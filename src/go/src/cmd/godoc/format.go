// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// This file implements FormatSelections and FormatText.
// FormatText is used to HTML-format Go and non-Go source
// text with line numbers and highlighted sections. It is
// built on top of FormatSelections, a generic formatter
// for "selected" text.

package main

import (
	"fmt"
	"go/scanner"
	"go/token"
	"io"
	"regexp"
	"strconv"
	"template"
)


// ----------------------------------------------------------------------------
// Implementation of FormatSelections

// A Selection is a function returning offset pairs []int{a, b}
// describing consecutive non-overlapping text segments [a, b).
// If there are no more segments, a Selection must return nil.
//
// TODO It's more efficient to return a pair (a, b int) instead
//      of creating lots of slices. Need to determine how to
//      indicate the end of a Selection.
//
type Selection func() []int


// A LinkWriter writes some start or end "tag" to w for the text offset offs.
// It is called by FormatSelections at the start or end of each link segment.
//
type LinkWriter func(w io.Writer, offs int, start bool)


// A SegmentWriter formats a text according to selections and writes it to w.
// The selections parameter is a bit set indicating which selections provided
// to FormatSelections overlap with the text segment: If the n'th bit is set
// in selections, the n'th selection provided to FormatSelections is overlapping
// with the text.
//
type SegmentWriter func(w io.Writer, text []byte, selections int)


// FormatSelections takes a text and writes it to w using link and segment
// writers lw and sw as follows: lw is invoked for consecutive segment starts
// and ends as specified through the links selection, and sw is invoked for
// consecutive segments of text overlapped by the same selections as specified
// by selections. The link writer lw may be nil, in which case the links
// Selection is ignored.
//
func FormatSelections(w io.Writer, text []byte, lw LinkWriter, links Selection, sw SegmentWriter, selections ...Selection) {
	if lw != nil {
		selections = append(selections, links)
	}

	// compute the sequence of consecutive segment changes
	changes := newMerger(selections)

	// The i'th bit in bitset indicates that the text
	// at the current offset is covered by selections[i].
	bitset := 0
	lastOffs := 0

	// Text segments are written in a delayed fashion
	// such that consecutive segments belonging to the
	// same selection can be combined (peephole optimization).
	// last describes the last segment which has not yet been written.
	var last struct {
		begin, end int // valid if begin < end
		bitset     int
	}

	// flush writes the last delayed text segment
	flush := func() {
		if last.begin < last.end {
			sw(w, text[last.begin:last.end], last.bitset)
		}
		last.begin = last.end // invalidate last
	}

	// segment runs the segment [lastOffs, end) with the selection
	// indicated by bitset through the segment peephole optimizer.
	segment := func(end int) {
		if lastOffs < end { // ignore empty segments
			if last.end != lastOffs || last.bitset != bitset {
				// the last segment is not adjacent to or
				// differs from the new one
				flush()
				// start a new segment
				last.begin = lastOffs
			}
			last.end = end
			last.bitset = bitset
		}
	}

	for {
		// get the next segment change
		index, offs, start := changes.next()
		if index < 0 || offs > len(text) {
			// no more segment changes or the next change
			// is past the end of the text - we're done
			break
		}
		// determine the kind of segment change
		if index == len(selections)-1 {
			// we have a link segment change:
			// format the previous selection segment, write the
			// link tag and start a new selection segment
			segment(offs)
			flush()
			lastOffs = offs
			lw(w, offs, start)
		} else {
			// we have a selection change:
			// format the previous selection segment, determine
			// the new selection bitset and start a new segment 
			segment(offs)
			lastOffs = offs
			mask := 1 << uint(index)
			if start {
				bitset |= mask
			} else {
				bitset &^= mask
			}
		}
	}
	segment(len(text))
	flush()
}


// A merger merges a slice of Selections and produces a sequence of
// consecutive segment change events through repeated next() calls.
//
type merger struct {
	selections []Selection
	segments   [][]int // segments[i] is the next segment of selections[i]
}


const infinity int = 2e9

func newMerger(selections []Selection) *merger {
	segments := make([][]int, len(selections))
	for i, sel := range selections {
		segments[i] = []int{infinity, infinity}
		if sel != nil {
			if seg := sel(); seg != nil {
				segments[i] = seg
			}
		}
	}
	return &merger{selections, segments}
}


// next returns the next segment change: index specifies the Selection
// to which the segment belongs, offs is the segment start or end offset
// as determined by the start value. If there are no more segment changes,
// next returns an index value < 0.
//
func (m *merger) next() (index, offs int, start bool) {
	// find the next smallest offset where a segment starts or ends
	offs = infinity
	index = -1
	for i, seg := range m.segments {
		switch {
		case seg[0] < offs:
			offs = seg[0]
			index = i
			start = true
		case seg[1] < offs:
			offs = seg[1]
			index = i
			start = false
		}
	}
	if index < 0 {
		// no offset found => all selections merged
		return
	}
	// offset found - it's either the start or end offset but
	// either way it is ok to consume the start offset: set it
	// to infinity so it won't be considered in the following
	// next call
	m.segments[index][0] = infinity
	if start {
		return
	}
	// end offset found - consume it
	m.segments[index][1] = infinity
	// advance to the next segment for that selection
	seg := m.selections[index]()
	if seg == nil {
		return
	}
	m.segments[index] = seg
	return
}


// ----------------------------------------------------------------------------
// Implementation of FormatText

// lineSelection returns the line segments for text as a Selection.
func lineSelection(text []byte) Selection {
	i, j := 0, 0
	return func() (seg []int) {
		// find next newline, if any
		for j < len(text) {
			j++
			if text[j-1] == '\n' {
				break
			}
		}
		if i < j {
			// text[i:j] constitutes a line
			seg = []int{i, j}
			i = j
		}
		return
	}
}


// commentSelection returns the sequence of consecutive comments
// in the Go src text as a Selection.
//
func commentSelection(src []byte) Selection {
	var s scanner.Scanner
	fset := token.NewFileSet()
	file := fset.AddFile("", fset.Base(), len(src))
	s.Init(file, src, nil, scanner.ScanComments+scanner.InsertSemis)
	return func() (seg []int) {
		for {
			pos, tok, lit := s.Scan()
			if tok == token.EOF {
				break
			}
			offs := file.Offset(pos)
			if tok == token.COMMENT {
				seg = []int{offs, offs + len(lit)}
				break
			}
		}
		return
	}
}


// makeSelection is a helper function to make a Selection from a slice of pairs.
func makeSelection(matches [][]int) Selection {
	return func() (seg []int) {
		if len(matches) > 0 {
			seg = matches[0]
			matches = matches[1:]
		}
		return
	}
}


// regexpSelection computes the Selection for the regular expression expr in text.
func regexpSelection(text []byte, expr string) Selection {
	var matches [][]int
	if rx, err := regexp.Compile(expr); err == nil {
		matches = rx.FindAllIndex(text, -1)
	}
	return makeSelection(matches)
}


var selRx = regexp.MustCompile(`^([0-9]+):([0-9]+)`)

// rangeSelection computes the Selection for a text range described
// by the argument str; the range description must match the selRx
// regular expression.
//
func rangeSelection(str string) Selection {
	m := selRx.FindStringSubmatch(str)
	if len(m) >= 2 {
		from, _ := strconv.Atoi(m[1])
		to, _ := strconv.Atoi(m[2])
		if from < to {
			return makeSelection([][]int{[]int{from, to}})
		}
	}
	return nil
}


// Span tags for all the possible selection combinations that may
// be generated by FormatText. Selections are indicated by a bitset,
// and the value of the bitset specifies the tag to be used.
//
// bit 0: comments
// bit 1: highlights
// bit 2: selections
//
var startTags = [][]byte{
	/* 000 */ []byte(``),
	/* 001 */ []byte(`<span class ="comment">`),
	/* 010 */ []byte(`<span class="highlight">`),
	/* 011 */ []byte(`<span class="highlight-comment">`),
	/* 100 */ []byte(`<span class="selection">`),
	/* 101 */ []byte(`<span class="selection-comment">`),
	/* 110 */ []byte(`<span class="selection-highlight">`),
	/* 111 */ []byte(`<span class="selection-highlight-comment">`),
}

var endTag = []byte(`</span>`)


func selectionTag(w io.Writer, text []byte, selections int) {
	if selections < len(startTags) {
		if tag := startTags[selections]; len(tag) > 0 {
			w.Write(tag)
			template.HTMLEscape(w, text)
			w.Write(endTag)
			return
		}
	}
	template.HTMLEscape(w, text)
}


// FormatText HTML-escapes text and writes it to w.
// Consecutive text segments are wrapped in HTML spans (with tags as
// defined by startTags and endTag) as follows:
//
//	- if line >= 0, line number (ln) spans are inserted before each line,
//	  starting with the value of line
//	- if the text is Go source, comments get the "comment" span class
//	- each occurrence of the regular expression pattern gets the "highlight"
//	  span class
//	- text segments covered by selection get the "selection" span class
//
// Comments, highlights, and selections may overlap arbitrarily; the respective
// HTML span classes are specified in the startTags variable.
//
func FormatText(w io.Writer, text []byte, line int, goSource bool, pattern string, selection Selection) {
	var comments, highlights Selection
	if goSource {
		comments = commentSelection(text)
	}
	if pattern != "" {
		highlights = regexpSelection(text, pattern)
	}
	if line >= 0 || comments != nil || highlights != nil || selection != nil {
		var lineTag LinkWriter
		if line >= 0 {
			lineTag = func(w io.Writer, _ int, start bool) {
				if start {
					fmt.Fprintf(w, "<a id=\"L%d\"></a><span class=\"ln\">%6d</span>\t", line, line)
					line++
				}
			}
		}
		FormatSelections(w, text, lineTag, lineSelection(text), selectionTag, comments, highlights, selection)
	} else {
		template.HTMLEscape(w, text)
	}
}
