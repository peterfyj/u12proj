// godefs -gsyscall -f-m32 types_freebsd.c

// MACHINE GENERATED - DO NOT EDIT.

package syscall

// Constants
const (
	sizeofPtr              = 0x4
	sizeofShort            = 0x2
	sizeofInt              = 0x4
	sizeofLong             = 0x4
	sizeofLongLong         = 0x8
	O_CLOEXEC              = 0
	S_IFMT                 = 0xf000
	S_IFIFO                = 0x1000
	S_IFCHR                = 0x2000
	S_IFDIR                = 0x4000
	S_IFBLK                = 0x6000
	S_IFREG                = 0x8000
	S_IFLNK                = 0xa000
	S_IFSOCK               = 0xc000
	S_ISUID                = 0x800
	S_ISGID                = 0x400
	S_ISVTX                = 0x200
	S_IRUSR                = 0x100
	S_IWUSR                = 0x80
	S_IXUSR                = 0x40
	SizeofSockaddrInet4    = 0x10
	SizeofSockaddrInet6    = 0x1c
	SizeofSockaddrAny      = 0x6c
	SizeofSockaddrUnix     = 0x6a
	SizeofSockaddrDatalink = 0x36
	SizeofLinger           = 0x8
	SizeofIpMreq           = 0x8
	SizeofMsghdr           = 0x1c
	SizeofCmsghdr          = 0xc
	PTRACE_TRACEME         = 0
	PTRACE_CONT            = 0x7
	PTRACE_KILL            = 0x8
	SizeofIfMsghdr         = 0x60
	SizeofIfData           = 0x50
	SizeofIfaMsghdr        = 0x14
	SizeofRtMsghdr         = 0x5c
	SizeofRtMetrics        = 0x38
)

// Types

type _C_short int16

type _C_int int32

type _C_long int32

type _C_long_long int64

type Timespec struct {
	Sec  int32
	Nsec int32
}

type Timeval struct {
	Sec  int32
	Usec int32
}

type Rusage struct {
	Utime    Timeval
	Stime    Timeval
	Maxrss   int32
	Ixrss    int32
	Idrss    int32
	Isrss    int32
	Minflt   int32
	Majflt   int32
	Nswap    int32
	Inblock  int32
	Oublock  int32
	Msgsnd   int32
	Msgrcv   int32
	Nsignals int32
	Nvcsw    int32
	Nivcsw   int32
}

type Rlimit struct {
	Cur int64
	Max int64
}

type _Gid_t uint32

type Stat_t struct {
	Dev           uint32
	Ino           uint32
	Mode          uint16
	Nlink         uint16
	Uid           uint32
	Gid           uint32
	Rdev          uint32
	Atimespec     Timespec
	Mtimespec     Timespec
	Ctimespec     Timespec
	Size          int64
	Blocks        int64
	Blksize       uint32
	Flags         uint32
	Gen           uint32
	Lspare        int32
	Birthtimespec Timespec
	Pad_godefs_0  uint32
	Pad_godefs_1  uint32
}

type Statfs_t struct {
	Version     uint32
	Type        uint32
	Flags       uint64
	Bsize       uint64
	Iosize      uint64
	Blocks      uint64
	Bfree       uint64
	Bavail      int64
	Files       uint64
	Ffree       int64
	Syncwrites  uint64
	Asyncwrites uint64
	Syncreads   uint64
	Asyncreads  uint64
	Spare       [10]uint64
	Namemax     uint32
	Owner       uint32
	Fsid        [8]byte /* fsid */
	Charspare   [80]int8
	Fstypename  [16]int8
	Mntfromname [88]int8
	Mntonname   [88]int8
}

type Flock_t struct {
	Start  int64
	Len    int64
	Pid    int32
	Type   int16
	Whence int16
	Sysid  int32
}

type Dirent struct {
	Fileno uint32
	Reclen uint16
	Type   uint8
	Namlen uint8
	Name   [256]int8
}

type RawSockaddrInet4 struct {
	Len    uint8
	Family uint8
	Port   uint16
	Addr   [4]byte /* in_addr */
	Zero   [8]int8
}

type RawSockaddrInet6 struct {
	Len      uint8
	Family   uint8
	Port     uint16
	Flowinfo uint32
	Addr     [16]byte /* in6_addr */
	Scope_id uint32
}

type RawSockaddrUnix struct {
	Len    uint8
	Family uint8
	Path   [104]int8
}

type RawSockaddrDatalink struct {
	Len    uint8
	Family uint8
	Index  uint16
	Type   uint8
	Nlen   uint8
	Alen   uint8
	Slen   uint8
	Data   [46]int8
}

type RawSockaddr struct {
	Len    uint8
	Family uint8
	Data   [14]int8
}

type RawSockaddrAny struct {
	Addr RawSockaddr
	Pad  [92]int8
}

type _Socklen uint32

type Linger struct {
	Onoff  int32
	Linger int32
}

type Iovec struct {
	Base *byte
	Len  uint32
}

type IpMreq struct {
	Multiaddr [4]byte /* in_addr */
	Interface [4]byte /* in_addr */
}

type Msghdr struct {
	Name       *byte
	Namelen    uint32
	Iov        *Iovec
	Iovlen     int32
	Control    *byte
	Controllen uint32
	Flags      int32
}

type Cmsghdr struct {
	Len   uint32
	Level int32
	Type  int32
}

type Kevent_t struct {
	Ident  uint32
	Filter int16
	Flags  uint16
	Fflags uint32
	Data   int32
	Udata  *byte
}

type FdSet struct {
	X__fds_bits [32]uint32
}

type IfMsghdr struct {
	Msglen       uint16
	Version      uint8
	Type         uint8
	Addrs        int32
	Flags        int32
	Index        uint16
	Pad_godefs_0 [2]byte
	Data         IfData
}

type IfData struct {
	Type        uint8
	Physical    uint8
	Addrlen     uint8
	Hdrlen      uint8
	Link_state  uint8
	Spare_char1 uint8
	Spare_char2 uint8
	Datalen     uint8
	Mtu         uint32
	Metric      uint32
	Baudrate    uint32
	Ipackets    uint32
	Ierrors     uint32
	Opackets    uint32
	Oerrors     uint32
	Collisions  uint32
	Ibytes      uint32
	Obytes      uint32
	Imcasts     uint32
	Omcasts     uint32
	Iqdrops     uint32
	Noproto     uint32
	Hwassist    uint32
	Epoch       int32
	Lastchange  Timeval
}

type IfaMsghdr struct {
	Msglen       uint16
	Version      uint8
	Type         uint8
	Addrs        int32
	Flags        int32
	Index        uint16
	Pad_godefs_0 [2]byte
	Metric       int32
}

type RtMsghdr struct {
	Msglen       uint16
	Version      uint8
	Type         uint8
	Index        uint16
	Pad_godefs_0 [2]byte
	Flags        int32
	Addrs        int32
	Pid          int32
	Seq          int32
	Errno        int32
	Fmask        int32
	Inits        uint32
	Rmx          RtMetrics
}

type RtMetrics struct {
	Locks    uint32
	Mtu      uint32
	Hopcount uint32
	Expire   uint32
	Recvpipe uint32
	Sendpipe uint32
	Ssthresh uint32
	Rtt      uint32
	Rttvar   uint32
	Pksent   uint32
	Weight   uint32
	Filler   [3]uint32
}
