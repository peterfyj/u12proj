# Copyright 2009 The Go Authors. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include ../../Make.inc

TARG=syscall
GOFILES=\
	str.go\
	syscall.go\
	syscall_$(GOARCH).go\
	syscall_$(GOOS).go\
	syscall_$(GOOS)_$(GOARCH).go\
	zerrors_$(GOOS)_$(GOARCH).go\
	zsyscall_$(GOOS)_$(GOARCH).go\
	zsysnum_$(GOOS)_$(GOARCH).go\
	ztypes_$(GOOS)_$(GOARCH).go\

GOFILES_freebsd=\
	syscall_bsd.go\
	syscall_unix.go\
	exec_unix.go\

GOFILES_darwin=\
	syscall_bsd.go\
	syscall_unix.go\
	exec_unix.go\

GOFILES_linux=\
	syscall_unix.go\
	exec_unix.go\

GOFILES_ucore=\
	syscall_unix.go\
	exec_unix.go\

GOFILES_windows=\
	exec_windows.go

OFILES=\
	asm_$(GOOS)_$(GOARCH).$O\

GOFILES+=$(GOFILES_$(GOOS))

include ../../Make.pkg
