// Copyright 2009 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Windows system calls.

package syscall

import (
	"unsafe"
	"utf16"
)

const OS = "windows"

/*

small demo to detect version of windows you are running:

package main

import (
	"syscall"
)

func abort(funcname string, err int) {
	panic(funcname + " failed: " + syscall.Errstr(err))
}

func print_version(v uint32) {
	major := byte(v)
	minor := uint8(v >> 8)
	build := uint16(v >> 16)
	print("windows version ", major, ".", minor, " (Build ", build, ")\n")
}

func main() {
	h, err := syscall.LoadLibrary("kernel32.dll")
	if err != 0 {
		abort("LoadLibrary", err)
	}
	defer syscall.FreeLibrary(h)
	proc, err := syscall.GetProcAddress(h, "GetVersion")
	if err != 0 {
		abort("GetProcAddress", err)
	}
	r, _, _ := syscall.Syscall(uintptr(proc), 0, 0, 0, 0)
	print_version(uint32(r))
}

*/

// StringToUTF16 returns the UTF-16 encoding of the UTF-8 string s,
// with a terminating NUL added.
func StringToUTF16(s string) []uint16 { return utf16.Encode([]int(s + "\x00")) }

// UTF16ToString returns the UTF-8 encoding of the UTF-16 sequence s,
// with a terminating NUL removed.
func UTF16ToString(s []uint16) string {
	for i, v := range s {
		if v == 0 {
			s = s[0:i]
			break
		}
	}
	return string(utf16.Decode(s))
}

// StringToUTF16Ptr returns pointer to the UTF-16 encoding of
// the UTF-8 string s, with a terminating NUL added.
func StringToUTF16Ptr(s string) *uint16 { return &StringToUTF16(s)[0] }

// dll helpers

// implemented in ../runtime/windows/syscall.cgo
func Syscall(trap, nargs, a1, a2, a3 uintptr) (r1, r2, err uintptr)
func Syscall6(trap, nargs, a1, a2, a3, a4, a5, a6 uintptr) (r1, r2, err uintptr)
func Syscall9(trap, nargs, a1, a2, a3, a4, a5, a6, a7, a8, a9 uintptr) (r1, r2, err uintptr)
func Syscall12(trap, nargs, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12 uintptr) (r1, r2, err uintptr)
func loadlibraryex(filename uintptr) (handle uint32)
func getprocaddress(handle uint32, procname uintptr) (proc uintptr)

func loadDll(fname string) uint32 {
	m := loadlibraryex(uintptr(unsafe.Pointer(StringBytePtr(fname))))
	if m == 0 {
		panic("syscall: could not LoadLibraryEx " + fname)
	}
	return m
}

func getSysProcAddr(m uint32, pname string) uintptr {
	p := getprocaddress(m, uintptr(unsafe.Pointer(StringBytePtr(pname))))
	if p == 0 {
		panic("syscall: could not GetProcAddress for " + pname)
	}
	return p
}

// Converts a Go function to a function pointer conforming
// to the stdcall calling convention.  This is useful when
// interoperating with Windows code requiring callbacks.
// Implemented in ../runtime/windows/syscall.cgo
func NewCallback(fn interface{}) uintptr

// windows api calls

