// Inferno utils/5l/asm.c
// http://code.google.com/p/inferno-os/source/browse/utils/5l/asm.c
//
//	Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
//	Portions Copyright © 1995-1997 C H Forsyth (forsyth@terzarima.net)
//	Portions Copyright © 1997-1999 Vita Nuova Limited
//	Portions Copyright © 2000-2007 Vita Nuova Holdings Limited (www.vitanuova.com)
//	Portions Copyright © 2004,2006 Bruce Ellis
//	Portions Copyright © 2005-2007 C H Forsyth (forsyth@terzarima.net)
//	Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
//	Portions Copyright © 2009 The Go Authors.  All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Writing object files.

#include	"l.h"
#include	"../ld/lib.h"
#include	"../ld/elf.h"

int32	OFFSET;

static Prog *PP;

char linuxdynld[] = "/lib/ld-linux.so.2";

int32
entryvalue(void)
{
	char *a;
	Sym *s;

	a = INITENTRY;
	if(*a >= '0' && *a <= '9')
		return atolwhex(a);
	s = lookup(a, 0);
	if(s->type == 0)
		return INITTEXT;
	if(s->type != STEXT)
		diag("entry not text: %s", s->name);
	return s->value;
}

enum {
	ElfStrEmpty,
	ElfStrInterp,
	ElfStrHash,
	ElfStrGot,
	ElfStrGotPlt,
	ElfStrDynamic,
	ElfStrDynsym,
	ElfStrDynstr,
	ElfStrRel,
	ElfStrText,
	ElfStrData,
	ElfStrBss,
	ElfStrGosymcounts,
	ElfStrGosymtab,
	ElfStrGopclntab,
	ElfStrShstrtab,
	ElfStrRelPlt,
	ElfStrPlt,
	NElfStr
};

vlong elfstr[NElfStr];

static int
needlib(char *name)
{
	char *p;
	Sym *s;

	/* reuse hash code in symbol table */
	p = smprint(".dynlib.%s", name);
	s = lookup(p, 0);
	if(s->type == 0) {
		s->type = 100;	// avoid SDATA, etc.
		return 1;
	}
	return 0;
}

int	nelfsym = 1;

void
adddynrel(Sym *s, Reloc *r)
{
	diag("adddynrel: unsupported binary format");
}

void
adddynsym(Sym *s)
{
	diag("adddynsym: not implemented");
}

static void
elfsetupplt(void)
{
	// TODO
}

int
archreloc(Reloc *r, Sym *s, vlong *val)
{
	return -1;
}

void
adddynlib(char *lib)
{
	Sym *s;
	
	if(!needlib(lib))
		return;
	
	if(iself) {
		s = lookup(".dynstr", 0);
		if(s->size == 0)
			addstring(s, "");
		elfwritedynent(lookup(".dynamic", 0), DT_NEEDED, addstring(s, lib));
	} else {
		diag("adddynlib: unsupported binary format");
	}
}

void
doelf(void)
{
	Sym *s, *shstrtab, *dynstr;

	if(!iself)
		return;

	/* predefine strings we need for section headers */
	shstrtab = lookup(".shstrtab", 0);
	shstrtab->type = SELFDATA;
	shstrtab->reachable = 1;

	elfstr[ElfStrEmpty] = addstring(shstrtab, "");
	elfstr[ElfStrText] = addstring(shstrtab, ".text");
	elfstr[ElfStrData] = addstring(shstrtab, ".data");
	addstring(shstrtab, ".rodata");
	elfstr[ElfStrBss] = addstring(shstrtab, ".bss");
	if(!debug['s']) {	
		elfstr[ElfStrGosymcounts] = addstring(shstrtab, ".gosymcounts");
		elfstr[ElfStrGosymtab] = addstring(shstrtab, ".gosymtab");
		elfstr[ElfStrGopclntab] = addstring(shstrtab, ".gopclntab");
	}
	elfstr[ElfStrShstrtab] = addstring(shstrtab, ".shstrtab");

	if(!debug['d']) {	/* -d suppresses dynamic loader format */
		elfstr[ElfStrInterp] = addstring(shstrtab, ".interp");
		elfstr[ElfStrHash] = addstring(shstrtab, ".hash");
		elfstr[ElfStrGot] = addstring(shstrtab, ".got");
		elfstr[ElfStrGotPlt] = addstring(shstrtab, ".got.plt");
		elfstr[ElfStrDynamic] = addstring(shstrtab, ".dynamic");
		elfstr[ElfStrDynsym] = addstring(shstrtab, ".dynsym");
		elfstr[ElfStrDynstr] = addstring(shstrtab, ".dynstr");
		elfstr[ElfStrRel] = addstring(shstrtab, ".rel");
		elfstr[ElfStrRelPlt] = addstring(shstrtab, ".rel.plt");
		elfstr[ElfStrPlt] = addstring(shstrtab, ".plt");

		/* interpreter string */
		s = lookup(".interp", 0);
		s->reachable = 1;
		s->type = SELFDATA;	// TODO: rodata

		/* dynamic symbol table - first entry all zeros */
		s = lookup(".dynsym", 0);
		s->type = SELFDATA;
		s->reachable = 1;
		s->value += ELF32SYMSIZE;

		/* dynamic string table */
		s = lookup(".dynstr", 0);
		s->type = SELFDATA;
		s->reachable = 1;
		if(s->size == 0)
			addstring(s, "");
		dynstr = s;

		/* relocation table */
		s = lookup(".rel", 0);
		s->reachable = 1;
		s->type = SELFDATA;

		/* global offset table */
		s = lookup(".got", 0);
		s->reachable = 1;
		s->type = SELFDATA;
		
		/* hash */
		s = lookup(".hash", 0);
		s->reachable = 1;
		s->type = SELFDATA;

		/* got.plt */
		s = lookup(".got.plt", 0);
		s->reachable = 1;
		s->type = SDATA;	// writable, so not SELFDATA
		
		s = lookup(".plt", 0);
		s->reachable = 1;
		s->type = SELFDATA;

		s = lookup(".rel.plt", 0);
		s->reachable = 1;
		s->type = SELFDATA;
		
		elfsetupplt();

		/* define dynamic elf table */
		s = lookup(".dynamic", 0);
		s->reachable = 1;
		s->type = SELFDATA;

		/*
		 * .dynamic table
		 */
		elfwritedynentsym(s, DT_HASH, lookup(".hash", 0));
		elfwritedynentsym(s, DT_SYMTAB, lookup(".dynsym", 0));
		elfwritedynent(s, DT_SYMENT, ELF32SYMSIZE);
		elfwritedynentsym(s, DT_STRTAB, lookup(".dynstr", 0));
		elfwritedynentsymsize(s, DT_STRSZ, lookup(".dynstr", 0));
		elfwritedynentsym(s, DT_REL, lookup(".rel", 0));
		elfwritedynentsymsize(s, DT_RELSZ, lookup(".rel", 0));
		elfwritedynent(s, DT_RELENT, ELF32RELSIZE);
		if(rpath)
			elfwritedynent(s, DT_RUNPATH, addstring(dynstr, rpath));
		elfwritedynentsym(s, DT_PLTGOT, lookup(".got.plt", 0));
		elfwritedynent(s, DT_PLTREL, DT_REL);
		elfwritedynentsymsize(s, DT_PLTRELSZ, lookup(".rel.plt", 0));
		elfwritedynentsym(s, DT_JMPREL, lookup(".rel.plt", 0));
		elfwritedynent(s, DT_NULL, 0);
	}
}

vlong
datoff(vlong addr)
{
	if(addr >= segdata.vaddr)
		return addr - segdata.vaddr + segdata.fileoff;
	if(addr >= segtext.vaddr)
		return addr - segtext.vaddr + segtext.fileoff;
	diag("datoff %#x", addr);
	return 0;
}

void
shsym(Elf64_Shdr *sh, Sym *s)
{
	sh->addr = symaddr(s);
	sh->off = datoff(sh->addr);
	sh->size = s->size;
}

void
phsh(Elf64_Phdr *ph, Elf64_Shdr *sh)
{
	ph->vaddr = sh->addr;
	ph->paddr = ph->vaddr;
	ph->off = sh->off;
	ph->filesz = sh->size;
	ph->memsz = sh->size;
	ph->align = sh->addralign;
}

