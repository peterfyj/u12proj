// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package os

import (
	"runtime"
	"syscall"
)

// Process stores the information about a process created by StartProcess.
type Process struct {
	Pid    int
	handle int
}

func newProcess(pid, handle int) *Process {
	p := &Process{pid, handle}
	runtime.SetFinalizer(p, (*Process).Release)
	return p
}

// StartProcess starts a new process with the program, arguments,
// and environment specified by name, argv, and envv. The fd array specifies the
// file descriptors to be set up in the new process: fd[0] will be Unix file
// descriptor 0 (standard input), fd[1] descriptor 1, and so on.  A nil entry
// will cause the child to have no open file descriptor with that index.
// If dir is not empty, the child chdirs into the directory before execing the program.
func StartProcess(name string, argv []string, envv []string, dir string, fd []*File) (p *Process, err Error) {
	if envv == nil {
		envv = Environ()
	}
	// Create array of integer (system) fds.
	intfd := make([]int, len(fd))
	for i, f := range fd {
		if f == nil {
			intfd[i] = -1
		} else {
			intfd[i] = f.Fd()
		}
	}

	pid, h, e := syscall.StartProcess(name, argv, envv, dir, intfd)
	if e != 0 {
		return nil, &PathError{"fork/exec", name, Errno(e)}
	}
	return newProcess(pid, h), nil
}

// Exec replaces the current process with an execution of the
// named binary, with arguments argv and environment envv.
// If successful, Exec never returns.  If it fails, it returns an Error.
// StartProcess is almost always a better way to execute a program.
func Exec(name string, argv []string, envv []string) Error {
	if envv == nil {
		envv = Environ()
	}
	e := syscall.Exec(name, argv, envv)
	if e != 0 {
		return &PathError{"exec", name, Errno(e)}
	}
	return nil
}

// TODO(rsc): Should os implement its own syscall.WaitStatus
// wrapper with the methods, or is exposing the underlying one enough?
//
// TODO(rsc): Certainly need to have Rusage struct,
// since syscall one might have different field types across
// different OS.

// Waitmsg stores the information about an exited process as reported by Wait.
type Waitmsg struct {
	Pid                int             // The process's id.
	syscall.WaitStatus                 // System-dependent status info.
	Rusage             *syscall.Rusage // System-dependent resource usage info.
}

// Wait waits for process pid to exit or stop, and then returns a
// Waitmsg describing its status and an Error, if any. The options
// (WNOHANG etc.) affect the behavior of the Wait call.
// Wait is equivalent to calling FindProcess and then Wait
// and Release on the result.
func Wait(pid int, options int) (w *Waitmsg, err Error) {
	p, e := FindProcess(pid)
	if e != nil {
		return nil, e
	}
	defer p.Release()
	return p.Wait(options)
}

// Convert i to decimal string.
func itod(i int) string {
	if i == 0 {
		return "0"
	}

	u := uint64(i)
	if i < 0 {
		u = -u
	}

	// Assemble decimal in reverse order.
	var b [32]byte
	bp := len(b)
	for ; u > 0; u /= 10 {
		bp--
		b[bp] = byte(u%10) + '0'
	}

	if i < 0 {
		bp--
		b[bp] = '-'
	}

	return string(b[bp:])
}

func (w Waitmsg) String() string {
	// TODO(austin) Use signal names when possible?
	res := ""
	switch {
	case w.Exited():
		res = "exit status " + itod(w.ExitStatus())
	case w.Signaled():
		res = "signal " + itod(w.Signal())
	case w.Stopped():
		res = "stop signal " + itod(w.StopSignal())
		if w.StopSignal() == syscall.SIGTRAP && w.TrapCause() != 0 {
			res += " (trap " + itod(w.TrapCause()) + ")"
		}
	case w.Continued():
		res = "continued"
	}
	if w.CoreDump() {
		res += " (core dumped)"
	}
	return res
}

// Getpid returns the process id of the caller.
func Getpid() int { return syscall.Getpid() }

// Getppid returns the process id of the caller's parent.
func Getppid() int { return syscall.Getppid() }
