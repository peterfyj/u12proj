// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Mach-O file writing
// http://developer.apple.com/mac/library/DOCUMENTATION/DeveloperTools/Conceptual/MachORuntime/Reference/reference.html

#include "l.h"
#include "../ld/dwarf.h"
#include "../ld/lib.h"
#include "../ld/macho.h"

static	int	macho64;
static	MachoHdr	hdr;
static	MachoLoad	load[16];
static	MachoSeg	seg[16];
static	MachoDebug	xdebug[16];
static	int	nload, nseg, ndebug, nsect;

void
machoinit(void)
{
	switch(thechar) {
	// 64-bit architectures
	case '6':
		macho64 = 1;
		break;

	// 32-bit architectures
	default:
		break;
	}
}

MachoHdr*
getMachoHdr(void)
{
	return &hdr;
}

MachoLoad*
newMachoLoad(uint32 type, uint32 ndata)
{
	MachoLoad *l;

	if(nload >= nelem(load)) {
		diag("too many loads");
		errorexit();
	}
	
	if(macho64 && (ndata & 1))
		ndata++;
	
	l = &load[nload++];
	l->type = type;
	l->ndata = ndata;
	l->data = mal(ndata*4);
	return l;
}

MachoSeg*
newMachoSeg(char *name, int msect)
{
	MachoSeg *s;

	if(nseg >= nelem(seg)) {
		diag("too many segs");
		errorexit();
	}
	s = &seg[nseg++];
	s->name = name;
	s->msect = msect;
	s->sect = mal(msect*sizeof s->sect[0]);
	return s;
}

MachoSect*
newMachoSect(MachoSeg *seg, char *name)
{
	MachoSect *s;

	if(seg->nsect >= seg->msect) {
		diag("too many sects in segment %s", seg->name);
		errorexit();
	}
	s = &seg->sect[seg->nsect++];
	s->name = name;
	nsect++;
	return s;
}

MachoDebug*
newMachoDebug(void)
{
	if(ndebug >= nelem(xdebug)) {
		diag("too many debugs");
		errorexit();
	}
	return &xdebug[ndebug++];
}


// Generic linking code.

static char **dylib;
static int ndylib;

static vlong linkoff;

int
machowrite(void)
{
	vlong o1;
	int loadsize;
	int i, j;
	MachoSeg *s;
	MachoSect *t;
	MachoDebug *d;
	MachoLoad *l;

	o1 = cpos();

	loadsize = 4*4*ndebug;
	for(i=0; i<nload; i++)
		loadsize += 4*(load[i].ndata+2);
	if(macho64) {
		loadsize += 18*4*nseg;
		loadsize += 20*4*nsect;
	} else {
		loadsize += 14*4*nseg;
		loadsize += 17*4*nsect;
	}

	if(macho64)
		LPUT(0xfeedfacf);
	else
		LPUT(0xfeedface);
	LPUT(hdr.cpu);
	LPUT(hdr.subcpu);
	LPUT(2);	/* file type - mach executable */
	LPUT(nload+nseg+ndebug);
	LPUT(loadsize);
	LPUT(1);	/* flags - no undefines */
	if(macho64)
		LPUT(0);	/* reserved */

	for(i=0; i<nseg; i++) {
		s = &seg[i];
		if(macho64) {
			LPUT(25);	/* segment 64 */
			LPUT(72+80*s->nsect);
			strnput(s->name, 16);
			VPUT(s->vaddr);
			VPUT(s->vsize);
			VPUT(s->fileoffset);
			VPUT(s->filesize);
			LPUT(s->prot1);
			LPUT(s->prot2);
			LPUT(s->nsect);
			LPUT(s->flag);
		} else {
			LPUT(1);	/* segment 32 */
			LPUT(56+68*s->nsect);
			strnput(s->name, 16);
			LPUT(s->vaddr);
			LPUT(s->vsize);
			LPUT(s->fileoffset);
			LPUT(s->filesize);
			LPUT(s->prot1);
			LPUT(s->prot2);
			LPUT(s->nsect);
			LPUT(s->flag);
		}
		for(j=0; j<s->nsect; j++) {
			t = &s->sect[j];
			if(macho64) {
				strnput(t->name, 16);
				strnput(s->name, 16);
				VPUT(t->addr);
				VPUT(t->size);
				LPUT(t->off);
				LPUT(t->align);
				LPUT(t->reloc);
				LPUT(t->nreloc);
				LPUT(t->flag);
				LPUT(t->res1);	/* reserved */
				LPUT(t->res2);	/* reserved */
				LPUT(0);	/* reserved */
			} else {
				strnput(t->name, 16);
				strnput(s->name, 16);
				LPUT(t->addr);
				LPUT(t->size);
				LPUT(t->off);
				LPUT(t->align);
				LPUT(t->reloc);
				LPUT(t->nreloc);
				LPUT(t->flag);
				LPUT(t->res1);	/* reserved */
				LPUT(t->res2);	/* reserved */
			}
		}
	}

	for(i=0; i<nload; i++) {
		l = &load[i];
		LPUT(l->type);
		LPUT(4*(l->ndata+2));
		for(j=0; j<l->ndata; j++)
			LPUT(l->data[j]);
	}

	for(i=0; i<ndebug; i++) {
		d = &xdebug[i];
		LPUT(3);	/* obsolete gdb debug info */
		LPUT(16);	/* size of symseg command */
		LPUT(d->fileoffset);
		LPUT(d->filesize);
	}

	return cpos() - o1;
}