void
asmb(void)
{
	int32 t;
	int a, dynsym;
	uint32 va, fo, w, startva;
	int strtabsize;
	ElfEhdr *eh;
	ElfPhdr *ph, *pph;
	ElfShdr *sh;
	Section *sect;

	strtabsize = 0;

	if(debug['v'])
		Bprint(&bso, "%5.2f asmb\n", cputime());
	Bflush(&bso);

	sect = segtext.sect;
	seek(cout, sect->vaddr - segtext.vaddr + segtext.fileoff, 0);
	codeblk(sect->vaddr, sect->len);

	/* output read-only data in text segment */
	sect = segtext.sect->next;
	seek(cout, sect->vaddr - segtext.vaddr + segtext.fileoff, 0);
	datblk(sect->vaddr, sect->len);

	if(debug['v'])
		Bprint(&bso, "%5.2f datblk\n", cputime());
	Bflush(&bso);

	seek(cout, segdata.fileoff, 0);
	datblk(segdata.vaddr, segdata.filelen);

	/* output read-only data in text segment */
	sect = segtext.sect->next;
	seek(cout, sect->vaddr - segtext.vaddr + segtext.fileoff, 0);
	datblk(sect->vaddr, sect->len);

	/* output symbol table */
	symsize = 0;
	lcsize = 0;
	if(!debug['s']) {
		// TODO: rationalize
		if(debug['v'])
			Bprint(&bso, "%5.2f sym\n", cputime());
		Bflush(&bso);
		switch(HEADTYPE) {
		case Hnoheader:
		case Hrisc:
		case Hixp1200:
		case Hipaq:
			debug['s'] = 1;
			break;
		case Hplan9x32:
			OFFSET = HEADR+textsize+segdata.filelen;
			seek(cout, OFFSET, 0);
			break;
		case Hnetbsd:
			OFFSET += rnd(segdata.filelen, 4096);
			seek(cout, OFFSET, 0);
			break;
		case Hlinux:
			OFFSET += segdata.filelen;
			seek(cout, rnd(OFFSET, INITRND), 0);
			break;
		}
		if(!debug['s'])
			asmthumbmap();
		cflush();
	}

	cursym = nil;
	if(debug['v'])
		Bprint(&bso, "%5.2f header\n", cputime());
	Bflush(&bso);
	OFFSET = 0;
	seek(cout, OFFSET, 0);
	switch(HEADTYPE) {
	case Hnoheader:	/* no header */
		break;
	case Hrisc:	/* aif for risc os */
		lputl(0xe1a00000);		/* NOP - decompress code */
		lputl(0xe1a00000);		/* NOP - relocation code */
		lputl(0xeb000000 + 12);		/* BL - zero init code */
		lputl(0xeb000000 +
			(entryvalue()
			 - INITTEXT
			 + HEADR
			 - 12
			 - 8) / 4);		/* BL - entry code */

		lputl(0xef000011);		/* SWI - exit code */
		lputl(textsize+HEADR);		/* text size */
		lputl(segdata.filelen);			/* data size */
		lputl(0);			/* sym size */

		lputl(segdata.len - segdata.filelen);			/* bss size */
		lputl(0);			/* sym type */
		lputl(INITTEXT-HEADR);		/* text addr */
		lputl(0);			/* workspace - ignored */

		lputl(32);			/* addr mode / data addr flag */
		lputl(0);			/* data addr */
		for(t=0; t<2; t++)
			lputl(0);		/* reserved */

		for(t=0; t<15; t++)
			lputl(0xe1a00000);	/* NOP - zero init code */
		lputl(0xe1a0f00e);		/* B (R14) - zero init return */
		break;
	case Hplan9x32:	/* plan 9 */
		lput(0x647);			/* magic */
		lput(textsize);			/* sizes */
		lput(segdata.filelen);
		lput(segdata.len - segdata.filelen);
		lput(symsize);			/* nsyms */
		lput(entryvalue());		/* va of entry */
		lput(0L);
		lput(lcsize);
		break;
	case Hnetbsd:	/* boot for NetBSD */
		lput((143<<16)|0413);		/* magic */
		lputl(rnd(HEADR+textsize, 4096));
		lputl(rnd(segdata.filelen, 4096));
		lputl(segdata.len - segdata.filelen);
		lputl(symsize);			/* nsyms */
		lputl(entryvalue());		/* va of entry */
		lputl(0L);
		lputl(0L);
		break;
	case Hixp1200: /* boot for IXP1200 */
		break;
	case Hipaq: /* boot for ipaq */
		lputl(0xe3300000);		/* nop */
		lputl(0xe3300000);		/* nop */
		lputl(0xe3300000);		/* nop */
		lputl(0xe3300000);		/* nop */
		break;
	case Hlinux:
		/* elf arm */
		eh = getElfEhdr();
		fo = HEADR;
		va = INITTEXT;
		startva = INITTEXT - fo;	/* va of byte 0 of file */
		w = textsize;
		
		/* This null SHdr must appear before all others */
		sh = newElfShdr(elfstr[ElfStrEmpty]);

		/* program header info */
		pph = newElfPhdr();
		pph->type = PT_PHDR;
		pph->flags = PF_R + PF_X;
		pph->off = eh->ehsize;
		pph->vaddr = INITTEXT - HEADR + pph->off;
		pph->paddr = INITTEXT - HEADR + pph->off;
		pph->align = INITRND;

		if(!debug['d']) {
			/* interpreter for dynamic linking */
			sh = newElfShdr(elfstr[ElfStrInterp]);
			sh->type = SHT_PROGBITS;
			sh->flags = SHF_ALLOC;
			sh->addralign = 1;
			if(interpreter == nil)
				interpreter = linuxdynld;
			elfinterp(sh, startva, interpreter);

			ph = newElfPhdr();
			ph->type = PT_INTERP;
			ph->flags = PF_R;
			phsh(ph, sh);
		}

		elfphload(&segtext);
		elfphload(&segdata);

		/* Dynamic linking sections */
		if (!debug['d']) {	/* -d suppresses dynamic loader format */
			/* S headers for dynamic linking */
			sh = newElfShdr(elfstr[ElfStrGot]);
			sh->type = SHT_PROGBITS;
			sh->flags = SHF_ALLOC+SHF_WRITE;
			sh->entsize = 4;
			sh->addralign = 4;
			shsym(sh, lookup(".got", 0));

			sh = newElfShdr(elfstr[ElfStrGotPlt]);
			sh->type = SHT_PROGBITS;
			sh->flags = SHF_ALLOC+SHF_WRITE;
			sh->entsize = 4;
			sh->addralign = 4;
			shsym(sh, lookup(".got.plt", 0));

			dynsym = eh->shnum;
			sh = newElfShdr(elfstr[ElfStrDynsym]);
			sh->type = SHT_DYNSYM;
			sh->flags = SHF_ALLOC;
			sh->entsize = ELF32SYMSIZE;
			sh->addralign = 4;
			sh->link = dynsym+1;	// dynstr
			// sh->info = index of first non-local symbol (number of local symbols)
			shsym(sh, lookup(".dynsym", 0));

			sh = newElfShdr(elfstr[ElfStrDynstr]);
			sh->type = SHT_STRTAB;
			sh->flags = SHF_ALLOC;
			sh->addralign = 1;
			shsym(sh, lookup(".dynstr", 0));

			sh = newElfShdr(elfstr[ElfStrHash]);
			sh->type = SHT_HASH;
			sh->flags = SHF_ALLOC;
			sh->entsize = 4;
			sh->addralign = 4;
			sh->link = dynsym;
			shsym(sh, lookup(".hash", 0));

			sh = newElfShdr(elfstr[ElfStrRel]);
			sh->type = SHT_REL;
			sh->flags = SHF_ALLOC;
			sh->entsize = ELF32RELSIZE;
			sh->addralign = 4;
			sh->link = dynsym;
			shsym(sh, lookup(".rel", 0));

			/* sh and PT_DYNAMIC for .dynamic section */
			sh = newElfShdr(elfstr[ElfStrDynamic]);
			sh->type = SHT_DYNAMIC;
			sh->flags = SHF_ALLOC+SHF_WRITE;
			sh->entsize = 8;
			sh->addralign = 4;
			sh->link = dynsym+1;	// dynstr
			shsym(sh, lookup(".dynamic", 0));

			ph = newElfPhdr();
			ph->type = PT_DYNAMIC;
			ph->flags = PF_R + PF_W;
			phsh(ph, sh);

			/*
			 * Thread-local storage segment (really just size).
			if(tlsoffset != 0) {
				ph = newElfPhdr();
				ph->type = PT_TLS;
				ph->flags = PF_R;
				ph->memsz = -tlsoffset;
				ph->align = 4;
			}
			 */
		}

		ph = newElfPhdr();
		ph->type = PT_GNU_STACK;
		ph->flags = PF_W+PF_R;
		ph->align = 4;

		for(sect=segtext.sect; sect!=nil; sect=sect->next)
			elfshbits(sect);
		for(sect=segdata.sect; sect!=nil; sect=sect->next)
			elfshbits(sect);

		if (!debug['s']) {
			sh = newElfShdr(elfstr[ElfStrGosymtab]);
			sh->type = SHT_PROGBITS;
			sh->flags = SHF_ALLOC;
			sh->addralign = 1;
			shsym(sh, lookup("symtab", 0));

			sh = newElfShdr(elfstr[ElfStrGopclntab]);
			sh->type = SHT_PROGBITS;
			sh->flags = SHF_ALLOC;
			sh->addralign = 1;
			shsym(sh, lookup("pclntab", 0));
		}

		sh = newElfShstrtab(elfstr[ElfStrShstrtab]);
		sh->type = SHT_STRTAB;
		sh->addralign = 1;
		shsym(sh, lookup(".shstrtab", 0));

		/* Main header */
		eh->ident[EI_MAG0] = '\177';
		eh->ident[EI_MAG1] = 'E';
		eh->ident[EI_MAG2] = 'L';
		eh->ident[EI_MAG3] = 'F';
		eh->ident[EI_CLASS] = ELFCLASS32;
		eh->ident[EI_DATA] = ELFDATA2LSB;
		eh->ident[EI_VERSION] = EV_CURRENT;

		eh->type = ET_EXEC;
		eh->machine = EM_ARM;
		eh->version = EV_CURRENT;
		eh->entry = entryvalue();

		if(pph != nil) {
			pph->filesz = eh->phnum * eh->phentsize;
			pph->memsz = pph->filesz;
		}

		seek(cout, 0, 0);
		a = 0;
		a += elfwritehdr();
		a += elfwritephdrs();
		a += elfwriteshdrs();
		cflush();
		if(a+elfwriteinterp() > ELFRESERVE)
			diag("ELFRESERVE too small: %d > %d", a, ELFRESERVE);
		break;
	}
	cflush();
	if(debug['c']){
		print("textsize=%d\n", textsize);
		print("datsize=%d\n", segdata.filelen);
		print("bsssize=%d\n", segdata.len - segdata.filelen);
		print("symsize=%d\n", symsize);
		print("lcsize=%d\n", lcsize);
		print("total=%d\n", textsize+segdata.len+symsize+lcsize);
	}
}