//sys	GetLastError() (lasterrno int)
//sys	LoadLibrary(libname string) (handle uint32, errno int) = LoadLibraryW
//sys	FreeLibrary(handle uint32) (errno int)
//sys	GetProcAddress(module uint32, procname string) (proc uint32, errno int)
//sys	GetVersion() (ver uint32, errno int)
//sys	FormatMessage(flags uint32, msgsrc uint32, msgid uint32, langid uint32, buf []uint16, args *byte) (n uint32, errno int) = FormatMessageW
//sys	ExitProcess(exitcode uint32)
//sys	CreateFile(name *uint16, access uint32, mode uint32, sa *SecurityAttributes, createmode uint32, attrs uint32, templatefile int32) (handle int32, errno int) [failretval==-1] = CreateFileW
//sys	ReadFile(handle int32, buf []byte, done *uint32, overlapped *Overlapped) (errno int)
//sys	WriteFile(handle int32, buf []byte, done *uint32, overlapped *Overlapped) (errno int)
//sys	SetFilePointer(handle int32, lowoffset int32, highoffsetptr *int32, whence uint32) (newlowoffset uint32, errno int) [failretval==0xffffffff]
//sys	CloseHandle(handle int32) (errno int)
//sys	GetStdHandle(stdhandle int32) (handle int32, errno int) [failretval==-1]
//sys	FindFirstFile(name *uint16, data *Win32finddata) (handle int32, errno int) [failretval==-1] = FindFirstFileW
//sys	FindNextFile(handle int32, data *Win32finddata) (errno int) = FindNextFileW
//sys	FindClose(handle int32) (errno int)
//sys	GetFileInformationByHandle(handle int32, data *ByHandleFileInformation) (errno int)
//sys	GetCurrentDirectory(buflen uint32, buf *uint16) (n uint32, errno int) = GetCurrentDirectoryW
//sys	SetCurrentDirectory(path *uint16) (errno int) = SetCurrentDirectoryW
//sys	CreateDirectory(path *uint16, sa *SecurityAttributes) (errno int) = CreateDirectoryW
//sys	RemoveDirectory(path *uint16) (errno int) = RemoveDirectoryW
//sys	DeleteFile(path *uint16) (errno int) = DeleteFileW
//sys	MoveFile(from *uint16, to *uint16) (errno int) = MoveFileW
//sys	GetComputerName(buf *uint16, n *uint32) (errno int) = GetComputerNameW
//sys	SetEndOfFile(handle int32) (errno int)
//sys	GetSystemTimeAsFileTime(time *Filetime)
//sys	sleep(msec uint32) = Sleep
//sys	GetTimeZoneInformation(tzi *Timezoneinformation) (rc uint32, errno int) [failretval==0xffffffff]
//sys	CreateIoCompletionPort(filehandle int32, cphandle int32, key uint32, threadcnt uint32) (handle int32, errno int)
//sys	GetQueuedCompletionStatus(cphandle int32, qty *uint32, key *uint32, overlapped **Overlapped, timeout uint32) (errno int)
//sys	CancelIo(s uint32) (errno int)
//sys	CreateProcess(appName *int16, commandLine *uint16, procSecurity *int16, threadSecurity *int16, inheritHandles bool, creationFlags uint32, env *uint16, currentDir *uint16, startupInfo *StartupInfo, outProcInfo *ProcessInformation) (errno int) = CreateProcessW
//sys	OpenProcess(da uint32, inheritHandle bool, pid uint32) (handle uint32, errno int)
//sys	GetExitCodeProcess(handle uint32, exitcode *uint32) (errno int)
//sys	GetStartupInfo(startupInfo *StartupInfo) (errno int) = GetStartupInfoW
//sys	GetCurrentProcess() (pseudoHandle int32, errno int)
//sys	DuplicateHandle(hSourceProcessHandle int32, hSourceHandle int32, hTargetProcessHandle int32, lpTargetHandle *int32, dwDesiredAccess uint32, bInheritHandle bool, dwOptions uint32) (errno int)
//sys	WaitForSingleObject(handle int32, waitMilliseconds uint32) (event uint32, errno int) [failretval==0xffffffff]
//sys	GetTempPath(buflen uint32, buf *uint16) (n uint32, errno int) = GetTempPathW
//sys	CreatePipe(readhandle *uint32, writehandle *uint32, sa *SecurityAttributes, size uint32) (errno int)
//sys	GetFileType(filehandle uint32) (n uint32, errno int)
//sys	CryptAcquireContext(provhandle *uint32, container *uint16, provider *uint16, provtype uint32, flags uint32) (errno int) = advapi32.CryptAcquireContextW
//sys	CryptReleaseContext(provhandle uint32, flags uint32) (errno int) = advapi32.CryptReleaseContext
//sys	CryptGenRandom(provhandle uint32, buflen uint32, buf *byte) (errno int) = advapi32.CryptGenRandom
//sys	GetEnvironmentStrings() (envs *uint16, errno int) [failretval==nil] = kernel32.GetEnvironmentStringsW
//sys	FreeEnvironmentStrings(envs *uint16) (errno int) = kernel32.FreeEnvironmentStringsW
//sys	GetEnvironmentVariable(name *uint16, buffer *uint16, size uint32) (n uint32, errno int) = kernel32.GetEnvironmentVariableW
//sys	SetEnvironmentVariable(name *uint16, value *uint16) (errno int) = kernel32.SetEnvironmentVariableW
//sys	SetFileTime(handle int32, ctime *Filetime, atime *Filetime, wtime *Filetime) (errno int)
//sys	GetFileAttributes(name *uint16) (attrs uint32, errno int) [failretval==INVALID_FILE_ATTRIBUTES] = kernel32.GetFileAttributesW
//sys	SetFileAttributes(name *uint16, attrs uint32) (errno int) = kernel32.SetFileAttributesW
//sys	GetCommandLine() (cmd *uint16) = kernel32.GetCommandLineW
//sys	CommandLineToArgv(cmd *uint16, argc *int32) (argv *[8192]*[8192]uint16, errno int) [failretval==nil] = shell32.CommandLineToArgvW
//sys	LocalFree(hmem uint32) (handle uint32, errno int) [failretval!=0]
//sys	SetHandleInformation(handle int32, mask uint32, flags uint32) (errno int)
//sys	FlushFileBuffers(handle int32) (errno int)

