// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// PE (Portable Executable) file writing
// http://www.microsoft.com/whdc/system/platform/firmware/PECOFF.mspx

#include <time.h>

#include "l.h"
#include "../ld/lib.h"
#include "../ld/pe.h"
#include "../ld/dwarf.h"

// DOS stub that prints out
// "This program cannot be run in DOS mode."
static char dosstub[] =
{
	0x4d, 0x5a, 0x90, 0x00, 0x03, 0x00, 0x04, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
	0x8b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
	0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd,
	0x21, 0xb8, 0x01, 0x4c, 0xcd, 0x21, 0x54, 0x68,
	0x69, 0x73, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72,
	0x61, 0x6d, 0x20, 0x63, 0x61, 0x6e, 0x6e, 0x6f,
	0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6e,
	0x20, 0x69, 0x6e, 0x20, 0x44, 0x4f, 0x53, 0x20,
	0x6d, 0x6f, 0x64, 0x65, 0x2e, 0x0d, 0x0d, 0x0a,
	0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static char symnames[256]; 
static int  nextsymoff;

int32 PESECTHEADR;
int32 PEFILEHEADR;

static int pe64;
static int nsect;
static int nextsectoff;
static int nextfileoff;

static IMAGE_FILE_HEADER fh;
static IMAGE_OPTIONAL_HEADER oh;
static PE64_IMAGE_OPTIONAL_HEADER oh64;
static IMAGE_SECTION_HEADER sh[16];
static IMAGE_DATA_DIRECTORY* dd;

#define	set(n, v)	(pe64 ? (oh64.n = v) : (oh.n = v))
#define	put(v)		(pe64 ? vputl(v) : lputl(v))

typedef struct Imp Imp;
struct Imp {
	Sym* s;
	uvlong off;
	Imp* next;
};

typedef struct Dll Dll;
struct Dll {
	char* name;
	uvlong nameoff;
	uvlong thunkoff;
	Imp* ms;
	Dll* next;
};

static Dll* dr;

static Sym *dexport[1024];
static int nexport;

static IMAGE_SECTION_HEADER*
addpesection(char *name, int sectsize, int filesize, Segment *s)
{
	IMAGE_SECTION_HEADER *h;

	if(nsect == 16) {
		diag("too many sections");
		errorexit();
	}
	h = &sh[nsect++];
	strncpy((char*)h->Name, name, sizeof(h->Name));
	h->VirtualSize = sectsize;
	h->VirtualAddress = nextsectoff;
	nextsectoff = rnd(nextsectoff+sectsize, PESECTALIGN);
	h->PointerToRawData = nextfileoff;
	if(filesize > 0) {
		h->SizeOfRawData = rnd(filesize, PEFILEALIGN);
		nextfileoff += h->SizeOfRawData;
	}
	if(s) {
		if(s->vaddr-PEBASE != h->VirtualAddress) {
			diag("%s.VirtualAddress = %#llux, want %#llux", name, (vlong)h->VirtualAddress, (vlong)(s->vaddr-PEBASE));
			errorexit();
		}
		if(s->fileoff != h->PointerToRawData) {
			diag("%s.PointerToRawData = %#llux, want %#llux", name, (vlong)h->PointerToRawData, (vlong)(s->fileoff));
			errorexit();
		}
	}
	return h;
}

void
peinit(void)
{
	int32 l;

	switch(thechar) {
	// 64-bit architectures
	case '6':
		pe64 = 1;
		l = sizeof(oh64);
		dd = oh64.DataDirectory;
		break;
	// 32-bit architectures
	default:
		l = sizeof(oh);
		dd = oh.DataDirectory;
		break;
	}
	
	PEFILEHEADR = rnd(sizeof(dosstub)+sizeof(fh)+l+sizeof(sh), PEFILEALIGN);
	PESECTHEADR = rnd(PEFILEHEADR, PESECTALIGN);
	nextsectoff = PESECTHEADR;
	nextfileoff = PEFILEHEADR;
}

static void
pewrite(void)
{
	seek(cout, 0, 0);
	ewrite(cout, dosstub, sizeof dosstub);
	strnput("PE", 4);
	cflush();
	// TODO: This code should not assume that the
	// memory representation is little-endian or
	// that the structs are packed identically to
	// their file representation.
	ewrite(cout, &fh, sizeof fh);
	if(pe64)
		ewrite(cout, &oh64, sizeof oh64);
	else
		ewrite(cout, &oh, sizeof oh);
	ewrite(cout, &sh, nsect * sizeof sh[0]);
}

static void
strput(char *s)
{
	int n;

	for(n=0; *s; n++)
		cput(*s++);
	cput('\0');
	n++;
	// string must be padded to even size
	if(n%2)
		cput('\0');
}

static Dll* 
initdynimport(void)
{
	Imp *m;
	Dll *d;
	Sym *s, *dynamic;
	int i;

	dr = nil;
	
	for(i=0; i<NHASH; i++)
	for(s = hash[i]; s != S; s = s->hash) {
		if(!s->reachable || !s->dynimpname || s->dynexport)
			continue;
		for(d = dr; d != nil; d = d->next) {
			if(strcmp(d->name,s->dynimplib) == 0) {
				m = mal(sizeof *m);
				break;
			}
		}
		if(d == nil) {
			d = mal(sizeof *d);
			d->name = s->dynimplib;
			d->next = dr;
			dr = d;
			m = mal(sizeof *m);
		}
		m->s = s;
		m->next = d->ms;
		d->ms = m;
	}
	
	dynamic = lookup(".windynamic", 0);
	dynamic->reachable = 1;
	dynamic->type = SWINDOWS;
	for(d = dr; d != nil; d = d->next) {
		for(m = d->ms; m != nil; m = m->next) {
			m->s->type = SWINDOWS | SSUB;
			m->s->sub = dynamic->sub;
			dynamic->sub = m->s;
			m->s->value = dynamic->size;
			dynamic->size += PtrSize;
		}
		dynamic->size += PtrSize;
	}
		
	return dr;
}

static void
addimports(vlong fileoff, IMAGE_SECTION_HEADER *datsect)
{
	IMAGE_SECTION_HEADER *isect;
	uvlong n, oftbase, ftbase;
	Imp *m;
	Dll *d;
	Sym* dynamic;
	
	dynamic = lookup(".windynamic", 0);

	// skip import descriptor table (will write it later)
	n = 0;
	for(d = dr; d != nil; d = d->next)
		n++;
	seek(cout, fileoff + sizeof(IMAGE_IMPORT_DESCRIPTOR) * (n + 1), 0);

	// write dll names
	for(d = dr; d != nil; d = d->next) {
		d->nameoff = cpos() - fileoff;
		strput(d->name);
	}

	// write function names
	for(d = dr; d != nil; d = d->next) {
		for(m = d->ms; m != nil; m = m->next) {
			m->off = nextsectoff + cpos() - fileoff;
			wputl(0); // hint
			strput(m->s->dynimpname);
		}
	}
	
	// write OriginalFirstThunks
	oftbase = cpos() - fileoff;
	n = cpos();
	for(d = dr; d != nil; d = d->next) {
		d->thunkoff = cpos() - n;
		for(m = d->ms; m != nil; m = m->next)
			put(m->off);
		put(0);
	}

	// add pe section and pad it at the end
	n = cpos() - fileoff;
	isect = addpesection(".idata", n, n, 0);
	isect->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA|
		IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
	strnput("", isect->SizeOfRawData - n);
	cflush();

	// write FirstThunks (allocated in .data section)
	ftbase = dynamic->value - datsect->VirtualAddress - PEBASE;
	seek(cout, datsect->PointerToRawData + ftbase, 0);
	for(d = dr; d != nil; d = d->next) {
		for(m = d->ms; m != nil; m = m->next)
			put(m->off);
		put(0);
	}
	cflush();
	
	// finally write import descriptor table
	seek(cout, fileoff, 0);
	for(d = dr; d != nil; d = d->next) {
		lputl(isect->VirtualAddress + oftbase + d->thunkoff);
		lputl(0);
		lputl(0);
		lputl(isect->VirtualAddress + d->nameoff);
		lputl(datsect->VirtualAddress + ftbase + d->thunkoff);
	}
	lputl(0); //end
	lputl(0);
	lputl(0);
	lputl(0);
	lputl(0);
	cflush();
	
	// update data directory
	dd[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = isect->VirtualAddress;
	dd[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = isect->VirtualSize;
	dd[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = dynamic->value - PEBASE;
	dd[IMAGE_DIRECTORY_ENTRY_IAT].Size = dynamic->size;

	seek(cout, 0, 2);
}

static int
scmp(const void *p1, const void *p2)
{
	Sym *s1, *s2;

	s1 = *(Sym**)p1;
	s2 = *(Sym**)p2;
	return strcmp(s1->dynimpname, s2->dynimpname);
}

static void
initdynexport(void)
{
	int i;
	Sym *s;
	
	nexport = 0;
	for(i=0; i<NHASH; i++)
	for(s = hash[i]; s != S; s = s->hash) {
		if(!s->reachable || !s->dynimpname || !s->dynexport)
			continue;
		if(nexport+1 > sizeof(dexport)/sizeof(dexport[0])) {
			diag("pe dynexport table is full");
			errorexit();
		}
		
		dexport[nexport] = s;
		nexport++;
	}
	
	qsort(dexport, nexport, sizeof dexport[0], scmp);
}

void
addexports(vlong fileoff)
{
	IMAGE_SECTION_HEADER *sect;
	IMAGE_EXPORT_DIRECTORY e;
	int size, i, va, va_name, va_addr, va_na, v;

	size = sizeof e + 10*nexport + strlen(outfile) + 1;
	for(i=0; i<nexport; i++)
		size += strlen(dexport[i]->dynimpname) + 1;
	
	if (nexport == 0)
		return;
		
	sect = addpesection(".edata", size, size, 0);
	sect->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA|IMAGE_SCN_MEM_READ;
	va = sect->VirtualAddress;
	dd[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = va;
	dd[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = sect->VirtualSize;

	seek(cout, fileoff, 0);
	va_name = va + sizeof e + nexport*4;
	va_addr = va + sizeof e;
	va_na = va + sizeof e + nexport*8;

	e.Characteristics = 0;
	e.MajorVersion = 0;
	e.MinorVersion = 0;
	e.NumberOfFunctions = nexport;
	e.NumberOfNames = nexport;
	e.Name = va + sizeof e + nexport*10; // Program names.
	e.Base = 1;
	e.AddressOfFunctions = va_addr;
	e.AddressOfNames = va_name;
	e.AddressOfNameOrdinals = va_na;
	// put IMAGE_EXPORT_DIRECTORY
	for (i=0; i<sizeof(e); i++)
		cput(((char*)&e)[i]);
	// put EXPORT Address Table
	for(i=0; i<nexport; i++)
		lputl(dexport[i]->value - PEBASE);		
	// put EXPORT Name Pointer Table
	v = e.Name + strlen(outfile)+1;
	for(i=0; i<nexport; i++) {
		lputl(v);
		v += strlen(dexport[i]->dynimpname)+1;
	}
	// put EXPORT Ordinal Table
	for(i=0; i<nexport; i++)
		wputl(i);
	// put Names
	strnput(outfile, strlen(outfile)+1);
	for(i=0; i<nexport; i++)
		strnput(dexport[i]->dynimpname, strlen(dexport[i]->dynimpname)+1);
	strnput("", sect->SizeOfRawData - size);
	cflush();

	seek(cout, 0, 2);
}

void
dope(void)
{
	Sym *rel;

	/* relocation table */
	rel = lookup(".rel", 0);
	rel->reachable = 1;
	rel->type = SELFDATA;

	initdynimport();
	initdynexport();
}

/*
 * For more than 8 characters section names, name contains a slash (/) that is 
 * followed by an ASCII representation of a decimal number that is an offset into 
 * the string table. 
 * reference: pecoff_v8.docx Page 24.
 * <http://www.microsoft.com/whdc/system/platform/firmware/PECOFFdwn.mspx>
 */
IMAGE_SECTION_HEADER*
newPEDWARFSection(char *name, vlong size)
{
	IMAGE_SECTION_HEADER *h;
	char s[8];

	if(nextsymoff+strlen(name)+1 > sizeof(symnames)) {
		diag("pe string table is full");
		errorexit();
	}

	strcpy(&symnames[nextsymoff], name);
	sprint(s, "/%d\0", nextsymoff+4);
	nextsymoff += strlen(name);
	symnames[nextsymoff] = 0;
	nextsymoff ++;
	h = addpesection(s, size, size, 0);
	h->Characteristics = IMAGE_SCN_MEM_READ|
		IMAGE_SCN_MEM_DISCARDABLE;

	return h;
}

static void
addsymtable(void)
{
	IMAGE_SECTION_HEADER *h;
	int i, size;
	
	if(nextsymoff == 0)
		return;
	
	size  = nextsymoff + 4;
	h = addpesection(".symtab", size, size, 0);
	h->Characteristics = IMAGE_SCN_MEM_READ|
		IMAGE_SCN_MEM_DISCARDABLE;
	fh.PointerToSymbolTable = cpos();
	fh.NumberOfSymbols = 0;
	// put symbol string table
	lputl(size);
	for (i=0; i<nextsymoff; i++)
		cput(symnames[i]);
	strnput("", h->SizeOfRawData - size);
	cflush();
}

void
asmbpe(void)
{
	IMAGE_SECTION_HEADER *t, *d;

	switch(thechar) {
	default:
		diag("unknown PE architecture");
		errorexit();
	case '6':
		fh.Machine = IMAGE_FILE_MACHINE_AMD64;
		break;
	case '8':
		fh.Machine = IMAGE_FILE_MACHINE_I386;
		break;
	}

	t = addpesection(".text", segtext.len, segtext.len, &segtext);
	t->Characteristics = IMAGE_SCN_CNT_CODE|
		IMAGE_SCN_CNT_INITIALIZED_DATA|
		IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;

	d = addpesection(".data", segdata.len, segdata.filelen, &segdata);
	d->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA|
		IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;

	addimports(nextfileoff, d);
	
	addexports(nextfileoff);
	
	if(!debug['s'])
		dwarfaddpeheaders();

	addsymtable();
		
	fh.NumberOfSections = nsect;
	fh.TimeDateStamp = time(0);
	fh.Characteristics = IMAGE_FILE_RELOCS_STRIPPED|
		IMAGE_FILE_EXECUTABLE_IMAGE|IMAGE_FILE_DEBUG_STRIPPED;
	if (pe64) {
		fh.SizeOfOptionalHeader = sizeof(oh64);
		fh.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
		set(Magic, 0x20b);	// PE32+
	} else {
		fh.SizeOfOptionalHeader = sizeof(oh);
		fh.Characteristics |= IMAGE_FILE_32BIT_MACHINE;
		set(Magic, 0x10b);	// PE32
		oh.BaseOfData = d->VirtualAddress;
	}
	set(MajorLinkerVersion, 1);
	set(MinorLinkerVersion, 0);
	set(SizeOfCode, t->SizeOfRawData);
	set(SizeOfInitializedData, d->SizeOfRawData);
	set(SizeOfUninitializedData, 0);
	set(AddressOfEntryPoint, entryvalue()-PEBASE);
	set(BaseOfCode, t->VirtualAddress);
	set(ImageBase, PEBASE);
	set(SectionAlignment, PESECTALIGN);
	set(FileAlignment, PEFILEALIGN);
	set(MajorOperatingSystemVersion, 4);
	set(MinorOperatingSystemVersion, 0);
	set(MajorImageVersion, 1);
	set(MinorImageVersion, 0);
	set(MajorSubsystemVersion, 4);
	set(MinorSubsystemVersion, 0);
	set(SizeOfImage, nextsectoff);
	set(SizeOfHeaders, PEFILEHEADR);
	if(strcmp(headstring, "windowsgui") == 0)
		set(Subsystem, IMAGE_SUBSYSTEM_WINDOWS_GUI);
	else
		set(Subsystem, IMAGE_SUBSYSTEM_WINDOWS_CUI);
	set(SizeOfStackReserve, 0x0040000);
	set(SizeOfStackCommit, 0x00001000);
	set(SizeOfHeapReserve, 0x00100000);
	set(SizeOfHeapCommit, 0x00001000);
	set(NumberOfRvaAndSizes, 16);

	pewrite();
}