void
cput(int c)
{
	cbp[0] = c;
	cbp++;
	cbc--;
	if(cbc <= 0)
		cflush();
}

/*
void
cput(int32 c)
{
	*cbp++ = c;
	if(--cbc <= 0)
		cflush();
}
*/

void
wput(int32 l)
{

	cbp[0] = l>>8;
	cbp[1] = l;
	cbp += 2;
	cbc -= 2;
	if(cbc <= 0)
		cflush();
}


void
hput(int32 l)
{

	cbp[0] = l>>8;
	cbp[1] = l;
	cbp += 2;
	cbc -= 2;
	if(cbc <= 0)
		cflush();
}

void
lput(int32 l)
{

	cbp[0] = l>>24;
	cbp[1] = l>>16;
	cbp[2] = l>>8;
	cbp[3] = l;
	cbp += 4;
	cbc -= 4;
	if(cbc <= 0)
		cflush();
}

void
cflush(void)
{
	int n;

	/* no bug if cbc < 0 since obuf(cbuf) followed by ibuf in buf! */
	n = sizeof(buf.cbuf) - cbc;
	if(n)
		ewrite(cout, buf.cbuf, n);
	cbp = buf.cbuf;
	cbc = sizeof(buf.cbuf);
}

void
nopstat(char *f, Count *c)
{
	if(c->outof)
	Bprint(&bso, "%s delay %d/%d (%.2f)\n", f,
		c->outof - c->count, c->outof,
		(double)(c->outof - c->count)/c->outof);
}

static void
outt(int32 f, int32 l)
{
	if(debug['L'])
		Bprint(&bso, "tmap: %ux-%ux\n", f, l);
	lput(f);
	lput(l);
}

void
asmthumbmap(void)
{
	int32 pc, lastt;
	Prog *p;

	if(!seenthumb)
		return;
	pc = 0;
	lastt = -1;
	for(cursym = textp; cursym != nil; cursym = cursym->next) {
		p = cursym->text;
		pc = p->pc - INITTEXT;
		setarch(p);
		if(thumb){
			if(p->from.sym->foreign){	// 8 bytes of ARM first
				if(lastt >= 0){
					outt(lastt, pc-1);
					lastt = -1;
				}
				pc += 8;
			}
			if(lastt < 0)
				lastt = pc;
		}
		else{
			if(p->from.sym->foreign){	// 4 bytes of THUMB first
				if(lastt < 0)
					lastt = pc;
				pc += 4;
			}
			if(lastt >= 0){
				outt(lastt, pc-1);
				lastt = -1;
			}
		}
		if(cursym->next == nil)
			for(; p != P; p = p->link)
				pc = p->pc = INITTEXT;
	}
	if(lastt >= 0)
		outt(lastt, pc+1);
}

