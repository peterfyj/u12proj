// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package gob

import (
	"bytes"
	"io"
	"math"
	"os"
	"reflect"
	"unsafe"
)

const uint64Size = unsafe.Sizeof(uint64(0))

// encoderState is the global execution state of an instance of the encoder.
// Field numbers are delta encoded and always increase. The field
// number is initialized to -1 so 0 comes out as delta(1). A delta of
// 0 terminates the structure.
type encoderState struct {
	enc      *Encoder
	b        *bytes.Buffer
	sendZero bool                 // encoding an array element or map key/value pair; send zero values
	fieldnum int                  // the last field number written.
	buf      [1 + uint64Size]byte // buffer used by the encoder; here to avoid allocation.
}

func newEncoderState(enc *Encoder, b *bytes.Buffer) *encoderState {
	return &encoderState{enc: enc, b: b}
}

// Unsigned integers have a two-state encoding.  If the number is less
// than 128 (0 through 0x7F), its value is written directly.
// Otherwise the value is written in big-endian byte order preceded
// by the byte length, negated.

// encodeUint writes an encoded unsigned integer to state.b.
func (state *encoderState) encodeUint(x uint64) {
	if x <= 0x7F {
		err := state.b.WriteByte(uint8(x))
		if err != nil {
			error(err)
		}
		return
	}
	var n, m int
	m = uint64Size
	for n = 1; x > 0; n++ {
		state.buf[m] = uint8(x & 0xFF)
		x >>= 8
		m--
	}
	state.buf[m] = uint8(-(n - 1))
	n, err := state.b.Write(state.buf[m : uint64Size+1])
	if err != nil {
		error(err)
	}
}

// encodeInt writes an encoded signed integer to state.w.
// The low bit of the encoding says whether to bit complement the (other bits of the)
// uint to recover the int.
func (state *encoderState) encodeInt(i int64) {
	var x uint64
	if i < 0 {
		x = uint64(^i<<1) | 1
	} else {
		x = uint64(i << 1)
	}
	state.encodeUint(uint64(x))
}

// encOp is the signature of an encoding operator for a given type.
type encOp func(i *encInstr, state *encoderState, p unsafe.Pointer)

// The 'instructions' of the encoding machine
type encInstr struct {
	op     encOp
	field  int     // field number
	indir  int     // how many pointer indirections to reach the value in the struct
	offset uintptr // offset in the structure of the field to encode
}

// update emits a field number and updates the state to record its value for delta encoding.
// If the instruction pointer is nil, it does nothing
func (state *encoderState) update(instr *encInstr) {
	if instr != nil {
		state.encodeUint(uint64(instr.field - state.fieldnum))
		state.fieldnum = instr.field
	}
}

// Each encoder for a composite is responsible for handling any
// indirections associated with the elements of the data structure.
// If any pointer so reached is nil, no bytes are written.  If the
// data item is zero, no bytes are written.  Single values - ints,
// strings etc. - are indirected before calling their encoders.
// Otherwise, the output (for a scalar) is the field number, as an
// encoded integer, followed by the field data in its appropriate
// format.

// encIndirect dereferences p indir times and returns the result.
func encIndirect(p unsafe.Pointer, indir int) unsafe.Pointer {
	for ; indir > 0; indir-- {
		p = *(*unsafe.Pointer)(p)
		if p == nil {
			return unsafe.Pointer(nil)
		}
	}
	return p
}

// encBool encodes the bool with address p as an unsigned 0 or 1.
func encBool(i *encInstr, state *encoderState, p unsafe.Pointer) {
	b := *(*bool)(p)
	if b || state.sendZero {
		state.update(i)
		if b {
			state.encodeUint(1)
		} else {
			state.encodeUint(0)
		}
	}
}

// encInt encodes the int with address p.
func encInt(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := int64(*(*int)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeInt(v)
	}
}

// encUint encodes the uint with address p.
func encUint(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := uint64(*(*uint)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeUint(v)
	}
}

// encInt8 encodes the int8 with address p.
func encInt8(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := int64(*(*int8)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeInt(v)
	}
}

// encUint8 encodes the uint8 with address p.
func encUint8(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := uint64(*(*uint8)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeUint(v)
	}
}

// encInt16 encodes the int16 with address p.
func encInt16(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := int64(*(*int16)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeInt(v)
	}
}

// encUint16 encodes the uint16 with address p.
func encUint16(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := uint64(*(*uint16)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeUint(v)
	}
}

