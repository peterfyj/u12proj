// Copyright 2009 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package pe implements access to PE (Microsoft Windows Portable Executable) files.
package pe

import (
	"debug/dwarf"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strconv"
)

// A File represents an open PE file.
type File struct {
	FileHeader
	Sections []*Section

	closer io.Closer
}

type SectionHeader struct {
	Name                 string
	VirtualSize          uint32
	VirtualAddress       uint32
	Size                 uint32
	Offset               uint32
	PointerToRelocations uint32
	PointerToLineNumbers uint32
	NumberOfRelocations  uint16
	NumberOfLineNumbers  uint16
	Characteristics      uint32
}


type Section struct {
	SectionHeader

	// Embed ReaderAt for ReadAt method.
	// Do not embed SectionReader directly
	// to avoid having Read and Seek.
	// If a client wants Read and Seek it must use
	// Open() to avoid fighting over the seek offset
	// with other clients.
	io.ReaderAt
	sr *io.SectionReader
}

type ImportDirectory struct {
	OriginalFirstThunk uint32
	TimeDateStamp      uint32
	ForwarderChain     uint32
	Name               uint32
	FirstThunk         uint32

	dll string
}

// Data reads and returns the contents of the PE section.
func (s *Section) Data() ([]byte, os.Error) {
	dat := make([]byte, s.sr.Size())
	n, err := s.sr.ReadAt(dat, 0)
	return dat[0:n], err
}

// Open returns a new ReadSeeker reading the PE section.
func (s *Section) Open() io.ReadSeeker { return io.NewSectionReader(s.sr, 0, 1<<63-1) }


type FormatError struct {
	off int64
	msg string
	val interface{}
}

func (e *FormatError) String() string {
	msg := e.msg
	if e.val != nil {
		msg += fmt.Sprintf(" '%v'", e.val)
	}
	msg += fmt.Sprintf(" in record at byte %#x", e.off)
	return msg
}

// Open opens the named file using os.Open and prepares it for use as a PE binary.
func Open(name string) (*File, os.Error) {
	f, err := os.Open(name, os.O_RDONLY, 0)
	if err != nil {
		return nil, err
	}
	ff, err := NewFile(f)
	if err != nil {
		f.Close()
		return nil, err
	}
	ff.closer = f
	return ff, nil
}

// Close closes the File.
// If the File was created using NewFile directly instead of Open,
// Close has no effect.
func (f *File) Close() os.Error {
	var err os.Error
	if f.closer != nil {
		err = f.closer.Close()
		f.closer = nil
	}
	return err
}

// NewFile creates a new File for acecssing a PE binary in an underlying reader.
func NewFile(r io.ReaderAt) (*File, os.Error) {
	f := new(File)
	sr := io.NewSectionReader(r, 0, 1<<63-1)

	var dosheader [96]byte
	if _, err := r.ReadAt(dosheader[0:], 0); err != nil {
		return nil, err
	}
	var base int64
	if dosheader[0] == 'M' && dosheader[1] == 'Z' {
		var sign [4]byte
		r.ReadAt(sign[0:], int64(dosheader[0x3c]))
		if !(sign[0] == 'P' && sign[1] == 'E' && sign[2] == 0 && sign[3] == 0) {
			return nil, os.NewError("Invalid PE File Format.")
		}
		base = int64(dosheader[0x3c]) + 4
	} else {
		base = int64(0)
	}
	sr.Seek(base, 0)
	if err := binary.Read(sr, binary.LittleEndian, &f.FileHeader); err != nil {
		return nil, err
	}
	if f.FileHeader.Machine != IMAGE_FILE_MACHINE_UNKNOWN && f.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 && f.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 {
		return nil, os.NewError("Invalid PE File Format.")
	}
	// get symbol string table
	sr.Seek(int64(f.FileHeader.PointerToSymbolTable+18*f.FileHeader.NumberOfSymbols), 0)
	var l uint32
	if err := binary.Read(sr, binary.LittleEndian, &l); err != nil {
		return nil, err
	}
	ss := make([]byte, l)
	if _, err := r.ReadAt(ss, int64(f.FileHeader.PointerToSymbolTable+18*f.FileHeader.NumberOfSymbols)); err != nil {
		return nil, err
	}
	sr.Seek(base, 0)
	binary.Read(sr, binary.LittleEndian, &f.FileHeader)
	sr.Seek(int64(f.FileHeader.SizeOfOptionalHeader), 1) //Skip OptionalHeader
	f.Sections = make([]*Section, f.FileHeader.NumberOfSections)
	for i := 0; i < int(f.FileHeader.NumberOfSections); i++ {
		sh := new(SectionHeader32)
		if err := binary.Read(sr, binary.LittleEndian, sh); err != nil {
			return nil, err
		}
		var name string
		if sh.Name[0] == '\x2F' {
			si, _ := strconv.Atoi(cstring(sh.Name[1:]))
			name, _ = getString(ss, si)
		} else {
			name = cstring(sh.Name[0:])
		}
		s := new(Section)
		s.SectionHeader = SectionHeader{
			Name:                 name,
			VirtualSize:          uint32(sh.VirtualSize),
			VirtualAddress:       uint32(sh.VirtualAddress),
			Size:                 uint32(sh.SizeOfRawData),
			Offset:               uint32(sh.PointerToRawData),
			PointerToRelocations: uint32(sh.PointerToRelocations),
			PointerToLineNumbers: uint32(sh.PointerToLineNumbers),
			NumberOfRelocations:  uint16(sh.NumberOfRelocations),
			NumberOfLineNumbers:  uint16(sh.NumberOfLineNumbers),
			Characteristics:      uint32(sh.Characteristics),
		}
		s.sr = io.NewSectionReader(r, int64(s.SectionHeader.Offset), int64(s.SectionHeader.Size))
		s.ReaderAt = s.sr
		f.Sections[i] = s
	}
	return f, nil
}