void
domacho(void)
{
	Sym *s;

	if(debug['d'])
		return;

	// empirically, string table must begin with " \x00".
	s = lookup(".dynstr", 0);
	s->type = SMACHODYNSTR;
	s->reachable = 1;
	adduint8(s, ' ');
	adduint8(s, '\0');
	
	s = lookup(".dynsym", 0);
	s->type = SMACHODYNSYM;
	s->reachable = 1;
	
	s = lookup(".plt", 0);	// will be __symbol_stub
	s->type = SMACHOPLT;
	s->reachable = 1;
	
	s = lookup(".got", 0);	// will be __nl_symbol_ptr
	s->type = SMACHOGOT;
	s->reachable = 1;
	
	s = lookup(".linkedit.plt", 0);	// indirect table for .plt
	s->type = SMACHOINDIRECTPLT;
	s->reachable = 1;
	
	s = lookup(".linkedit.got", 0);	// indirect table for .got
	s->type = SMACHOINDIRECTGOT;
	s->reachable = 1;
}

void
machoadddynlib(char *lib)
{
	if(ndylib%32 == 0) {
		dylib = realloc(dylib, (ndylib+32)*sizeof dylib[0]);
		if(dylib == nil) {
			diag("out of memory");
			errorexit();
		}
	}
	dylib[ndylib++] = lib;
}