// syscall interface implementation for other packages

func Errstr(errno int) string {
	// deal with special go errors
	e := errno - APPLICATION_ERROR
	if 0 <= e && e < len(errors) {
		return errors[e]
	}
	// ask windows for the remaining errors
	var flags uint32 = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_IGNORE_INSERTS
	b := make([]uint16, 300)
	n, err := FormatMessage(flags, 0, uint32(errno), 0, b, nil)
	if err != 0 {
		return "error " + str(errno) + " (FormatMessage failed with err=" + str(err) + ")"
	}
	// trim terminating \r and \n
	for ; n > 0 && (b[n-1] == '\n' || b[n-1] == '\r'); n-- {
	}
	return string(utf16.Decode(b[:n]))
}

func Exit(code int) { ExitProcess(uint32(code)) }

func makeInheritSa() *SecurityAttributes {
	var sa SecurityAttributes
	sa.Length = uint32(unsafe.Sizeof(sa))
	sa.InheritHandle = 1
	return &sa
}

func Open(path string, mode int, perm uint32) (fd int, errno int) {
	if len(path) == 0 {
		return -1, ERROR_FILE_NOT_FOUND
	}
	var access uint32
	switch mode & (O_RDONLY | O_WRONLY | O_RDWR) {
	case O_RDONLY:
		access = GENERIC_READ
	case O_WRONLY:
		access = GENERIC_WRITE
	case O_RDWR:
		access = GENERIC_READ | GENERIC_WRITE
	}
	if mode&O_CREAT != 0 {
		access |= GENERIC_WRITE
	}
	if mode&O_APPEND != 0 {
		access &^= GENERIC_WRITE
		access |= FILE_APPEND_DATA
	}
	sharemode := uint32(FILE_SHARE_READ | FILE_SHARE_WRITE)
	var sa *SecurityAttributes
	if mode&O_CLOEXEC == 0 {
		sa = makeInheritSa()
	}
	var createmode uint32
	switch {
	case mode&O_CREAT != 0:
		if mode&O_EXCL != 0 {
			createmode = CREATE_NEW
		} else {
			createmode = CREATE_ALWAYS
		}
	case mode&O_TRUNC != 0:
		createmode = TRUNCATE_EXISTING
	default:
		createmode = OPEN_EXISTING
	}
	h, e := CreateFile(StringToUTF16Ptr(path), access, sharemode, sa, createmode, FILE_ATTRIBUTE_NORMAL, 0)
	return int(h), int(e)
}