void
asmout(Prog *p, Optab *o, int32 *out)
{
	int32 o1, o2, o3, o4, o5, o6, v;
	int r, rf, rt, rt2;
	Reloc *rel;

PP = p;
	o1 = 0;
	o2 = 0;
	o3 = 0;
	o4 = 0;
	o5 = 0;
	o6 = 0;
	armsize += o->size;
if(debug['P']) print("%ux: %P	type %d\n", (uint32)(p->pc), p, o->type);
	switch(o->type) {
	default:
		diag("unknown asm %d", o->type);
		prasm(p);
		break;

	case 0:		/* pseudo ops */
if(debug['G']) print("%ux: %s: arm %d %d %d\n", (uint32)(p->pc), p->from.sym->name, p->from.sym->thumb, p->from.sym->foreign, p->from.sym->fnptr);
		break;

	case 1:		/* op R,[R],R */
		o1 = oprrr(p->as, p->scond);
		rf = p->from.reg;
		rt = p->to.reg;
		r = p->reg;
		if(p->to.type == D_NONE)
			rt = 0;
		if(p->as == AMOVW || p->as == AMVN)
			r = 0;
		else
		if(r == NREG)
			r = rt;
		o1 |= rf | (r<<16) | (rt<<12);
		break;

	case 2:		/* movbu $I,[R],R */
		aclass(&p->from);
		o1 = oprrr(p->as, p->scond);
		o1 |= immrot(instoffset);
		rt = p->to.reg;
		r = p->reg;
		if(p->to.type == D_NONE)
			rt = 0;
		if(p->as == AMOVW || p->as == AMVN)
			r = 0;
		else if(r == NREG)
			r = rt;
		o1 |= (r<<16) | (rt<<12);
		break;

	case 3:		/* add R<<[IR],[R],R */
	mov:
		aclass(&p->from);
		o1 = oprrr(p->as, p->scond);
		o1 |= p->from.offset;
		rt = p->to.reg;
		r = p->reg;
		if(p->to.type == D_NONE)
			rt = 0;
		if(p->as == AMOVW || p->as == AMVN)
			r = 0;
		else if(r == NREG)
			r = rt;
		o1 |= (r<<16) | (rt<<12);
		break;

	case 4:		/* add $I,[R],R */
		aclass(&p->from);
		o1 = oprrr(AADD, p->scond);
		o1 |= immrot(instoffset);
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o1 |= r << 16;
		o1 |= p->to.reg << 12;
		break;

	case 5:		/* bra s */
		v = -8;
		if(p->cond != P)
			v = (p->cond->pc - pc) - 8;
#ifdef CALLEEBX
		if(p->as == ABL)
			v += fninc(p->to.sym);
#endif
		o1 = opbra(p->as, p->scond);
		o1 |= (v >> 2) & 0xffffff;
		break;

	case 6:		/* b ,O(R) -> add $O,R,PC */
		aclass(&p->to);
		o1 = oprrr(AADD, p->scond);
		o1 |= immrot(instoffset);
		o1 |= p->to.reg << 16;
		o1 |= REGPC << 12;
		break;

	case 7:		/* bl ,O(R) -> mov PC,link; add $O,R,PC */
		aclass(&p->to);
		o1 = oprrr(AADD, p->scond);
		o1 |= immrot(0);
		o1 |= REGPC << 16;
		o1 |= REGLINK << 12;

		o2 = oprrr(AADD, p->scond);
		o2 |= immrot(instoffset);
		o2 |= p->to.reg << 16;
		o2 |= REGPC << 12;
		break;

	case 8:		/* sll $c,[R],R -> mov (R<<$c),R */
		aclass(&p->from);
		o1 = oprrr(p->as, p->scond);
		r = p->reg;
		if(r == NREG)
			r = p->to.reg;
		o1 |= r;
		o1 |= (instoffset&31) << 7;
		o1 |= p->to.reg << 12;
		break;

	case 9:		/* sll R,[R],R -> mov (R<<R),R */
		o1 = oprrr(p->as, p->scond);
		r = p->reg;
		if(r == NREG)
			r = p->to.reg;
		o1 |= r;
		o1 |= (p->from.reg << 8) | (1<<4);
		o1 |= p->to.reg << 12;
		break;

	case 10:	/* swi [$con] */
		o1 = oprrr(p->as, p->scond);
		if(p->to.type != D_NONE) {
			aclass(&p->to);
			o1 |= instoffset & 0xffffff;
		}
		break;

	case 11:	/* word */
		aclass(&p->to);
		o1 = instoffset;
		if(p->to.sym != S) {
			rel = addrel(cursym);
			rel->off = pc - cursym->value;
			rel->siz = 4;
			rel->type = D_ADDR;
			rel->sym = p->to.sym;
			rel->add = p->to.offset;
			o1 = 0;
		}
		break;

	case 12:	/* movw $lcon, reg */
		o1 = omvl(p, &p->from, p->to.reg);
		break;

	case 13:	/* op $lcon, [R], R */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;
		o2 = oprrr(p->as, p->scond);
		o2 |= REGTMP;
		r = p->reg;
		if(p->as == AMOVW || p->as == AMVN)
			r = 0;
		else if(r == NREG)
			r = p->to.reg;
		o2 |= r << 16;
		if(p->to.type != D_NONE)
			o2 |= p->to.reg << 12;
		break;

	case 14:	/* movb/movbu/movh/movhu R,R */
		o1 = oprrr(ASLL, p->scond);

		if(p->as == AMOVBU || p->as == AMOVHU)
			o2 = oprrr(ASRL, p->scond);
		else
			o2 = oprrr(ASRA, p->scond);

		r = p->to.reg;
		o1 |= (p->from.reg)|(r<<12);
		o2 |= (r)|(r<<12);
		if(p->as == AMOVB || p->as == AMOVBU) {
			o1 |= (24<<7);
			o2 |= (24<<7);
		} else {
			o1 |= (16<<7);
			o2 |= (16<<7);
		}
		break;

	case 15:	/* mul r,[r,]r */
		o1 = oprrr(p->as, p->scond);
		rf = p->from.reg;
		rt = p->to.reg;
		r = p->reg;
		if(r == NREG)
			r = rt;
		if(rt == r) {
			r = rf;
			rf = rt;
		}
		if(0)
		if(rt == r || rf == REGPC || r == REGPC || rt == REGPC) {
			diag("bad registers in MUL");
			prasm(p);
		}
		o1 |= (rf<<8) | r | (rt<<16);
		break;


	case 16:	/* div r,[r,]r */
		o1 = 0xf << 28;
		o2 = 0;
		break;

	case 17:
		o1 = oprrr(p->as, p->scond);
		rf = p->from.reg;
		rt = p->to.reg;
		rt2 = p->to.offset;
		r = p->reg;
		o1 |= (rf<<8) | r | (rt<<16) | (rt2<<12);
		break;

	case 20:	/* mov/movb/movbu R,O(R) */
		aclass(&p->to);
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o1 = osr(p->as, p->from.reg, instoffset, r, p->scond);
		break;

	case 21:	/* mov/movbu O(R),R -> lr */
		aclass(&p->from);
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o1 = olr(instoffset, r, p->to.reg, p->scond);
		if(p->as != AMOVW)
			o1 |= 1<<22;
		break;

	case 22:	/* movb/movh/movhu O(R),R -> lr,shl,shr */
		aclass(&p->from);
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o1 = olr(instoffset, r, p->to.reg, p->scond);

		o2 = oprrr(ASLL, p->scond);
		o3 = oprrr(ASRA, p->scond);
		r = p->to.reg;
		if(p->as == AMOVB) {
			o2 |= (24<<7)|(r)|(r<<12);
			o3 |= (24<<7)|(r)|(r<<12);
		} else {
			o2 |= (16<<7)|(r)|(r<<12);
			if(p->as == AMOVHU)
				o3 = oprrr(ASRL, p->scond);
			o3 |= (16<<7)|(r)|(r<<12);
		}
		break;

	case 23:	/* movh/movhu R,O(R) -> sb,sb */
		aclass(&p->to);
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o1 = osr(AMOVH, p->from.reg, instoffset, r, p->scond);

		o2 = oprrr(ASRL, p->scond);
		o2 |= (8<<7)|(p->from.reg)|(REGTMP<<12);

		o3 = osr(AMOVH, REGTMP, instoffset+1, r, p->scond);
		break;

	case 30:	/* mov/movb/movbu R,L(R) */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o2 = osrr(p->from.reg, REGTMP,r, p->scond);
		if(p->as != AMOVW)
			o2 |= 1<<22;
		break;

	case 31:	/* mov/movbu L(R),R -> lr[b] */
	case 32:	/* movh/movb L(R),R -> lr[b] */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o2 = olrr(REGTMP,r, p->to.reg, p->scond);
		if(p->as == AMOVBU || p->as == AMOVB)
			o2 |= 1<<22;
		if(o->type == 31)
			break;

		o3 = oprrr(ASLL, p->scond);

		if(p->as == AMOVBU || p->as == AMOVHU)
			o4 = oprrr(ASRL, p->scond);
		else
			o4 = oprrr(ASRA, p->scond);

		r = p->to.reg;
		o3 |= (r)|(r<<12);
		o4 |= (r)|(r<<12);
		if(p->as == AMOVB || p->as == AMOVBU) {
			o3 |= (24<<7);
			o4 |= (24<<7);
		} else {
			o3 |= (16<<7);
			o4 |= (16<<7);
		}
		break;

	case 33:	/* movh/movhu R,L(R) -> sb, sb */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o2 = osrr(p->from.reg, REGTMP, r, p->scond);
		o2 |= (1<<22) ;

		o3 = oprrr(ASRL, p->scond);
		o3 |= (8<<7)|(p->from.reg)|(p->from.reg<<12);
		o3 |= (1<<6);	/* ROR 8 */

		o4 = oprrr(AADD, p->scond);
		o4 |= (REGTMP << 12) | (REGTMP << 16);
		o4 |= immrot(1);

		o5 = osrr(p->from.reg, REGTMP,r,p->scond);
		o5 |= (1<<22);

		o6 = oprrr(ASRL, p->scond);
		o6 |= (24<<7)|(p->from.reg)|(p->from.reg<<12);
		o6 |= (1<<6);	/* ROL 8 */

		break;

	case 34:	/* mov $lacon,R */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;

		o2 = oprrr(AADD, p->scond);
		o2 |= REGTMP;
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o2 |= r << 16;
		if(p->to.type != D_NONE)
			o2 |= p->to.reg << 12;
		break;

	case 35:	/* mov PSR,R */
		o1 = (2<<23) | (0xf<<16) | (0<<0);
		o1 |= (p->scond & C_SCOND) << 28;
		o1 |= (p->from.reg & 1) << 22;
		o1 |= p->to.reg << 12;
		break;

	case 36:	/* mov R,PSR */
		o1 = (2<<23) | (0x29f<<12) | (0<<4);
		if(p->scond & C_FBIT)
			o1 ^= 0x010 << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		o1 |= (p->to.reg & 1) << 22;
		o1 |= p->from.reg << 0;
		break;

	case 37:	/* mov $con,PSR */
		aclass(&p->from);
		o1 = (2<<23) | (0x29f<<12) | (0<<4);
		if(p->scond & C_FBIT)
			o1 ^= 0x010 << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		o1 |= immrot(instoffset);
		o1 |= (p->to.reg & 1) << 22;
		o1 |= p->from.reg << 0;
		break;

	case 38:	/* movm $con,oreg -> stm */
		o1 = (0x4 << 25);
		o1 |= p->from.offset & 0xffff;
		o1 |= p->to.reg << 16;
		aclass(&p->to);
		goto movm;

	case 39:	/* movm oreg,$con -> ldm */
		o1 = (0x4 << 25) | (1 << 20);
		o1 |= p->to.offset & 0xffff;
		o1 |= p->from.reg << 16;
		aclass(&p->from);
	movm:
		if(instoffset != 0)
			diag("offset must be zero in MOVM");
		o1 |= (p->scond & C_SCOND) << 28;
		if(p->scond & C_PBIT)
			o1 |= 1 << 24;
		if(p->scond & C_UBIT)
			o1 |= 1 << 23;
		if(p->scond & C_SBIT)
			o1 |= 1 << 22;
		if(p->scond & C_WBIT)
			o1 |= 1 << 21;
		break;

	case 40:	/* swp oreg,reg,reg */
		aclass(&p->from);
		if(instoffset != 0)
			diag("offset must be zero in SWP");
		o1 = (0x2<<23) | (0x9<<4);
		if(p->as != ASWPW)
			o1 |= 1 << 22;
		o1 |= p->from.reg << 16;
		o1 |= p->reg << 0;
		o1 |= p->to.reg << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		break;

	case 41:	/* rfe -> movm.s.w.u 0(r13),[r15] */
		o1 = 0xe8fd8000;
		break;

	case 50:	/* floating point store */
		v = regoff(&p->to);
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o1 = ofsr(p->as, p->from.reg, v, r, p->scond, p);
		break;

	case 51:	/* floating point load */
		v = regoff(&p->from);
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o1 = ofsr(p->as, p->to.reg, v, r, p->scond, p) | (1<<20);
		break;

	case 52:	/* floating point store, int32 offset UGLY */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o2 = oprrr(AADD, p->scond) | (REGTMP << 12) | (REGTMP << 16) | r;
		o3 = ofsr(p->as, p->from.reg, 0, REGTMP, p->scond, p);
		break;

	case 53:	/* floating point load, int32 offset UGLY */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o2 = oprrr(AADD, p->scond) | (REGTMP << 12) | (REGTMP << 16) | r;
		o3 = ofsr(p->as, p->to.reg, 0, REGTMP, p->scond, p) | (1<<20);
		break;

	case 54:	/* floating point arith */
		o1 = oprrr(p->as, p->scond);
		rf = p->from.reg;
		rt = p->to.reg;
		r = p->reg;
		if(r == NREG) {
			r = rt;
			if(p->as == AMOVF || p->as == AMOVD)
				r = 0;
		}
		o1 |= rf | (r<<16) | (rt<<12);
		break;

	case 56:	/* move to FP[CS]R */
		o1 = ((p->scond & C_SCOND) << 28) | (0xe << 24) | (1<<8) | (1<<4);
		o1 |= ((p->to.reg+1)<<21) | (p->from.reg << 12);
		break;

	case 57:	/* move from FP[CS]R */
		o1 = ((p->scond & C_SCOND) << 28) | (0xe << 24) | (1<<8) | (1<<4);
		o1 |= ((p->from.reg+1)<<21) | (p->to.reg<<12) | (1<<20);
		break;
	case 58:	/* movbu R,R */
		o1 = oprrr(AAND, p->scond);
		o1 |= immrot(0xff);
		rt = p->to.reg;
		r = p->from.reg;
		if(p->to.type == D_NONE)
			rt = 0;
		if(r == NREG)
			r = rt;
		o1 |= (r<<16) | (rt<<12);
		break;

	case 59:	/* movw/bu R<<I(R),R -> ldr indexed */
		if(p->from.reg == NREG) {
			if(p->as != AMOVW)
				diag("byte MOV from shifter operand");
			goto mov;
		}
		if(p->from.offset&(1<<4))
			diag("bad shift in LDR");
		o1 = olrr(p->from.offset, p->from.reg, p->to.reg, p->scond);
		if(p->as == AMOVBU)
			o1 |= 1<<22;
		break;

	case 60:	/* movb R(R),R -> ldrsb indexed */
		if(p->from.reg == NREG) {
			diag("byte MOV from shifter operand");
			goto mov;
		}
		if(p->from.offset&(~0xf))
			diag("bad shift in LDRSB");
		o1 = olhrr(p->from.offset, p->from.reg, p->to.reg, p->scond);
		o1 ^= (1<<5)|(1<<6);
		break;

	case 61:	/* movw/b/bu R,R<<[IR](R) -> str indexed */
		if(p->to.reg == NREG)
			diag("MOV to shifter operand");
		o1 = osrr(p->from.reg, p->to.offset, p->to.reg, p->scond);
		if(p->as == AMOVB || p->as == AMOVBU)
			o1 |= 1<<22;
		break;

	case 62:	/* case R -> movw	R<<2(PC),PC */
		o1 = olrr(p->from.reg, REGPC, REGPC, p->scond);
		o1 |= 2<<7;
		break;

	case 63:	/* bcase */
		if(p->cond != P)
			o1 = p->cond->pc;
		break;

	/* reloc ops */
	case 64:	/* mov/movb/movbu R,addr */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		o2 = osr(p->as, p->from.reg, 0, REGTMP, p->scond);
		break;

	case 65:	/* mov/movbu addr,R */
	case 66:	/* movh/movhu/movb addr,R */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;
		o2 = olr(0, REGTMP, p->to.reg, p->scond);
		if(p->as == AMOVBU || p->as == AMOVB)
			o2 |= 1<<22;
		if(o->type == 65)
			break;

		o3 = oprrr(ASLL, p->scond);

		if(p->as == AMOVBU || p->as == AMOVHU)
			o4 = oprrr(ASRL, p->scond);
		else
			o4 = oprrr(ASRA, p->scond);

		r = p->to.reg;
		o3 |= (r)|(r<<12);
		o4 |= (r)|(r<<12);
		if(p->as == AMOVB || p->as == AMOVBU) {
			o3 |= (24<<7);
			o4 |= (24<<7);
		} else {
			o3 |= (16<<7);
			o4 |= (16<<7);
		}
		break;

	case 67:	/* movh/movhu R,addr -> sb, sb */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		o2 = osr(p->as, p->from.reg, 0, REGTMP, p->scond);

		o3 = oprrr(ASRL, p->scond);
		o3 |= (8<<7)|(p->from.reg)|(p->from.reg<<12);
		o3 |= (1<<6);	/* ROR 8 */

		o4 = oprrr(AADD, p->scond);
		o4 |= (REGTMP << 12) | (REGTMP << 16);
		o4 |= immrot(1);

		o5 = osr(p->as, p->from.reg, 0, REGTMP, p->scond);

		o6 = oprrr(ASRL, p->scond);
		o6 |= (24<<7)|(p->from.reg)|(p->from.reg<<12);
		o6 |= (1<<6);	/* ROL 8 */
		break;

	case 68:	/* floating point store -> ADDR */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		o2 = ofsr(p->as, p->from.reg, 0, REGTMP, p->scond, p);
		break;

	case 69:	/* floating point load <- ADDR */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;
		o2 = ofsr(p->as, p->to.reg, 0, REGTMP, p->scond, p) | (1<<20);
		break;

	/* ArmV4 ops: */
	case 70:	/* movh/movhu R,O(R) -> strh */
		aclass(&p->to);
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o1 = oshr(p->from.reg, instoffset, r, p->scond);
		break;
	case 71:	/* movb/movh/movhu O(R),R -> ldrsb/ldrsh/ldrh */
		aclass(&p->from);
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o1 = olhr(instoffset, r, p->to.reg, p->scond);
		if(p->as == AMOVB)
			o1 ^= (1<<5)|(1<<6);
		else if(p->as == AMOVH)
			o1 ^= (1<<6);
		break;
	case 72:	/* movh/movhu R,L(R) -> strh */
		o1 = omvl(p, &p->to, REGTMP);
		if(!o1)
			break;
		r = p->to.reg;
		if(r == NREG)
			r = o->param;
		o2 = oshrr(p->from.reg, REGTMP,r, p->scond);
		break;
	case 73:	/* movb/movh/movhu L(R),R -> ldrsb/ldrsh/ldrh */
		o1 = omvl(p, &p->from, REGTMP);
		if(!o1)
			break;
		r = p->from.reg;
		if(r == NREG)
			r = o->param;
		o2 = olhrr(REGTMP, r, p->to.reg, p->scond);
		if(p->as == AMOVB)
			o2 ^= (1<<5)|(1<<6);
		else if(p->as == AMOVH)
			o2 ^= (1<<6);
		break;
	case 74:	/* bx $I */
#ifdef CALLEEBX
		diag("bx $i case (arm)");
#endif
		if(!seenthumb)
			diag("ABX $I and seenthumb==0");
		v = p->cond->pc;
		if(p->to.sym->thumb)
			v |= 1;	// T bit
		o1 = olr(8, REGPC, REGTMP, p->scond&C_SCOND);	// mov 8(PC), Rtmp
		o2 = oprrr(AADD, p->scond) | immrot(8) | (REGPC<<16) | (REGLINK<<12);	// add 8,PC, LR
		o3 = ((p->scond&C_SCOND)<<28) | (0x12fff<<8) | (1<<4) | REGTMP;		// bx Rtmp
		o4 = opbra(AB, 14);	// B over o6
		o5 = v;
		break;
	case 75:	/* bx O(R) */
		aclass(&p->to);
		if(instoffset != 0)
			diag("non-zero offset in ABX");
/*
		o1 = 	oprrr(AADD, p->scond) | immrot(0) | (REGPC<<16) | (REGLINK<<12);	// mov PC, LR
		o2 = ((p->scond&C_SCOND)<<28) | (0x12fff<<8) | (1<<4) | p->to.reg;		// BX R
*/
		// p->to.reg may be REGLINK
		o1 = oprrr(AADD, p->scond);
		o1 |= immrot(instoffset);
		o1 |= p->to.reg << 16;
		o1 |= REGTMP << 12;
		o2 = oprrr(AADD, p->scond) | immrot(0) | (REGPC<<16) | (REGLINK<<12);	// mov PC, LR
		o3 = ((p->scond&C_SCOND)<<28) | (0x12fff<<8) | (1<<4) | REGTMP;		// BX Rtmp
		break;
	case 76:	/* bx O(R) when returning from fn*/
		if(!seenthumb)
			diag("ABXRET and seenthumb==0");
		aclass(&p->to);
// print("ARM BXRET %d(R%d)\n", instoffset, p->to.reg);
		if(instoffset != 0)
			diag("non-zero offset in ABXRET");
		// o1 = olr(instoffset, p->to.reg, REGTMP, p->scond);	// mov O(R), Rtmp
		o1 = ((p->scond&C_SCOND)<<28) | (0x12fff<<8) | (1<<4) | p->to.reg;		// BX R
		break;
	case 77:	/* ldrex oreg,reg */
		aclass(&p->from);
		if(instoffset != 0)
			diag("offset must be zero in LDREX");
		o1 = (0x19<<20) | (0xf9f);
		o1 |= p->from.reg << 16;
		o1 |= p->to.reg << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		break;
	case 78:	/* strex reg,oreg,reg */
		aclass(&p->from);
		if(instoffset != 0)
			diag("offset must be zero in STREX");
		o1 = (0x18<<20) | (0xf90);
		o1 |= p->from.reg << 16;
		o1 |= p->reg << 0;
		o1 |= p->to.reg << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		break;
	case 80:	/* fmov zfcon,freg */
		if(p->as == AMOVD) {
			o1 = 0xeeb00b00;	// VMOV imm 64
			o2 = oprrr(ASUBD, p->scond);
		} else {
			o1 = 0x0eb00a00;	// VMOV imm 32
			o2 = oprrr(ASUBF, p->scond);
		}
		v = 0x70;	// 1.0
		r = p->to.reg;

		// movf $1.0, r
		o1 |= (p->scond & C_SCOND) << 28;
		o1 |= r << 12;
		o1 |= (v&0xf) << 0;
		o1 |= (v&0xf0) << 12;

		// subf r,r,r
		o2 |= r | (r<<16) | (r<<12);
		break;
	case 81:	/* fmov sfcon,freg */
		o1 = 0x0eb00a00;		// VMOV imm 32
		if(p->as == AMOVD)
			o1 = 0xeeb00b00;	// VMOV imm 64
		o1 |= (p->scond & C_SCOND) << 28;
		o1 |= p->to.reg << 12;
		v = chipfloat(&p->from.ieee);
		o1 |= (v&0xf) << 0;
		o1 |= (v&0xf0) << 12;
		break;
	case 82:	/* fcmp freg,freg, */
		o1 = oprrr(p->as, p->scond);
		o1 |= (p->reg<<12) | (p->from.reg<<0);
		o2 = 0x0ef1fa10;	// VMRS R15
		o2 |= (p->scond & C_SCOND) << 28;
		break;
	case 83:	/* fcmp freg,, */
		o1 = oprrr(p->as, p->scond);
		o1 |= (p->from.reg<<12) | (1<<16);
		o2 = 0x0ef1fa10;	// VMRS R15
		o2 |= (p->scond & C_SCOND) << 28;
		break;
	case 84:	/* movfw freg,freg - truncate float-to-fix */
		o1 = oprrr(p->as, p->scond);
		o1 |= (p->from.reg<<0);
		o1 |= (p->to.reg<<12);
		break;
	case 85:	/* movwf freg,freg - fix-to-float */
		o1 = oprrr(p->as, p->scond);
		o1 |= (p->from.reg<<0);
		o1 |= (p->to.reg<<12);
		break;
	case 86:	/* movfw freg,reg - truncate float-to-fix */
		// macro for movfw freg,FTMP; movw FTMP,reg
		o1 = oprrr(p->as, p->scond);
		o1 |= (p->from.reg<<0);
		o1 |= (FREGTMP<<12);
		o2 = oprrr(AMOVFW+AEND, p->scond);
		o2 |= (FREGTMP<<16);
		o2 |= (p->to.reg<<12);
		break;
	case 87:	/* movwf reg,freg - fix-to-float */
		// macro for movw reg,FTMP; movwf FTMP,freg
		o1 = oprrr(AMOVWF+AEND, p->scond);
		o1 |= (p->from.reg<<12);
		o1 |= (FREGTMP<<16);
		o2 = oprrr(p->as, p->scond);
		o2 |= (FREGTMP<<0);
		o2 |= (p->to.reg<<12);
		break;
	case 88:	/* movw reg,freg  */
		o1 = oprrr(AMOVWF+AEND, p->scond);
		o1 |= (p->from.reg<<12);
		o1 |= (p->to.reg<<16);
		break;
	case 89:	/* movw freg,reg  */
		o1 = oprrr(AMOVFW+AEND, p->scond);
		o1 |= (p->from.reg<<16);
		o1 |= (p->to.reg<<12);
		break;
	case 90:	/* tst reg  */
		o1 = oprrr(ACMP+AEND, p->scond);
		o1 |= p->from.reg<<16;
		break;
	case 91:	/* ldrexd oreg,reg */
		aclass(&p->from);
		if(instoffset != 0)
			diag("offset must be zero in LDREX");
		o1 = (0x1b<<20) | (0xf9f);
		o1 |= p->from.reg << 16;
		o1 |= p->to.reg << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		break;
	case 92:	/* strexd reg,oreg,reg */
		aclass(&p->from);
		if(instoffset != 0)
			diag("offset must be zero in STREX");
		o1 = (0x1a<<20) | (0xf90);
		o1 |= p->from.reg << 16;
		o1 |= p->reg << 0;
		o1 |= p->to.reg << 12;
		o1 |= (p->scond & C_SCOND) << 28;
		break;
	}
	
	out[0] = o1;
	out[1] = o2;
	out[2] = o3;
	out[3] = o4;
	out[4] = o5;
	out[5] = o6;
	return;

	v = p->pc;
	switch(o->size) {
	default:
		if(debug['a'])
			Bprint(&bso, " %.8ux:\t\t%P\n", v, p);
		break;
	case 4:
		if(debug['a'])
			Bprint(&bso, " %.8ux: %.8ux\t%P\n", v, o1, p);
		lputl(o1);
		break;
	case 8:
		if(debug['a'])
			Bprint(&bso, " %.8ux: %.8ux %.8ux%P\n", v, o1, o2, p);
		lputl(o1);
		lputl(o2);
		break;
	case 12:
		if(debug['a'])
			Bprint(&bso, " %.8ux: %.8ux %.8ux %.8ux%P\n", v, o1, o2, o3, p);
		lputl(o1);
		lputl(o2);
		lputl(o3);
		break;
	case 16:
		if(debug['a'])
			Bprint(&bso, " %.8ux: %.8ux %.8ux %.8ux %.8ux%P\n",
				v, o1, o2, o3, o4, p);
		lputl(o1);
		lputl(o2);
		lputl(o3);
		lputl(o4);
		break;
	case 20:
		if(debug['a'])
			Bprint(&bso, " %.8ux: %.8ux %.8ux %.8ux %.8ux %.8ux%P\n",
				v, o1, o2, o3, o4, o5, p);
		lputl(o1);
		lputl(o2);
		lputl(o3);
		lputl(o4);
		lputl(o5);
		break;
	case 24:
		if(debug['a'])
			Bprint(&bso, " %.8ux: %.8ux %.8ux %.8ux %.8ux %.8ux %.8ux%P\n",
				v, o1, o2, o3, o4, o5, o6, p);
		lputl(o1);
		lputl(o2);
		lputl(o3);
		lputl(o4);
		lputl(o5);
		lputl(o6);
		break;
	}
}