// encInt32 encodes the int32 with address p.
func encInt32(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := int64(*(*int32)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeInt(v)
	}
}

// encUint encodes the uint32 with address p.
func encUint32(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := uint64(*(*uint32)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeUint(v)
	}
}

// encInt64 encodes the int64 with address p.
func encInt64(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := *(*int64)(p)
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeInt(v)
	}
}

// encInt64 encodes the uint64 with address p.
func encUint64(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := *(*uint64)(p)
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeUint(v)
	}
}

// encUintptr encodes the uintptr with address p.
func encUintptr(i *encInstr, state *encoderState, p unsafe.Pointer) {
	v := uint64(*(*uintptr)(p))
	if v != 0 || state.sendZero {
		state.update(i)
		state.encodeUint(v)
	}
}

// floatBits returns a uint64 holding the bits of a floating-point number.
// Floating-point numbers are transmitted as uint64s holding the bits
// of the underlying representation.  They are sent byte-reversed, with
// the exponent end coming out first, so integer floating point numbers
// (for example) transmit more compactly.  This routine does the
// swizzling.
func floatBits(f float64) uint64 {
	u := math.Float64bits(f)
	var v uint64
	for i := 0; i < 8; i++ {
		v <<= 8
		v |= u & 0xFF
		u >>= 8
	}
	return v
}

// encFloat32 encodes the float32 with address p.
func encFloat32(i *encInstr, state *encoderState, p unsafe.Pointer) {
	f := *(*float32)(p)
	if f != 0 || state.sendZero {
		v := floatBits(float64(f))
		state.update(i)
		state.encodeUint(v)
	}
}

// encFloat64 encodes the float64 with address p.
func encFloat64(i *encInstr, state *encoderState, p unsafe.Pointer) {
	f := *(*float64)(p)
	if f != 0 || state.sendZero {
		state.update(i)
		v := floatBits(f)
		state.encodeUint(v)
	}
}

// encComplex64 encodes the complex64 with address p.
// Complex numbers are just a pair of floating-point numbers, real part first.
func encComplex64(i *encInstr, state *encoderState, p unsafe.Pointer) {
	c := *(*complex64)(p)
	if c != 0+0i || state.sendZero {
		rpart := floatBits(float64(real(c)))
		ipart := floatBits(float64(imag(c)))
		state.update(i)
		state.encodeUint(rpart)
		state.encodeUint(ipart)
	}
}

// encComplex128 encodes the complex128 with address p.
func encComplex128(i *encInstr, state *encoderState, p unsafe.Pointer) {
	c := *(*complex128)(p)
	if c != 0+0i || state.sendZero {
		rpart := floatBits(real(c))
		ipart := floatBits(imag(c))
		state.update(i)
		state.encodeUint(rpart)
		state.encodeUint(ipart)
	}
}

// encUint8Array encodes the byte slice whose header has address p.
// Byte arrays are encoded as an unsigned count followed by the raw bytes.
func encUint8Array(i *encInstr, state *encoderState, p unsafe.Pointer) {
	b := *(*[]byte)(p)
	if len(b) > 0 || state.sendZero {
		state.update(i)
		state.encodeUint(uint64(len(b)))
		state.b.Write(b)
	}
}

// encString encodes the string whose header has address p.
// Strings are encoded as an unsigned count followed by the raw bytes.
func encString(i *encInstr, state *encoderState, p unsafe.Pointer) {
	s := *(*string)(p)
	if len(s) > 0 || state.sendZero {
		state.update(i)
		state.encodeUint(uint64(len(s)))
		io.WriteString(state.b, s)
	}
}

// encStructTerminator encodes the end of an encoded struct
// as delta field number of 0.
func encStructTerminator(i *encInstr, state *encoderState, p unsafe.Pointer) {
	state.encodeUint(0)
}

// Execution engine

// encEngine an array of instructions indexed by field number of the encoding
// data, typically a struct.  It is executed top to bottom, walking the struct.
type encEngine struct {
	instr []encInstr
}

const singletonField = 0

// encodeSingle encodes a single top-level non-struct value.
func (enc *Encoder) encodeSingle(b *bytes.Buffer, engine *encEngine, basep uintptr) {
	state := newEncoderState(enc, b)
	state.fieldnum = singletonField
	// There is no surrounding struct to frame the transmission, so we must
	// generate data even if the item is zero.  To do this, set sendZero.
	state.sendZero = true
	instr := &engine.instr[singletonField]
	p := unsafe.Pointer(basep) // offset will be zero
	if instr.indir > 0 {
		if p = encIndirect(p, instr.indir); p == nil {
			return
		}
	}
	instr.op(instr, state, p)
}

