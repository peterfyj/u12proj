// This file ought to be machine-generated,
// but seeing that for ucore it's easier to 
// write it manually, I decide to do it myself.

package syscall

const (
	E_UNSPECIFIED		= 1		// Unspecified or unknown problem
	E_BAD_PROC			= 2		// Process doesn't exist or otherwise
	E_INVAL				= 3		// Invalid parameter
	E_NO_MEM			= 4		// Request failed due to memory shortage
	E_NO_FREE_PROC		= 5		// Attempt to create a new process beyond
	E_FAULT				= 6		// Memory fault
	E_SWAP_FAULT		= 7		// SWAP READ/WRITE fault
	E_INVAL_ELF			= 8		// Invalid elf file
	E_KILLED			= 9		// Process is killed
	E_PANIC				= 10	// Panic Failure
	E_TIMEOUT			= 11	// Timeout
	E_TOO_BIG			= 12	// Argument is Too Big
	E_NO_DEV			= 13	// No such Device
	E_NA_DEV			= 14	// Device Not Available
	E_BUSY				= 15	// Device/File is Busy
	E_NOENT				= 16	// No Such File or Directory
	E_ISDIR				= 17	// Is a Directory
	E_NOTDIR			= 18	// Not a Directory
	E_XDEV				= 19	// Cross Device-Link
	E_UNIMP				= 20	// Unimplemented Feature
	E_SEEK				= 21	// Illegal Seek
	E_MAX_OPEN			= 22	// Too Many Files are Open
	E_EXISTS			= 23	// File/Directory Already Exists
	E_NOTEMPTY			= 24	// Directory is Not Empty
)


var errors = [...]string{
	1:   "Unspecified or unknown problem",
	2:   "Process doesn't exist or otherwise",
	3:   "Invalid parameter",
	4:   "Request failed due to memory shortage",
	5:   "Attempt to create a new process beyond",
	6:   "Memory fault",
	7:   "SWAP READ/WRITE fault",
	8:   "Invalid elf file",
	9:   "Process is killed",
	10:  "Panic Failure",
	11:  "Timeout",
	12:  "Argument is Too Big",
	13:  "No such Device",
	14:  "Device Not Available",
	15:  "Device/File is Busy",
	16:  "No Such File or Directory",
	17:  "Is a Directory",
	18:  "Not a Directory",
	19:  "Cross Device-Link",
	20:  "Unimplemented Feature",
	21:  "Illegal Seek",
	22:  "Too Many Files are Open",
	23:  "File/Directory Already Exists",
	24:  "Directory is Not Empty",
}