int32
oprrr(int a, int sc)
{
	int32 o;

	o = (sc & C_SCOND) << 28;
	if(sc & C_SBIT)
		o |= 1 << 20;
	if(sc & (C_PBIT|C_WBIT))
		diag(".P/.W on dp instruction");
	switch(a) {
	case AMULU:
	case AMUL:	return o | (0x0<<21) | (0x9<<4);
	case AMULA:	return o | (0x1<<21) | (0x9<<4);
	case AMULLU:	return o | (0x4<<21) | (0x9<<4);
	case AMULL:	return o | (0x6<<21) | (0x9<<4);
	case AMULALU:	return o | (0x5<<21) | (0x9<<4);
	case AMULAL:	return o | (0x7<<21) | (0x9<<4);
	case AAND:	return o | (0x0<<21);
	case AEOR:	return o | (0x1<<21);
	case ASUB:	return o | (0x2<<21);
	case ARSB:	return o | (0x3<<21);
	case AADD:	return o | (0x4<<21);
	case AADC:	return o | (0x5<<21);
	case ASBC:	return o | (0x6<<21);
	case ARSC:	return o | (0x7<<21);
	case ATST:	return o | (0x8<<21) | (1<<20);
	case ATEQ:	return o | (0x9<<21) | (1<<20);
	case ACMP:	return o | (0xa<<21) | (1<<20);
	case ACMN:	return o | (0xb<<21) | (1<<20);
	case AORR:	return o | (0xc<<21);
	case AMOVW:	return o | (0xd<<21);
	case ABIC:	return o | (0xe<<21);
	case AMVN:	return o | (0xf<<21);
	case ASLL:	return o | (0xd<<21) | (0<<5);
	case ASRL:	return o | (0xd<<21) | (1<<5);
	case ASRA:	return o | (0xd<<21) | (2<<5);
	case ASWI:	return o | (0xf<<24);

	case AADDD:	return o | (0xe<<24) | (0x3<<20) | (0xb<<8) | (0<<4);
	case AADDF:	return o | (0xe<<24) | (0x3<<20) | (0xa<<8) | (0<<4);
	case ASUBD:	return o | (0xe<<24) | (0x3<<20) | (0xb<<8) | (4<<4);
	case ASUBF:	return o | (0xe<<24) | (0x3<<20) | (0xa<<8) | (4<<4);
	case AMULD:	return o | (0xe<<24) | (0x2<<20) | (0xb<<8) | (0<<4);
	case AMULF:	return o | (0xe<<24) | (0x2<<20) | (0xa<<8) | (0<<4);
	case ADIVD:	return o | (0xe<<24) | (0x8<<20) | (0xb<<8) | (0<<4);
	case ADIVF:	return o | (0xe<<24) | (0x8<<20) | (0xa<<8) | (0<<4);
	case ACMPD:	return o | (0xe<<24) | (0xb<<20) | (4<<16) | (0xb<<8) | (0xc<<4);
	case ACMPF:	return o | (0xe<<24) | (0xb<<20) | (4<<16) | (0xa<<8) | (0xc<<4);

	case AMOVF:	return o | (0xe<<24) | (0xb<<20) | (0<<16) | (0xa<<8) | (4<<4);
	case AMOVD:	return o | (0xe<<24) | (0xb<<20) | (0<<16) | (0xb<<8) | (4<<4);

	case AMOVDF:	return o | (0xe<<24) | (0xb<<20) | (7<<16) | (0xa<<8) | (0xc<<4) |
			(1<<8);	// dtof
	case AMOVFD:	return o | (0xe<<24) | (0xb<<20) | (7<<16) | (0xa<<8) | (0xc<<4) |
			(0<<8);	// dtof

	case AMOVWF:
			if((sc & C_UBIT) == 0)
				o |= 1<<7;	/* signed */
			return o | (0xe<<24) | (0xb<<20) | (8<<16) | (0xa<<8) | (4<<4) |
				(0<<18) | (0<<8);	// toint, double
	case AMOVWD:
			if((sc & C_UBIT) == 0)
				o |= 1<<7;	/* signed */
			return o | (0xe<<24) | (0xb<<20) | (8<<16) | (0xa<<8) | (4<<4) |
				(0<<18) | (1<<8);	// toint, double

	case AMOVFW:
			if((sc & C_UBIT) == 0)
				o |= 1<<16;	/* signed */
			return o | (0xe<<24) | (0xb<<20) | (8<<16) | (0xa<<8) | (4<<4) |
				(1<<18) | (0<<8) | (1<<7);	// toint, double, trunc
	case AMOVDW:
			if((sc & C_UBIT) == 0)
				o |= 1<<16;	/* signed */
			return o | (0xe<<24) | (0xb<<20) | (8<<16) | (0xa<<8) | (4<<4) |
				(1<<18) | (1<<8) | (1<<7);	// toint, double, trunc

	case AMOVWF+AEND:	// copy WtoF
		return o | (0xe<<24) | (0x0<<20) | (0xb<<8) | (1<<4);
	case AMOVFW+AEND:	// copy FtoW
		return o | (0xe<<24) | (0x1<<20) | (0xb<<8) | (1<<4);
	case ACMP+AEND:	// cmp imm
		return o | (0x3<<24) | (0x5<<20);
	}
	diag("bad rrr %d", a);
	prasm(curp);
	return 0;
}