func cstring(b []byte) string {
	var i int
	for i = 0; i < len(b) && b[i] != 0; i++ {
	}
	return string(b[0:i])
}

// getString extracts a string from symbol string table.
func getString(section []byte, start int) (string, bool) {
	if start < 0 || start >= len(section) {
		return "", false
	}

	for end := start; end < len(section); end++ {
		if section[end] == 0 {
			return string(section[start:end]), true
		}
	}
	return "", false
}

// Section returns the first section with the given name, or nil if no such
// section exists.
func (f *File) Section(name string) *Section {
	for _, s := range f.Sections {
		if s.Name == name {
			return s
		}
	}
	return nil
}

func (f *File) DWARF() (*dwarf.Data, os.Error) {
	// There are many other DWARF sections, but these
	// are the required ones, and the debug/dwarf package
	// does not use the others, so don't bother loading them.
	var names = [...]string{"abbrev", "info", "str"}
	var dat [len(names)][]byte
	for i, name := range names {
		name = ".debug_" + name
		s := f.Section(name)
		if s == nil {
			continue
		}
		b, err := s.Data()
		if err != nil && uint32(len(b)) < s.Size {
			return nil, err
		}
		dat[i] = b
	}

	abbrev, info, str := dat[0], dat[1], dat[2]
	return dwarf.New(abbrev, nil, nil, info, nil, nil, nil, str)
}

// ImportedSymbols returns the names of all symbols
// referred to by the binary f that are expected to be
// satisfied by other libraries at dynamic load time.
// It does not return weak symbols.
func (f *File) ImportedSymbols() ([]string, os.Error) {
	ds := f.Section(".idata")
	if ds == nil {
		// not dynamic, so no libraries
		return nil, nil
	}
	d, err := ds.Data()
	if err != nil {
		return nil, err
	}
	var ida []ImportDirectory
	for len(d) > 0 {
		var dt ImportDirectory
		dt.OriginalFirstThunk = binary.LittleEndian.Uint32(d[0:4])
		dt.Name = binary.LittleEndian.Uint32(d[12:16])
		dt.FirstThunk = binary.LittleEndian.Uint32(d[16:20])
		d = d[20:]
		if dt.OriginalFirstThunk == 0 {
			break
		}
		ida = append(ida, dt)
	}
	names, _ := ds.Data()
	var all []string
	for _, dt := range ida {
		dt.dll, _ = getString(names, int(dt.Name-ds.VirtualAddress))
		d, _ = ds.Data()
		// seek to OriginalFirstThunk
		d = d[dt.OriginalFirstThunk-ds.VirtualAddress:]
		for len(d) > 0 {
			va := binary.LittleEndian.Uint32(d[0:4])
			d = d[4:]
			if va == 0 {
				break
			}
			if va&0x80000000 > 0 { // is Ordinal
				// TODO add dynimport ordinal support.
				//ord := va&0x0000FFFF
			} else {
				fn, _ := getString(names, int(va-ds.VirtualAddress+2))
				all = append(all, fn+":"+dt.dll)
			}
		}
	}

	return all, nil
}

// ImportedLibraries returns the names of all libraries
// referred to by the binary f that are expected to be
// linked with the binary at dynamic link time.
func (f *File) ImportedLibraries() ([]string, os.Error) {
	// TODO
	// cgo -dynimport don't use this for windows PE, so just return.
	return nil, nil
}
