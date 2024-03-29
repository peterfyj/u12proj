// Copyright 2009 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Cgo; see gmp.go for an overview.

// TODO(rsc):
//	Emit correct line number annotations.
//	Make 6g understand the annotations.

package main

import (
	"crypto/md5"
	"flag"
	"fmt"
	"go/ast"
	"go/token"
	"io"
	"os"
	"reflect"
	"strings"
	"runtime"
)

// A Package collects information about the package we're going to write.
type Package struct {
	PackageName string // name of package
	PackagePath string
	PtrSize     int64
	GccOptions  []string
	CgoFlags    map[string]string // #cgo flags (CFLAGS, LDFLAGS)
	Written     map[string]bool
	Name        map[string]*Name    // accumulated Name from Files
	Typedef     map[string]ast.Expr // accumulated Typedef from Files
	ExpFunc     []*ExpFunc          // accumulated ExpFunc from Files
	Decl        []ast.Decl
	GoFiles     []string // list of Go files
	GccFiles    []string // list of gcc output files
}

// A File collects information about a single Go input file.
type File struct {
	AST      *ast.File           // parsed AST
	Package  string              // Package name
	Preamble string              // C preamble (doc comment on import "C")
	Ref      []*Ref              // all references to C.xxx in AST
	ExpFunc  []*ExpFunc          // exported functions for this file
	Name     map[string]*Name    // map from Go name to Name
	Typedef  map[string]ast.Expr // translations of all necessary types from C
}

// A Ref refers to an expression of the form C.xxx in the AST.
type Ref struct {
	Name    *Name
	Expr    *ast.Expr
	Context string // "type", "expr", "call", or "call2"
}

func (r *Ref) Pos() token.Pos {
	return (*r.Expr).Pos()
}

// A Name collects information about C.xxx.
type Name struct {
	Go       string // name used in Go referring to package C
	Mangle   string // name used in generated Go
	C        string // name used in C
	Define   string // #define expansion
	Kind     string // "const", "type", "var", "func", "not-type"
	Type     *Type  // the type of xxx
	FuncType *FuncType
	AddError bool
	Const    string // constant definition
}

// A ExpFunc is an exported function, callable from C.
// Such functions are identified in the Go input file
// by doc comments containing the line //export ExpName
type ExpFunc struct {
	Func    *ast.FuncDecl
	ExpName string // name to use from C
}

// A TypeRepr contains the string representation of a type.
type TypeRepr struct {
	Repr       string
	FormatArgs []interface{}
}

// A Type collects information about a type in both the C and Go worlds.
type Type struct {
	Size       int64
	Align      int64
	C          *TypeRepr
	Go         ast.Expr
	EnumValues map[string]int64
}

// A FuncType collects information about a function type in both the C and Go worlds.
type FuncType struct {
	Params []*Type
	Result *Type
	Go     *ast.FuncType
}

func usage() {
	fmt.Fprint(os.Stderr, "usage: cgo -- [compiler options] file.go ...\n")
	flag.PrintDefaults()
	os.Exit(2)
}

var ptrSizeMap = map[string]int64{
	"386":   4,
	"amd64": 8,
	"arm":   4,
}

var cPrefix string

var fset = token.NewFileSet()

var dynobj = flag.String("dynimport", "", "if non-empty, print dynamic import data for that file")