int32
opbra(int a, int sc)
{

	if(sc & (C_SBIT|C_PBIT|C_WBIT))
		diag(".S/.P/.W on bra instruction");
	sc &= C_SCOND;
	if(a == ABL)
		return (sc<<28)|(0x5<<25)|(0x1<<24);
	if(sc != 0xe)
		diag(".COND on bcond instruction");
	switch(a) {
	case ABEQ:	return (0x0<<28)|(0x5<<25);
	case ABNE:	return (0x1<<28)|(0x5<<25);
	case ABCS:	return (0x2<<28)|(0x5<<25);
	case ABHS:	return (0x2<<28)|(0x5<<25);
	case ABCC:	return (0x3<<28)|(0x5<<25);
	case ABLO:	return (0x3<<28)|(0x5<<25);
	case ABMI:	return (0x4<<28)|(0x5<<25);
	case ABPL:	return (0x5<<28)|(0x5<<25);
	case ABVS:	return (0x6<<28)|(0x5<<25);
	case ABVC:	return (0x7<<28)|(0x5<<25);
	case ABHI:	return (0x8<<28)|(0x5<<25);
	case ABLS:	return (0x9<<28)|(0x5<<25);
	case ABGE:	return (0xa<<28)|(0x5<<25);
	case ABLT:	return (0xb<<28)|(0x5<<25);
	case ABGT:	return (0xc<<28)|(0x5<<25);
	case ABLE:	return (0xd<<28)|(0x5<<25);
	case AB:	return (0xe<<28)|(0x5<<25);
	}
	diag("bad bra %A", a);
	prasm(curp);
	return 0;
}