func Read(fd int, p []byte) (n int, errno int) {
	var done uint32
	e := ReadFile(int32(fd), p, &done, nil)
	if e != 0 {
		if e == ERROR_BROKEN_PIPE {
			// NOTE(brainman): work around ERROR_BROKEN_PIPE is returned on reading EOF from stdin
			return 0, 0
		}
		return 0, e
	}
	return int(done), 0
}

// TODO(brainman): ReadFile/WriteFile change file offset, therefore
// i use Seek here to preserve semantics of unix pread/pwrite,
// not sure if I should do that

func Pread(fd int, p []byte, offset int64) (n int, errno int) {
	curoffset, e := Seek(fd, 0, 1)
	if e != 0 {
		return 0, e
	}
	defer Seek(fd, curoffset, 0)
	var o Overlapped
	o.OffsetHigh = uint32(offset >> 32)
	o.Offset = uint32(offset)
	var done uint32
	e = ReadFile(int32(fd), p, &done, &o)
	if e != 0 {
		return 0, e
	}
	return int(done), 0
}

func Write(fd int, p []byte) (n int, errno int) {
	var done uint32
	e := WriteFile(int32(fd), p, &done, nil)
	if e != 0 {
		return 0, e
	}
	return int(done), 0
}

func Pwrite(fd int, p []byte, offset int64) (n int, errno int) {
	curoffset, e := Seek(fd, 0, 1)
	if e != 0 {
		return 0, e
	}
	defer Seek(fd, curoffset, 0)
	var o Overlapped
	o.OffsetHigh = uint32(offset >> 32)
	o.Offset = uint32(offset)
	var done uint32
	e = WriteFile(int32(fd), p, &done, &o)
	if e != 0 {
		return 0, e
	}
	return int(done), 0
}

func Seek(fd int, offset int64, whence int) (newoffset int64, errno int) {
	var w uint32
	switch whence {
	case 0:
		w = FILE_BEGIN
	case 1:
		w = FILE_CURRENT
	case 2:
		w = FILE_END
	}
	hi := int32(offset >> 32)
	lo := int32(offset)
	// use GetFileType to check pipe, pipe can't do seek
	ft, _ := GetFileType(uint32(fd))
	if ft == FILE_TYPE_PIPE {
		return 0, EPIPE
	}
	rlo, e := SetFilePointer(int32(fd), lo, &hi, w)
	if e != 0 {
		return 0, e
	}
	return int64(hi)<<32 + int64(rlo), 0
}

func Close(fd int) (errno int) {
	return CloseHandle(int32(fd))
}

var (
	Stdin  = getStdHandle(STD_INPUT_HANDLE)
	Stdout = getStdHandle(STD_OUTPUT_HANDLE)
	Stderr = getStdHandle(STD_ERROR_HANDLE)
)

func getStdHandle(h int32) (fd int) {
	r, _ := GetStdHandle(h)
	return int(r)
}

func Stat(path string, stat *Stat_t) (errno int) {
	if len(path) == 0 {
		return ERROR_PATH_NOT_FOUND
	}
	// Remove trailing slash.
	if path[len(path)-1] == '/' || path[len(path)-1] == '\\' {
		// Check if we're given root directory ("\" or "c:\").
		if len(path) == 1 || (len(path) == 3 && path[1] == ':') {
			// TODO(brainman): Perhaps should fetch other fields, not just FileAttributes.
			stat.Windata = Win32finddata{}
			a, e := GetFileAttributes(StringToUTF16Ptr(path))
			if e != 0 {
				return e
			}
			stat.Windata.FileAttributes = a
			return 0
		}
		path = path[:len(path)-1]
	}
	h, e := FindFirstFile(StringToUTF16Ptr(path), &stat.Windata)
	if e != 0 {
		return e
	}
	defer FindClose(h)
	stat.Mode = 0
	return 0
}