func main() {
	flag.Usage = usage
	flag.Parse()

	if *dynobj != "" {
		// cgo -dynimport is essentially a separate helper command
		// built into the cgo binary.  It scans a gcc-produced executable
		// and dumps information about the imported symbols and the
		// imported libraries.  The Make.pkg rules for cgo prepare an
		// appropriate executable and then use its import information
		// instead of needing to make the linkers duplicate all the
		// specialized knowledge gcc has about where to look for imported
		// symbols and which ones to use.
		syms, imports := dynimport(*dynobj)
		if runtime.GOOS == "windows" {
			for _, sym := range syms {
				ss := strings.Split(sym, ":", -1)
				fmt.Printf("#pragma dynimport %s %s %q\n", ss[0], ss[0], strings.ToLower(ss[1]))
			}
			return
		}
		for _, sym := range syms {
			fmt.Printf("#pragma dynimport %s %s %q\n", sym, sym, "")
		}
		for _, p := range imports {
			fmt.Printf("#pragma dynimport %s %s %q\n", "_", "_", p)
		}
		return
	}

	args := flag.Args()
	if len(args) < 1 {
		usage()
	}

	// Find first arg that looks like a go file and assume everything before
	// that are options to pass to gcc.
	var i int
	for i = len(args); i > 0; i-- {
		if !strings.HasSuffix(args[i-1], ".go") {
			break
		}
	}
	if i == len(args) {
		usage()
	}

	// Copy it to a new slice so it can grow.
	gccOptions := make([]string, i)
	copy(gccOptions, args[0:i])

	goFiles := args[i:]

	arch := os.Getenv("GOARCH")
	if arch == "" {
		fatal("$GOARCH is not set")
	}
	ptrSize := ptrSizeMap[arch]
	if ptrSize == 0 {
		fatal("unknown $GOARCH %q", arch)
	}

	// Clear locale variables so gcc emits English errors [sic].
	os.Setenv("LANG", "en_US.UTF-8")
	os.Setenv("LC_ALL", "C")
	os.Setenv("LC_CTYPE", "C")

	p := &Package{
		PtrSize:    ptrSize,
		GccOptions: gccOptions,
		CgoFlags:   make(map[string]string),
		Written:    make(map[string]bool),
	}

	// Need a unique prefix for the global C symbols that
	// we use to coordinate between gcc and ourselves.
	// We already put _cgo_ at the beginning, so the main
	// concern is other cgo wrappers for the same functions.
	// Use the beginning of the md5 of the input to disambiguate.
	h := md5.New()
	for _, input := range goFiles {
		f, err := os.Open(input, os.O_RDONLY, 0)
		if err != nil {
			fatal("%s", err)
		}
		io.Copy(h, f)
		f.Close()
	}
	cPrefix = fmt.Sprintf("_%x", h.Sum()[0:6])

	fs := make([]*File, len(goFiles))
	for i, input := range goFiles {
		// Parse flags for all files before translating due to CFLAGS.
		f := new(File)
		f.ReadGo(input)
		p.ParseFlags(f, input)
		fs[i] = f
	}

	// make sure that _obj directory exists, so that we can write
	// all the output files there.
	os.Mkdir("_obj", 0777)

	for i, input := range goFiles {
		f := fs[i]
		p.Translate(f)
		for _, cref := range f.Ref {
			switch cref.Context {
			case "call", "call2":
				if cref.Name.Kind != "type" {
					break
				}
				*cref.Expr = cref.Name.Type.Go
			}
		}
		if nerrors > 0 {
			os.Exit(2)
		}
		pkg := f.Package
		if dir := os.Getenv("CGOPKGPATH"); dir != "" {
			pkg = dir + "/" + pkg
		}
		p.PackagePath = pkg
		p.writeOutput(f, input)

		p.Record(f)
	}

	p.writeDefs()
	if nerrors > 0 {
		os.Exit(2)
	}
}

// Record what needs to be recorded about f.
func (p *Package) Record(f *File) {
	if p.PackageName == "" {
		p.PackageName = f.Package
	} else if p.PackageName != f.Package {
		error(token.NoPos, "inconsistent package names: %s, %s", p.PackageName, f.Package)
	}

	if p.Name == nil {
		p.Name = f.Name
	} else {
		for k, v := range f.Name {
			if p.Name[k] == nil {
				p.Name[k] = v
			} else if !reflect.DeepEqual(p.Name[k], v) {
				error(token.NoPos, "inconsistent definitions for C.%s", k)
			}
		}
	}

	p.ExpFunc = append(p.ExpFunc, f.ExpFunc...)
	p.Decl = append(p.Decl, f.AST.Decls...)
}