int32
olr(int32 v, int b, int r, int sc)
{
	int32 o;

	if(sc & C_SBIT)
		diag(".S on LDR/STR instruction");
	o = (sc & C_SCOND) << 28;
	if(!(sc & C_PBIT))
		o |= 1 << 24;
	if(!(sc & C_UBIT))
		o |= 1 << 23;
	if(sc & C_WBIT)
		o |= 1 << 21;
	o |= (1<<26) | (1<<20);
	if(v < 0) {
		if(sc & C_UBIT) diag(".U on neg offset");
		v = -v;
		o ^= 1 << 23;
	}
	if(v >= (1<<12) || v < 0)
		diag("literal span too large: %d (R%d)\n%P", v, b, PP);
	o |= v;
	o |= b << 16;
	o |= r << 12;
	return o;
}

int32
olhr(int32 v, int b, int r, int sc)
{
	int32 o;

	if(sc & C_SBIT)
		diag(".S on LDRH/STRH instruction");
	o = (sc & C_SCOND) << 28;
	if(!(sc & C_PBIT))
		o |= 1 << 24;
	if(sc & C_WBIT)
		o |= 1 << 21;
	o |= (1<<23) | (1<<20)|(0xb<<4);
	if(v < 0) {
		v = -v;
		o ^= 1 << 23;
	}
	if(v >= (1<<8) || v < 0)
		diag("literal span too large: %d (R%d)\n%P", v, b, PP);
	o |= (v&0xf)|((v>>4)<<8)|(1<<22);
	o |= b << 16;
	o |= r << 12;
	return o;
}