func Lstat(path string, stat *Stat_t) (errno int) {
	// no links on windows, just call Stat
	return Stat(path, stat)
}

const ImplementsGetwd = true

func Getwd() (wd string, errno int) {
	b := make([]uint16, 300)
	n, e := GetCurrentDirectory(uint32(len(b)), &b[0])
	if e != 0 {
		return "", e
	}
	return string(utf16.Decode(b[0:n])), 0
}

func Chdir(path string) (errno int) {
	return SetCurrentDirectory(&StringToUTF16(path)[0])
}

func Mkdir(path string, mode uint32) (errno int) {
	return CreateDirectory(&StringToUTF16(path)[0], nil)
}

func Rmdir(path string) (errno int) {
	return RemoveDirectory(&StringToUTF16(path)[0])
}

func Unlink(path string) (errno int) {
	return DeleteFile(&StringToUTF16(path)[0])
}

func Rename(oldpath, newpath string) (errno int) {
	from := &StringToUTF16(oldpath)[0]
	to := &StringToUTF16(newpath)[0]
	return MoveFile(from, to)
}

func ComputerName() (name string, errno int) {
	var n uint32 = MAX_COMPUTERNAME_LENGTH + 1
	b := make([]uint16, n)
	e := GetComputerName(&b[0], &n)
	if e != 0 {
		return "", e
	}
	return string(utf16.Decode(b[0:n])), 0
}

func Ftruncate(fd int, length int64) (errno int) {
	curoffset, e := Seek(fd, 0, 1)
	if e != 0 {
		return e
	}
	defer Seek(fd, curoffset, 0)
	_, e = Seek(fd, length, 0)
	if e != 0 {
		return e
	}
	e = SetEndOfFile(int32(fd))
	if e != 0 {
		return e
	}
	return 0
}

func Gettimeofday(tv *Timeval) (errno int) {
	var ft Filetime
	GetSystemTimeAsFileTime(&ft)
	*tv = NsecToTimeval(ft.Nanoseconds())
	return 0
}

func Sleep(nsec int64) (errno int) {
	sleep(uint32((nsec + 1e6 - 1) / 1e6)) // round up to milliseconds
	return 0
}

func Pipe(p []int) (errno int) {
	if len(p) != 2 {
		return EINVAL
	}
	var r, w uint32
	e := CreatePipe(&r, &w, makeInheritSa(), 0)
	if e != 0 {
		return e
	}
	p[0] = int(r)
	p[1] = int(w)
	return 0
}

func Utimes(path string, tv []Timeval) (errno int) {
	if len(tv) != 2 {
		return EINVAL
	}
	h, e := CreateFile(StringToUTF16Ptr(path),
		FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, nil,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0)
	if e != 0 {
		return e
	}
	defer Close(int(h))
	a := NsecToFiletime(tv[0].Nanoseconds())
	w := NsecToFiletime(tv[1].Nanoseconds())
	return SetFileTime(h, nil, &a, &w)
}

func Fsync(fd int) (errno int) {
	return FlushFileBuffers(int32(fd))
}

func Chmod(path string, mode uint32) (errno int) {
	if mode == 0 {
		return EINVAL
	}
	p := StringToUTF16Ptr(path)
	attrs, e := GetFileAttributes(p)
	if e != 0 {
		return e
	}
	if mode&S_IWRITE != 0 {
		attrs &^= FILE_ATTRIBUTE_READONLY
	} else {
		attrs |= FILE_ATTRIBUTE_READONLY
	}
	return SetFileAttributes(p, attrs)
}

// net api calls

