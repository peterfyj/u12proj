# Copyright 2010 The Go Authors. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""GDB Pretty printers and convencience functions for Go's runtime structures.

This script is loaded by GDB when it finds a .debug_gdb_scripts
section in the compiled binary.  The [68]l linkers emit this with a
path to this file based on the path to the runtime package.
"""

# Known issues:
#    - pretty printing only works for the 'native' strings. E.g. 'type
#      foo string' will make foo a plain struct in the eyes of gdb,
#      circumventing the pretty print triggering.


import sys, re

print >>sys.stderr, "Loading Go Runtime support."

# allow to manually reload while developing
goobjfile = gdb.current_objfile() or gdb.objfiles()[0]
goobjfile.pretty_printers = []

#
#  Pretty Printers
#

class StringTypePrinter:
	"Pretty print Go strings."

	pattern = re.compile(r'^struct string$')

	def __init__(self, val):
		self.val = val

	def display_hint(self):
		return 'string'

	def to_string(self):
		l = int(self.val['len'])
		return self.val['str'].string("utf-8", "ignore", l)


class SliceTypePrinter:
	"Pretty print slices."

	pattern = re.compile(r'^struct \[\]')

	def __init__(self, val):
		self.val = val

	def display_hint(self):
		return 'array'

	def to_string(self):
		return str(self.val.type)[6:]  # skip 'struct '

	def children(self):
		ptr = self.val["array"]
		for idx in range(self.val["len"]):
			yield ('[%d]' % idx, (ptr + idx).dereference())


class MapTypePrinter:
	"""Pretty print map[K]V types.

	Map-typed go variables are really pointers. dereference them in gdb
	to inspect their contents with this pretty printer.
	"""

	pattern = re.compile(r'^struct hash<.*>$')

	def __init__(self, val):
		self.val = val

	def display_hint(self):
		return 'map'

	def to_string(self):
		return str(self.val.type)

	def children(self):
		stab = self.val['st']
		i = 0
		for v in self.traverse_hash(stab):
			yield ("[%d]" %  i, v['key'])
			yield ("[%d]" % (i + 1), v['val'])
			i += 2

	def traverse_hash(self, stab):
		ptr = stab['entry'].address
		end = stab['end']
		while ptr < end:
			v = ptr.dereference()
			ptr = ptr + 1
			if v['hash'] == 0: continue
			if v['hash'] & 63 == 63:   # subtable
				for v in self.traverse_hash(v['key'].cast(self.val['st'].type)):
					yield v
			else:
				yield v


class ChanTypePrinter:
	"""Pretty print chan[T] types.

	Chan-typed go variables are really pointers. dereference them in gdb
	to inspect their contents with this pretty printer.
	"""

	pattern = re.compile(r'^struct hchan<.*>$')

	def __init__(self, val):
		self.val = val

	def display_hint(self):
		return 'array'

	def to_string(self):
		return str(self.val.type)

	def children(self):
		ptr = self.val['recvdataq']
		for idx in range(self.val["qcount"]):
			yield ('[%d]' % idx, ptr['elem'])
			ptr = ptr['link']

#
#  Register all the *Printer classes above.
#

def makematcher(klass):
	def matcher(val):
		try:
			if klass.pattern.match(str(val.type)):
				return klass(val)
		except:
			pass
	return matcher

goobjfile.pretty_printers.extend([makematcher(k) for k in vars().values() if hasattr(k, 'pattern')])

#
#  For reference, this is what we're trying to do:
#  eface: p *(*(struct 'runtime.commonType'*)'main.e'->type_->data)->string
#  iface: p *(*(struct 'runtime.commonType'*)'main.s'->tab->Type->data)->string
#
# interface types can't be recognized by their name, instead we check
# if they have the expected fields.  Unfortunately the mapping of
# fields to python attributes in gdb.py isn't complete: you can't test
# for presence other than by trapping.


def is_iface(val):
	try:
		return str(val['tab'].type) == "struct runtime.itab *" \
		      and str(val['data'].type) == "void *"
	except:
		pass

def is_eface(val):
	try:
		return str(val['_type'].type) == "struct runtime._type *" \
		      and str(val['data'].type) == "void *"
	except:
		pass

def lookup_type(name):
	try:
		return gdb.lookup_type(name)
	except:
		pass
	try:
		return gdb.lookup_type('struct ' + name)
	except:
		pass
	try:
		return gdb.lookup_type('struct ' + name[1:]).pointer()
	except:
		pass


def iface_dtype(obj):
	"Decode type of the data field of an eface or iface struct."

	if is_iface(obj):
		go_type_ptr = obj['tab']['_type']
	elif is_eface(obj):
		go_type_ptr = obj['_type']
	else:
		return

	ct = gdb.lookup_type("struct runtime.commonType").pointer()
	dynamic_go_type = go_type_ptr['ptr'].cast(ct).dereference()
	dtype_name = dynamic_go_type['string'].dereference()['str'].string()
	type_size = int(dynamic_go_type['size'])
	uintptr_size = int(dynamic_go_type['size'].type.sizeof)  # size is itself an uintptr
	dynamic_gdb_type = lookup_type(dtype_name)
	if type_size > uintptr_size:
		dynamic_gdb_type = dynamic_gdb_type.pointer()
	return dynamic_gdb_type


class IfacePrinter:
	"""Pretty print interface values

	Casts the data field to the appropriate dynamic type."""

	def __init__(self, val):
		self.val = val

	def display_hint(self):
		return 'string'

	def to_string(self):
		try:
			dtype = iface_dtype(self.val)
		except:
			return "<bad dynamic type>"
		try:
			return self.val['data'].cast(dtype).dereference()
		except:
			pass
		return self.val['data'].cast(dtype)


def ifacematcher(val):
	if is_iface(val) or is_eface(val):
		return IfacePrinter(val)

goobjfile.pretty_printers.append(ifacematcher)

#
#  Convenience Functions
#

class GoLenFunc(gdb.Function):
	"Length of strings, slices, maps or channels"

	how = ((StringTypePrinter, 'len' ),
	       (SliceTypePrinter, 'len'),
	       (MapTypePrinter, 'count'),
	       (ChanTypePrinter, 'qcount'))

	def __init__(self):
		super(GoLenFunc, self).__init__("len")

	def invoke(self, obj):
		typename = str(obj.type)
		for klass, fld in self.how:
			if klass.pattern.match(typename):
				return obj[fld]

class GoCapFunc(gdb.Function):
	"Capacity of slices or channels"

	how = ((SliceTypePrinter, 'cap'),
	       (ChanTypePrinter, 'dataqsiz'))

	def __init__(self):
		super(GoCapFunc, self).__init__("cap")

	def invoke(self, obj):
		typename = str(obj.type)
		for klass, fld in self.how:
			if klass.pattern.match(typename):
				return obj[fld]

class DTypeFunc(gdb.Function):
	"""Cast Interface values to their dynamic type.

	For non-interface types this behaves as the identity operation.
	"""

	def __init__(self):
		super(DTypeFunc, self).__init__("dtype")

	def invoke(self, obj):
		try:
			return obj['data'].cast(iface_dtype(obj))
		except:
			pass
		return obj

#
#  Commands
#

sts = ( 'idle', 'runnable', 'running', 'syscall', 'waiting', 'moribund', 'dead', 'recovery')

def linked_list(ptr, linkfield):
	while ptr:
		yield ptr
		ptr = ptr[linkfield]


class GoroutinesCmd(gdb.Command):
	"List all goroutines."

	def __init__(self):
		super(GoroutinesCmd, self).__init__("info goroutines", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

	def invoke(self, arg, from_tty):
		# args = gdb.string_to_argv(arg)
		vp = gdb.lookup_type('void').pointer()
		for ptr in linked_list(gdb.parse_and_eval("'runtime.allg'"), 'alllink'):
			if ptr['status'] == 6:	# 'gdead'
				continue
			m = ptr['m']
			s = ' '
			if m:
				pc = m['sched']['pc'].cast(vp)
				sp = m['sched']['sp'].cast(vp)
				s = '*'
			else:
				pc = ptr['sched']['pc'].cast(vp)
				sp = ptr['sched']['sp'].cast(vp)
			blk = gdb.block_for_pc(long((pc)))
			print s, ptr['goid'], "%8s" % sts[long((ptr['status']))], blk.function

def find_goroutine(goid):
	vp = gdb.lookup_type('void').pointer()
	for ptr in linked_list(gdb.parse_and_eval("'runtime.allg'"), 'alllink'):
		if ptr['status'] == 6:	# 'gdead'
			continue
		if ptr['goid'] == goid:
			return [(ptr['m'] or ptr)['sched'][x].cast(vp) for x in 'pc', 'sp']
	return None, None


class GoroutineCmd(gdb.Command):
	"""Execute gdb command in the context of goroutine <goid>.

	Switch PC and SP to the ones in the goroutine's G structure,
	execute an arbitrary gdb command, and restore PC and SP.

	Usage: (gdb) goroutine <goid> <gdbcmd>

	Note that it is ill-defined to modify state in the context of a goroutine.
	Restrict yourself to inspecting values.
	"""

	def __init__(self):
		super(GoroutineCmd, self).__init__("goroutine", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

	def invoke(self, arg, from_tty):
		goid, cmd = arg.split(None, 1)
		pc, sp = find_goroutine(int(goid))
		if not pc:
			print "No such goroutine: ", goid
			return
		save_frame = gdb.selected_frame()
		gdb.parse_and_eval('$save_pc = $pc')
		gdb.parse_and_eval('$save_sp = $sp')
		gdb.parse_and_eval('$pc = 0x%x' % long(pc))
		gdb.parse_and_eval('$sp = 0x%x' % long(sp))
		try:
			gdb.execute(cmd)
                finally:
			gdb.parse_and_eval('$pc = $save_pc')
                        gdb.parse_and_eval('$sp = $save_sp')
                        save_frame.select()


class GoIfaceCmd(gdb.Command):
	"Print Static and dynamic interface types"

	def __init__(self):
		super(GoIfaceCmd, self).__init__("iface", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL)

	def invoke(self, arg, from_tty):
		for obj in gdb.string_to_argv(arg):
			try:
				#TODO fix quoting for qualified variable names
				obj = gdb.parse_and_eval("%s" % obj)
			except Exception, e:
				print "Can't parse ", obj, ": ", e
				continue

			dtype = iface_dtype(obj)
			if not dtype:
				print "Not an interface: ", obj.type
				continue

			print "%s: %s" % (obj.type, dtype)

# TODO: print interface's methods and dynamic type's func pointers thereof.
#rsc: "to find the number of entries in the itab's Fn field look at itab.inter->numMethods
#i am sure i have the names wrong but look at the interface type and its method count"
# so Itype will start with a commontype which has kind = interface

#
# Register all convience functions and CLI commands
#
for k in vars().values():
	if hasattr(k, 'invoke'):
		k()