int32
osr(int a, int r, int32 v, int b, int sc)
{
	int32 o;

	o = olr(v, b, r, sc) ^ (1<<20);
	if(a != AMOVW)
		o |= 1<<22;
	return o;
}

int32
oshr(int r, int32 v, int b, int sc)
{
	int32 o;

	o = olhr(v, b, r, sc) ^ (1<<20);
	return o;
}


int32
osrr(int r, int i, int b, int sc)
{

	return olr(i, b, r, sc) ^ ((1<<25) | (1<<20));
}

int32
oshrr(int r, int i, int b, int sc)
{
	return olhr(i, b, r, sc) ^ ((1<<22) | (1<<20));
}

int32
olrr(int i, int b, int r, int sc)
{

	return olr(i, b, r, sc) ^ (1<<25);
}

int32
olhrr(int i, int b, int r, int sc)
{
	return olhr(i, b, r, sc) ^ (1<<22);
}

int32
ofsr(int a, int r, int32 v, int b, int sc, Prog *p)
{
	int32 o;

	if(sc & C_SBIT)
		diag(".S on FLDR/FSTR instruction");
	o = (sc & C_SCOND) << 28;
	if(!(sc & C_PBIT))
		o |= 1 << 24;
	if(sc & C_WBIT)
		o |= 1 << 21;
	o |= (6<<25) | (1<<24) | (1<<23) | (10<<8);
	if(v < 0) {
		v = -v;
		o ^= 1 << 23;
	}
	if(v & 3)
		diag("odd offset for floating point op: %d\n%P", v, p);
	else
	if(v >= (1<<10) || v < 0)
		diag("literal span too large: %d\n%P", v, p);
	o |= (v>>2) & 0xFF;
	o |= b << 16;
	o |= r << 12;

	switch(a) {
	default:
		diag("bad fst %A", a);
	case AMOVD:
		o |= 1 << 8;
	case AMOVF:
		break;
	}
	return o;
}

int32
omvl(Prog *p, Adr *a, int dr)
{
	int32 v, o1;
	if(!p->cond) {
		aclass(a);
		v = immrot(~instoffset);
		if(v == 0) {
			diag("missing literal");
			prasm(p);
			return 0;
		}
		o1 = oprrr(AMVN, p->scond&C_SCOND);
		o1 |= v;
		o1 |= dr << 12;
	} else {
		v = p->cond->pc - p->pc - 8;
		o1 = olr(v, REGPC, dr, p->scond&C_SCOND);
	}
	return o1;
}

int
chipzero(Ieee *e)
{
	if(e->l != 0 || e->h != 0)
		return -1;
	return 0;
}

int
chipfloat(Ieee *e)
{
	int n;
	ulong h;

	if(e->l != 0 || (e->h&0xffff) != 0)
		goto no;
	h = e->h & 0x7fc00000;
	if(h != 0x40000000 && h != 0x3fc00000)
		goto no;
	n = 0;

	// sign bit (a)
	if(e->h & 0x80000000)
		n |= 1<<7;

	// exp sign bit (b)
	if(h == 0x3fc00000)
		n |= 1<<6;

	// rest of exp and mantissa (cd-efgh)
	n |= (e->h >> 16) & 0x3f;

//print("match %.8lux %.8lux %d\n", e->l, e->h, n);
	return n;

no:
	return -1;
}


void
genasmsym(void (*put)(Sym*, char*, int, vlong, vlong, int, Sym*))
{
	Auto *a;
	Sym *s;
	int h;

	s = lookup("etext", 0);
	if(s->type == STEXT)
		put(s, s->name, 'T', s->value, s->size, s->version, 0);

	for(h=0; h<NHASH; h++) {
		for(s=hash[h]; s!=S; s=s->hash) {
			if(s->hide)
				continue;
			switch(s->type) {
			case SCONST:
			case SRODATA:
			case SDATA:
			case SELFDATA:
			case STYPE:
			case SSTRING:
			case SGOSTRING:
				if(!s->reachable)
					continue;
				put(s, s->name, 'D', s->value, s->size, s->version, s->gotype);
				continue;

			case SBSS:
				if(!s->reachable)
					continue;
				put(s, s->name, 'B', s->value, s->size, s->version, s->gotype);
				continue;

			case SFILE:
				put(nil, s->name, 'f', s->value, 0, s->version, 0);
				continue;
			}
		}
	}

	for(s = textp; s != nil; s = s->next) {
		/* filenames first */
		for(a=s->autom; a; a=a->link)
			if(a->type == D_FILE)
				put(nil, a->asym->name, 'z', a->aoffset, 0, 0, 0);
			else
			if(a->type == D_FILE1)
				put(nil, a->asym->name, 'Z', a->aoffset, 0, 0, 0);

		put(s, s->name, 'T', s->value, s->size, s->version, s->gotype);

		/* frame, auto and param after */
		put(nil, ".frame", 'm', s->text->to.offset+4, 0, 0, 0);

		for(a=s->autom; a; a=a->link)
			if(a->type == D_AUTO)
				put(nil, a->asym->name, 'a', -a->aoffset, 0, 0, a->gotype);
			else
			if(a->type == D_PARAM)
				put(nil, a->asym->name, 'p', a->aoffset, 0, 0, a->gotype);
	}
	if(debug['v'] || debug['n'])
		Bprint(&bso, "symsize = %ud\n", symsize);
	Bflush(&bso);
}