//sys	WSAStartup(verreq uint32, data *WSAData) (sockerrno int) = wsock32.WSAStartup
//sys	WSACleanup() (errno int) [failretval==-1] = wsock32.WSACleanup
//sys	socket(af int32, typ int32, protocol int32) (handle int32, errno int) [failretval==-1] = wsock32.socket
//sys	setsockopt(s int32, level int32, optname int32, optval *byte, optlen int32) (errno int) [failretval==-1] = wsock32.setsockopt
//sys	bind(s int32, name uintptr, namelen int32) (errno int) [failretval==-1] = wsock32.bind
//sys	connect(s int32, name uintptr, namelen int32) (errno int) [failretval==-1] = wsock32.connect
//sys	getsockname(s int32, rsa *RawSockaddrAny, addrlen *int32) (errno int) [failretval==-1] = wsock32.getsockname
//sys	getpeername(s int32, rsa *RawSockaddrAny, addrlen *int32) (errno int) [failretval==-1] = wsock32.getpeername
//sys	listen(s int32, backlog int32) (errno int) [failretval==-1] = wsock32.listen
//sys	shutdown(s int32, how int32) (errno int) [failretval==-1] = wsock32.shutdown
//sys	Closesocket(s int32) (errno int) [failretval==-1] = wsock32.closesocket
//sys	AcceptEx(ls uint32, as uint32, buf *byte, rxdatalen uint32, laddrlen uint32, raddrlen uint32, recvd *uint32, overlapped *Overlapped) (errno int) = wsock32.AcceptEx
//sys	GetAcceptExSockaddrs(buf *byte, rxdatalen uint32, laddrlen uint32, raddrlen uint32, lrsa **RawSockaddrAny, lrsalen *int32, rrsa **RawSockaddrAny, rrsalen *int32) = wsock32.GetAcceptExSockaddrs
//sys	WSARecv(s uint32, bufs *WSABuf, bufcnt uint32, recvd *uint32, flags *uint32, overlapped *Overlapped, croutine *byte) (errno int) [failretval==-1] = ws2_32.WSARecv
//sys	WSASend(s uint32, bufs *WSABuf, bufcnt uint32, sent *uint32, flags uint32, overlapped *Overlapped, croutine *byte) (errno int) [failretval==-1] = ws2_32.WSASend
//sys	WSARecvFrom(s uint32, bufs *WSABuf, bufcnt uint32, recvd *uint32, flags *uint32,  from *RawSockaddrAny, fromlen *int32, overlapped *Overlapped, croutine *byte) (errno int) [failretval==-1] = ws2_32.WSARecvFrom
//sys	WSASendTo(s uint32, bufs *WSABuf, bufcnt uint32, sent *uint32, flags uint32, to *RawSockaddrAny, tolen int32,  overlapped *Overlapped, croutine *byte) (errno int) [failretval==-1] = ws2_32.WSASendTo
//sys	GetHostByName(name string) (h *Hostent, errno int) [failretval==nil] = ws2_32.gethostbyname
//sys	GetServByName(name string, proto string) (s *Servent, errno int) [failretval==nil] = ws2_32.getservbyname
//sys	Ntohs(netshort uint16) (u uint16) = ws2_32.ntohs
//sys	DnsQuery(name string, qtype uint16, options uint32, extra *byte, qrs **DNSRecord, pr *byte) (status uint32) = dnsapi.DnsQuery_W
//sys	DnsRecordListFree(rl *DNSRecord, freetype uint32) = dnsapi.DnsRecordListFree

// For testing: clients can set this flag to force
// creation of IPv6 sockets to return EAFNOSUPPORT.
var SocketDisableIPv6 bool

type RawSockaddrInet4 struct {
	Family uint16
	Port   uint16
	Addr   [4]byte /* in_addr */
	Zero   [8]uint8
}

type RawSockaddr struct {
	Family uint16
	Data   [14]int8
}

type RawSockaddrAny struct {
	Addr RawSockaddr
	Pad  [96]int8
}