void
asmbmacho(void)
{
	vlong v, w;
	vlong va;
	int a, i;
	MachoHdr *mh;
	MachoSect *msect;
	MachoSeg *ms;
	MachoDebug *md;
	MachoLoad *ml;
	Sym *s;

	/* apple MACH */
	va = INITTEXT - HEADR;
	mh = getMachoHdr();
	switch(thechar){
	default:
		diag("unknown mach architecture");
		errorexit();
	case '6':
		mh->cpu = MACHO_CPU_AMD64;
		mh->subcpu = MACHO_SUBCPU_X86;
		break;
	case '8':
		mh->cpu = MACHO_CPU_386;
		mh->subcpu = MACHO_SUBCPU_X86;
		break;
	}

	/* segment for zero page */
	ms = newMachoSeg("__PAGEZERO", 0);
	ms->vsize = va;

	/* text */
	v = rnd(HEADR+segtext.len, INITRND);
	ms = newMachoSeg("__TEXT", 2);
	ms->vaddr = va;
	ms->vsize = v;
	ms->filesize = v;
	ms->prot1 = 7;
	ms->prot2 = 5;

	msect = newMachoSect(ms, "__text");
	msect->addr = INITTEXT;
	msect->size = segtext.sect->len;
	msect->off = INITTEXT - va;
	msect->flag = 0x400;	/* flag - some instructions */
	
	s = lookup(".plt", 0);
	if(s->size > 0) {
		msect = newMachoSect(ms, "__symbol_stub1");
		msect->addr = symaddr(s);
		msect->size = s->size;
		msect->off = ms->fileoffset + msect->addr - ms->vaddr;
		msect->flag = 0x80000408;	/* flag */
		msect->res1 = 0;	/* index into indirect symbol table */
		msect->res2 = 6;	/* size of stubs */
	}

	/* data */
	w = segdata.len;
	ms = newMachoSeg("__DATA", 3);
	ms->vaddr = va+v;
	ms->vsize = w;
	ms->fileoffset = v;
	ms->filesize = segdata.filelen;
	ms->prot1 = 7;
	ms->prot2 = 3;

	msect = newMachoSect(ms, "__data");
	msect->addr = va+v;
	msect->size = symaddr(lookup(".got", 0)) - msect->addr;
	msect->off = v;

	s = lookup(".got", 0);
	if(s->size > 0) {
		msect = newMachoSect(ms, "__nl_symbol_ptr");
		msect->addr = symaddr(s);
		msect->size = s->size;
		msect->off = datoff(msect->addr);
		msect->align = 2;
		msect->flag = 6;	/* section with nonlazy symbol pointers */
		msect->res1 = lookup(".linkedit.plt", 0)->size / 4;	/* offset into indirect symbol table */
	}

	msect = newMachoSect(ms, "__bss");
	msect->addr = va+v+segdata.filelen;
	msect->size = segdata.len - segdata.filelen;
	msect->flag = 1;	/* flag - zero fill */

	switch(thechar) {
	default:
		diag("unknown macho architecture");
		errorexit();
	case '6':
		ml = newMachoLoad(5, 42+2);	/* unix thread */
		ml->data[0] = 4;	/* thread type */
		ml->data[1] = 42;	/* word count */
		ml->data[2+32] = entryvalue();	/* start pc */
		ml->data[2+32+1] = entryvalue()>>16>>16;	// hide >>32 for 8l
		break;
	case '8':
		ml = newMachoLoad(5, 16+2);	/* unix thread */
		ml->data[0] = 1;	/* thread type */
		ml->data[1] = 16;	/* word count */
		ml->data[2+10] = entryvalue();	/* start pc */
		break;
	}

	if(!debug['d']) {
		Sym *s1, *s2, *s3, *s4;

		// must match domacholink below
		s1 = lookup(".dynsym", 0);
		s2 = lookup(".dynstr", 0);
		s3 = lookup(".linkedit.plt", 0);
		s4 = lookup(".linkedit.got", 0);

		ms = newMachoSeg("__LINKEDIT", 0);
		ms->vaddr = va+v+rnd(segdata.len, INITRND);
		ms->vsize = s1->size + s2->size + s3->size + s4->size;
		ms->fileoffset = linkoff;
		ms->filesize = ms->vsize;
		ms->prot1 = 7;
		ms->prot2 = 3;

		ml = newMachoLoad(2, 4);	/* LC_SYMTAB */
		ml->data[0] = linkoff;	/* symoff */
		ml->data[1] = s1->size / (macho64 ? 16 : 12);	/* nsyms */
		ml->data[2] = linkoff + s1->size;	/* stroff */
		ml->data[3] = s2->size;	/* strsize */

		ml = newMachoLoad(11, 18);	/* LC_DYSYMTAB */
		ml->data[0] = 0;	/* ilocalsym */
		ml->data[1] = 0;	/* nlocalsym */
		ml->data[2] = 0;	/* iextdefsym */
		ml->data[3] = ndynexp;	/* nextdefsym */
		ml->data[4] = ndynexp;	/* iundefsym */
		ml->data[5] = (s1->size / (macho64 ? 16 : 12)) - ndynexp;	/* nundefsym */
		ml->data[6] = 0;	/* tocoffset */
		ml->data[7] = 0;	/* ntoc */
		ml->data[8] = 0;	/* modtaboff */
		ml->data[9] = 0;	/* nmodtab */
		ml->data[10] = 0;	/* extrefsymoff */
		ml->data[11] = 0;	/* nextrefsyms */
		ml->data[12] = linkoff + s1->size + s2->size;	/* indirectsymoff */
		ml->data[13] = (s3->size + s4->size) / 4;	/* nindirectsyms */
		ml->data[14] = 0;	/* extreloff */
		ml->data[15] = 0;	/* nextrel */
		ml->data[16] = 0;	/* locreloff */
		ml->data[17] = 0;	/* nlocrel */

		ml = newMachoLoad(14, 6);	/* LC_LOAD_DYLINKER */
		ml->data[0] = 12;	/* offset to string */
		strcpy((char*)&ml->data[1], "/usr/lib/dyld");

		for(i=0; i<ndylib; i++) {
			ml = newMachoLoad(12, 4+(strlen(dylib[i])+1+7)/8*2);	/* LC_LOAD_DYLIB */
			ml->data[0] = 24;	/* offset of string from beginning of load */
			ml->data[1] = 0;	/* time stamp */
			ml->data[2] = 0;	/* version */
			ml->data[3] = 0;	/* compatibility version */
			strcpy((char*)&ml->data[4], dylib[i]);
		}
	}

	if(!debug['s']) {
		Sym *s;

		md = newMachoDebug();
		s = lookup("symtab", 0);
		md->fileoffset = datoff(s->value);
		md->filesize = s->size;

		md = newMachoDebug();
		s = lookup("pclntab", 0);
		md->fileoffset = datoff(s->value);
		md->filesize = s->size;

		dwarfaddmachoheaders();
	}

	a = machowrite();
	if(a > MACHORESERVE)
		diag("MACHORESERVE too small: %d > %d", a, MACHORESERVE);
}

vlong
domacholink(void)
{
	int size;
	Sym *s1, *s2, *s3, *s4;

	// write data that will be linkedit section
	s1 = lookup(".dynsym", 0);
	relocsym(s1);
	s2 = lookup(".dynstr", 0);
	s3 = lookup(".linkedit.plt", 0);
	s4 = lookup(".linkedit.got", 0);

	while(s2->size%4)
		adduint8(s2, 0);
	
	size = s1->size + s2->size + s3->size + s4->size;

	if(size > 0) {
		linkoff = rnd(HEADR+segtext.len, INITRND) + rnd(segdata.filelen, INITRND);
		seek(cout, linkoff, 0);

		ewrite(cout, s1->p, s1->size);
		ewrite(cout, s2->p, s2->size);
		ewrite(cout, s3->p, s3->size);
		ewrite(cout, s4->p, s4->size);
	}

	return rnd(size, INITRND);
}