// encodeStruct encodes a single struct value.
func (enc *Encoder) encodeStruct(b *bytes.Buffer, engine *encEngine, basep uintptr) {
	state := newEncoderState(enc, b)
	state.fieldnum = -1
	for i := 0; i < len(engine.instr); i++ {
		instr := &engine.instr[i]
		p := unsafe.Pointer(basep + instr.offset)
		if instr.indir > 0 {
			if p = encIndirect(p, instr.indir); p == nil {
				continue
			}
		}
		instr.op(instr, state, p)
	}
}

// encodeArray encodes the array whose 0th element is at p.
func (enc *Encoder) encodeArray(b *bytes.Buffer, p uintptr, op encOp, elemWid uintptr, elemIndir int, length int) {
	state := newEncoderState(enc, b)
	state.fieldnum = -1
	state.sendZero = true
	state.encodeUint(uint64(length))
	for i := 0; i < length; i++ {
		elemp := p
		up := unsafe.Pointer(elemp)
		if elemIndir > 0 {
			if up = encIndirect(up, elemIndir); up == nil {
				errorf("gob: encodeArray: nil element")
			}
			elemp = uintptr(up)
		}
		op(nil, state, unsafe.Pointer(elemp))
		p += uintptr(elemWid)
	}
}

// encodeReflectValue is a helper for maps. It encodes the value v.
func encodeReflectValue(state *encoderState, v reflect.Value, op encOp, indir int) {
	for i := 0; i < indir && v != nil; i++ {
		v = reflect.Indirect(v)
	}
	if v == nil {
		errorf("gob: encodeReflectValue: nil element")
	}
	op(nil, state, unsafe.Pointer(v.UnsafeAddr()))
}

// encodeMap encodes a map as unsigned count followed by key:value pairs.
// Because map internals are not exposed, we must use reflection rather than
// addresses.
func (enc *Encoder) encodeMap(b *bytes.Buffer, mv *reflect.MapValue, keyOp, elemOp encOp, keyIndir, elemIndir int) {
	state := newEncoderState(enc, b)
	state.fieldnum = -1
	state.sendZero = true
	keys := mv.Keys()
	state.encodeUint(uint64(len(keys)))
	for _, key := range keys {
		encodeReflectValue(state, key, keyOp, keyIndir)
		encodeReflectValue(state, mv.Elem(key), elemOp, elemIndir)
	}
}

// encodeInterface encodes the interface value iv.
// To send an interface, we send a string identifying the concrete type, followed
// by the type identifier (which might require defining that type right now), followed
// by the concrete value.  A nil value gets sent as the empty string for the name,
// followed by no value.
func (enc *Encoder) encodeInterface(b *bytes.Buffer, iv *reflect.InterfaceValue) {
	state := newEncoderState(enc, b)
	state.fieldnum = -1
	state.sendZero = true
	if iv.IsNil() {
		state.encodeUint(0)
		return
	}

	ut := userType(iv.Elem().Type())
	name, ok := concreteTypeToName[ut.base]
	if !ok {
		errorf("gob: type not registered for interface: %s", ut.base)
	}
	// Send the name.
	state.encodeUint(uint64(len(name)))
	_, err := io.WriteString(state.b, name)
	if err != nil {
		error(err)
	}
	// Define the type id if necessary.
	enc.sendTypeDescriptor(enc.writer(), state, ut)
	// Send the type id.
	enc.sendTypeId(state, ut)
	// Encode the value into a new buffer.  Any nested type definitions
	// should be written to b, before the encoded value.
	enc.pushWriter(b)
	data := new(bytes.Buffer)
	err = enc.encode(data, iv.Elem(), ut)
	if err != nil {
		error(err)
	}
	enc.popWriter()
	enc.writeMessage(b, data)
	if enc.err != nil {
		error(err)
	}
}

// encGobEncoder encodes a value that implements the GobEncoder interface.
// The data is sent as a byte array.
func (enc *Encoder) encodeGobEncoder(b *bytes.Buffer, v reflect.Value, index int) {
	// TODO: should we catch panics from the called method?
	// We know it's a GobEncoder, so just call the method directly.
	data, err := v.Interface().(GobEncoder).GobEncode()
	if err != nil {
		error(err)
	}
	state := newEncoderState(enc, b)
	state.fieldnum = -1
	state.encodeUint(uint64(len(data)))
	state.b.Write(data)
}