type Sockaddr interface {
	sockaddr() (ptr uintptr, len int32, errno int) // lowercase; only we can define Sockaddrs
}

type SockaddrInet4 struct {
	Port int
	Addr [4]byte
	raw  RawSockaddrInet4
}

func (sa *SockaddrInet4) sockaddr() (uintptr, int32, int) {
	if sa.Port < 0 || sa.Port > 0xFFFF {
		return 0, 0, EINVAL
	}
	sa.raw.Family = AF_INET
	p := (*[2]byte)(unsafe.Pointer(&sa.raw.Port))
	p[0] = byte(sa.Port >> 8)
	p[1] = byte(sa.Port)
	for i := 0; i < len(sa.Addr); i++ {
		sa.raw.Addr[i] = sa.Addr[i]
	}
	return uintptr(unsafe.Pointer(&sa.raw)), int32(unsafe.Sizeof(sa.raw)), 0
}

type SockaddrInet6 struct {
	Port int
	Addr [16]byte
}

func (sa *SockaddrInet6) sockaddr() (uintptr, int32, int) {
	// TODO(brainman): implement SockaddrInet6.sockaddr()
	return 0, 0, EWINDOWS
}

type SockaddrUnix struct {
	Name string
}

func (sa *SockaddrUnix) sockaddr() (uintptr, int32, int) {
	// TODO(brainman): implement SockaddrUnix.sockaddr()
	return 0, 0, EWINDOWS
}

func (rsa *RawSockaddrAny) Sockaddr() (Sockaddr, int) {
	switch rsa.Addr.Family {
	case AF_UNIX:
		return nil, EWINDOWS

	case AF_INET:
		pp := (*RawSockaddrInet4)(unsafe.Pointer(rsa))
		sa := new(SockaddrInet4)
		p := (*[2]byte)(unsafe.Pointer(&pp.Port))
		sa.Port = int(p[0])<<8 + int(p[1])
		for i := 0; i < len(sa.Addr); i++ {
			sa.Addr[i] = pp.Addr[i]
		}
		return sa, 0

	case AF_INET6:
		return nil, EWINDOWS
	}
	return nil, EAFNOSUPPORT
}

func Socket(domain, typ, proto int) (fd, errno int) {
	if domain == AF_INET6 && SocketDisableIPv6 {
		return -1, EAFNOSUPPORT
	}
	h, e := socket(int32(domain), int32(typ), int32(proto))
	return int(h), int(e)
}

func SetsockoptInt(fd, level, opt int, value int) (errno int) {
	v := int32(value)
	return int(setsockopt(int32(fd), int32(level), int32(opt), (*byte)(unsafe.Pointer(&v)), int32(unsafe.Sizeof(v))))
}

func Bind(fd int, sa Sockaddr) (errno int) {
	ptr, n, err := sa.sockaddr()
	if err != 0 {
		return err
	}
	return bind(int32(fd), ptr, n)
}

func Connect(fd int, sa Sockaddr) (errno int) {
	ptr, n, err := sa.sockaddr()
	if err != 0 {
		return err
	}
	return connect(int32(fd), ptr, n)
}

func Getsockname(fd int) (sa Sockaddr, errno int) {
	var rsa RawSockaddrAny
	l := int32(unsafe.Sizeof(rsa))
	if errno = getsockname(int32(fd), &rsa, &l); errno != 0 {
		return
	}
	return rsa.Sockaddr()
}

func Getpeername(fd int) (sa Sockaddr, errno int) {
	var rsa RawSockaddrAny
	l := int32(unsafe.Sizeof(rsa))
	if errno = getpeername(int32(fd), &rsa, &l); errno != 0 {
		return
	}
	return rsa.Sockaddr()
}

func Listen(s int, n int) (errno int) {
	return int(listen(int32(s), int32(n)))
}

func Shutdown(fd, how int) (errno int) {
	return int(shutdown(int32(fd), int32(how)))
}