var encOpTable = [...]encOp{
	reflect.Bool:       encBool,
	reflect.Int:        encInt,
	reflect.Int8:       encInt8,
	reflect.Int16:      encInt16,
	reflect.Int32:      encInt32,
	reflect.Int64:      encInt64,
	reflect.Uint:       encUint,
	reflect.Uint8:      encUint8,
	reflect.Uint16:     encUint16,
	reflect.Uint32:     encUint32,
	reflect.Uint64:     encUint64,
	reflect.Uintptr:    encUintptr,
	reflect.Float32:    encFloat32,
	reflect.Float64:    encFloat64,
	reflect.Complex64:  encComplex64,
	reflect.Complex128: encComplex128,
	reflect.String:     encString,
}

// encOpFor returns (a pointer to) the encoding op for the base type under rt and
// the indirection count to reach it.
func (enc *Encoder) encOpFor(rt reflect.Type, inProgress map[reflect.Type]*encOp) (*encOp, int) {
	ut := userType(rt)
	// If the type implements GobEncoder, we handle it without further processing.
	if ut.isGobEncoder {
		return enc.gobEncodeOpFor(ut)
	}
	// If this type is already in progress, it's a recursive type (e.g. map[string]*T).
	// Return the pointer to the op we're already building.
	if opPtr := inProgress[rt]; opPtr != nil {
		return opPtr, ut.indir
	}
	typ := ut.base
	indir := ut.indir
	k := typ.Kind()
	var op encOp
	if int(k) < len(encOpTable) {
		op = encOpTable[k]
	}
	if op == nil {
		inProgress[rt] = &op
		// Special cases
		switch t := typ.(type) {
		case *reflect.SliceType:
			if t.Elem().Kind() == reflect.Uint8 {
				op = encUint8Array
				break
			}
			// Slices have a header; we decode it to find the underlying array.
			elemOp, indir := enc.encOpFor(t.Elem(), inProgress)
			op = func(i *encInstr, state *encoderState, p unsafe.Pointer) {
				slice := (*reflect.SliceHeader)(p)
				if !state.sendZero && slice.Len == 0 {
					return
				}
				state.update(i)
				state.enc.encodeArray(state.b, slice.Data, *elemOp, t.Elem().Size(), indir, int(slice.Len))
			}
		case *reflect.ArrayType:
			// True arrays have size in the type.
			elemOp, indir := enc.encOpFor(t.Elem(), inProgress)
			op = func(i *encInstr, state *encoderState, p unsafe.Pointer) {
				state.update(i)
				state.enc.encodeArray(state.b, uintptr(p), *elemOp, t.Elem().Size(), indir, t.Len())
			}
		case *reflect.MapType:
			keyOp, keyIndir := enc.encOpFor(t.Key(), inProgress)
			elemOp, elemIndir := enc.encOpFor(t.Elem(), inProgress)
			op = func(i *encInstr, state *encoderState, p unsafe.Pointer) {
				// Maps cannot be accessed by moving addresses around the way
				// that slices etc. can.  We must recover a full reflection value for
				// the iteration.
				v := reflect.NewValue(unsafe.Unreflect(t, unsafe.Pointer(p)))
				mv := reflect.Indirect(v).(*reflect.MapValue)
				if !state.sendZero && mv.Len() == 0 {
					return
				}
				state.update(i)
				state.enc.encodeMap(state.b, mv, *keyOp, *elemOp, keyIndir, elemIndir)
			}
		case *reflect.StructType:
			// Generate a closure that calls out to the engine for the nested type.
			enc.getEncEngine(userType(typ))
			info := mustGetTypeInfo(typ)
			op = func(i *encInstr, state *encoderState, p unsafe.Pointer) {
				state.update(i)
				// indirect through info to delay evaluation for recursive structs
				state.enc.encodeStruct(state.b, info.encoder, uintptr(p))
			}
		case *reflect.InterfaceType:
			op = func(i *encInstr, state *encoderState, p unsafe.Pointer) {
				// Interfaces transmit the name and contents of the concrete
				// value they contain.
				v := reflect.NewValue(unsafe.Unreflect(t, unsafe.Pointer(p)))
				iv := reflect.Indirect(v).(*reflect.InterfaceValue)
				if !state.sendZero && (iv == nil || iv.IsNil()) {
					return
				}
				state.update(i)
				state.enc.encodeInterface(state.b, iv)
			}
		}
	}
	if op == nil {
		errorf("gob enc: can't happen: encode type %s", rt.String())
	}
	return &op, indir
}