func WSASendto(s uint32, bufs *WSABuf, bufcnt uint32, sent *uint32, flags uint32, to Sockaddr, overlapped *Overlapped, croutine *byte) (errno int) {
	rsa, l, err := to.sockaddr()
	if err != 0 {
		return err
	}
	errno = WSASendTo(s, bufs, bufcnt, sent, flags, (*RawSockaddrAny)(unsafe.Pointer(rsa)), l, overlapped, croutine)
	return
}

// Invented structures to support what package os expects.
type Rusage struct{}

type WaitStatus struct {
	Status   uint32
	ExitCode uint32
}

func (w WaitStatus) Exited() bool { return true }

func (w WaitStatus) ExitStatus() int { return int(w.ExitCode) }

func (w WaitStatus) Signal() int { return -1 }

func (w WaitStatus) CoreDump() bool { return false }

func (w WaitStatus) Stopped() bool { return false }

func (w WaitStatus) Continued() bool { return false }

func (w WaitStatus) StopSignal() int { return -1 }

func (w WaitStatus) Signaled() bool { return true }

func (w WaitStatus) TrapCause() int { return -1 }

// TODO(brainman): fix all needed for net

func Accept(fd int) (nfd int, sa Sockaddr, errno int)                        { return 0, nil, EWINDOWS }
func Recvfrom(fd int, p []byte, flags int) (n int, from Sockaddr, errno int) { return 0, nil, EWINDOWS }
func Sendto(fd int, p []byte, flags int, to Sockaddr) (errno int)            { return EWINDOWS }
func SetsockoptTimeval(fd, level, opt int, tv *Timeval) (errno int)          { return EWINDOWS }

type Linger struct {
	Onoff  int32
	Linger int32
}

const (
	IP_ADD_MEMBERSHIP = iota
	IP_DROP_MEMBERSHIP
)

type IpMreq struct {
	Multiaddr [4]byte /* in_addr */
	Interface [4]byte /* in_addr */
}

func SetsockoptLinger(fd, level, opt int, l *Linger) (errno int)    { return EWINDOWS }
func SetsockoptIpMreq(fd, level, opt int, mreq *IpMreq) (errno int) { return EWINDOWS }
func BindToDevice(fd int, device string) (errno int)                { return EWINDOWS }

// TODO(brainman): fix all needed for os

const (
	SIGTRAP = 5
)

func Getpid() (pid int)   { return -1 }
func Getppid() (ppid int) { return -1 }

func Fchdir(fd int) (errno int)                           { return EWINDOWS }
func Link(oldpath, newpath string) (errno int)            { return EWINDOWS }
func Symlink(path, link string) (errno int)               { return EWINDOWS }
func Readlink(path string, buf []byte) (n int, errno int) { return 0, EWINDOWS }

func Fchmod(fd int, mode uint32) (errno int)           { return EWINDOWS }
func Chown(path string, uid int, gid int) (errno int)  { return EWINDOWS }
func Lchown(path string, uid int, gid int) (errno int) { return EWINDOWS }
func Fchown(fd int, uid int, gid int) (errno int)      { return EWINDOWS }

func Getuid() (uid int)                  { return -1 }
func Geteuid() (euid int)                { return -1 }
func Getgid() (gid int)                  { return -1 }
func Getegid() (egid int)                { return -1 }
func Getgroups() (gids []int, errno int) { return nil, EWINDOWS }

// TODO(brainman): fix all this meaningless code, it is here to compile exec.go

func read(fd int, buf *byte, nbuf int) (n int, errno int) {
	return 0, EWINDOWS
}

func fcntl(fd, cmd, arg int) (val int, errno int) {
	return 0, EWINDOWS
}

const (
	PTRACE_TRACEME = 1 + iota
	WNOHANG
	WSTOPPED
	WUNTRACED
	SYS_CLOSE
	SYS_WRITE
	SYS_EXIT
	SYS_READ
)