// methodIndex returns which method of rt implements the method.
func methodIndex(rt reflect.Type, method string) int {
	for i := 0; i < rt.NumMethod(); i++ {
		if rt.Method(i).Name == method {
			return i
		}
	}
	errorf("gob: internal error: can't find method %s", method)
	return 0
}

// gobEncodeOpFor returns the op for a type that is known to implement
// GobEncoder.
func (enc *Encoder) gobEncodeOpFor(ut *userTypeInfo) (*encOp, int) {
	rt := ut.user
	if ut.encIndir == -1 {
		rt = reflect.PtrTo(rt)
	} else if ut.encIndir > 0 {
		for i := int8(0); i < ut.encIndir; i++ {
			rt = rt.(*reflect.PtrType).Elem()
		}
	}
	var op encOp
	op = func(i *encInstr, state *encoderState, p unsafe.Pointer) {
		var v reflect.Value
		if ut.encIndir == -1 {
			// Need to climb up one level to turn value into pointer.
			v = reflect.NewValue(unsafe.Unreflect(rt, unsafe.Pointer(&p)))
		} else {
			v = reflect.NewValue(unsafe.Unreflect(rt, p))
		}
		state.update(i)
		state.enc.encodeGobEncoder(state.b, v, methodIndex(rt, gobEncodeMethodName))
	}
	return &op, int(ut.encIndir) // encIndir: op will get called with p == address of receiver.
}

// compileEnc returns the engine to compile the type.
func (enc *Encoder) compileEnc(ut *userTypeInfo) *encEngine {
	srt, isStruct := ut.base.(*reflect.StructType)
	engine := new(encEngine)
	seen := make(map[reflect.Type]*encOp)
	rt := ut.base
	if ut.isGobEncoder {
		rt = ut.user
	}
	if !ut.isGobEncoder && isStruct {
		for fieldNum, wireFieldNum := 0, 0; fieldNum < srt.NumField(); fieldNum++ {
			f := srt.Field(fieldNum)
			if !isExported(f.Name) {
				continue
			}
			op, indir := enc.encOpFor(f.Type, seen)
			engine.instr = append(engine.instr, encInstr{*op, wireFieldNum, indir, uintptr(f.Offset)})
			wireFieldNum++
		}
		if srt.NumField() > 0 && len(engine.instr) == 0 {
			errorf("gob: type %s has no exported fields", rt)
		}
		engine.instr = append(engine.instr, encInstr{encStructTerminator, 0, 0, 0})
	} else {
		engine.instr = make([]encInstr, 1)
		op, indir := enc.encOpFor(rt, seen)
		engine.instr[0] = encInstr{*op, singletonField, indir, 0} // offset is zero
	}
	return engine
}

// getEncEngine returns the engine to compile the type.
// typeLock must be held (or we're in initialization and guaranteed single-threaded).
func (enc *Encoder) getEncEngine(ut *userTypeInfo) *encEngine {
	info, err1 := getTypeInfo(ut)
	if err1 != nil {
		error(err1)
	}
	if info.encoder == nil {
		// mark this engine as underway before compiling to handle recursive types.
		info.encoder = new(encEngine)
		info.encoder = enc.compileEnc(ut)
	}
	return info.encoder
}

// lockAndGetEncEngine is a function that locks and compiles.
// This lets us hold the lock only while compiling, not when encoding.
func (enc *Encoder) lockAndGetEncEngine(ut *userTypeInfo) *encEngine {
	typeLock.Lock()
	defer typeLock.Unlock()
	return enc.getEncEngine(ut)
}

func (enc *Encoder) encode(b *bytes.Buffer, value reflect.Value, ut *userTypeInfo) (err os.Error) {
	defer catchError(&err)
	engine := enc.lockAndGetEncEngine(ut)
	indir := ut.indir
	if ut.isGobEncoder {
		indir = int(ut.encIndir)
	}
	for i := 0; i < indir; i++ {
		value = reflect.Indirect(value)
	}
	if !ut.isGobEncoder && value.Type().Kind() == reflect.Struct {
		enc.encodeStruct(b, engine, value.UnsafeAddr())
	} else {
		enc.encodeSingle(b, engine, value.UnsafeAddr())
	}
	return nil
}
