// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package gob

import (
	"bytes"
	"io"
	"os"
	"reflect"
	"sync"
)

// An Encoder manages the transmission of type and data information to the
// other side of a connection.
type Encoder struct {
	mutex      sync.Mutex              // each item must be sent atomically
	w          []io.Writer             // where to send the data
	sent       map[reflect.Type]typeId // which types we've already sent
	countState *encoderState           // stage for writing counts
	buf        []byte                  // for collecting the output.
	err        os.Error
}

// NewEncoder returns a new encoder that will transmit on the io.Writer.
func NewEncoder(w io.Writer) *Encoder {
	enc := new(Encoder)
	enc.w = []io.Writer{w}
	enc.sent = make(map[reflect.Type]typeId)
	enc.countState = newEncoderState(enc, new(bytes.Buffer))
	return enc
}

// writer() returns the innermost writer the encoder is using
func (enc *Encoder) writer() io.Writer {
	return enc.w[len(enc.w)-1]
}

// pushWriter adds a writer to the encoder.
func (enc *Encoder) pushWriter(w io.Writer) {
	enc.w = append(enc.w, w)
}

// popWriter pops the innermost writer.
func (enc *Encoder) popWriter() {
	enc.w = enc.w[0 : len(enc.w)-1]
}

func (enc *Encoder) badType(rt reflect.Type) {
	enc.setError(os.ErrorString("gob: can't encode type " + rt.String()))
}

func (enc *Encoder) setError(err os.Error) {
	if enc.err == nil { // remember the first.
		enc.err = err
	}
}

// writeMessage sends the data item preceded by a unsigned count of its length.
func (enc *Encoder) writeMessage(w io.Writer, b *bytes.Buffer) {
	enc.countState.encodeUint(uint64(b.Len()))
	// Build the buffer.
	countLen := enc.countState.b.Len()
	total := countLen + b.Len()
	if total > len(enc.buf) {
		enc.buf = make([]byte, total+1000) // extra for growth
	}
	// Place the length before the data.
	// TODO(r): avoid the extra copy here.
	enc.countState.b.Read(enc.buf[0:countLen])
	// Now the data.
	b.Read(enc.buf[countLen:total])
	// Write the data.
	_, err := w.Write(enc.buf[0:total])
	if err != nil {
		enc.setError(err)
	}
}

// sendActualType sends the requested type, without further investigation, unless
// it's been sent before.
func (enc *Encoder) sendActualType(w io.Writer, state *encoderState, ut *userTypeInfo, actual reflect.Type) (sent bool) {
	if _, alreadySent := enc.sent[actual]; alreadySent {
		return false
	}
	typeLock.Lock()
	info, err := getTypeInfo(ut)
	typeLock.Unlock()
	if err != nil {
		enc.setError(err)
		return
	}
	// Send the pair (-id, type)
	// Id:
	state.encodeInt(-int64(info.id))
	// Type:
	enc.encode(state.b, reflect.NewValue(info.wire), wireTypeUserInfo)
	enc.writeMessage(w, state.b)
	if enc.err != nil {
		return
	}

	// Remember we've sent this type, both what the user gave us and the base type.
	enc.sent[ut.base] = info.id
	if ut.user != ut.base {
		enc.sent[ut.user] = info.id
	}
	// Now send the inner types
	switch st := actual.(type) {
	case *reflect.StructType:
		for i := 0; i < st.NumField(); i++ {
			enc.sendType(w, state, st.Field(i).Type)
		}
	case reflect.ArrayOrSliceType:
		enc.sendType(w, state, st.Elem())
	}
	return true
}

// sendType sends the type info to the other side, if necessary. 
func (enc *Encoder) sendType(w io.Writer, state *encoderState, origt reflect.Type) (sent bool) {
	ut := userType(origt)
	if ut.isGobEncoder {
		// The rules are different: regardless of the underlying type's representation,
		// we need to tell the other side that this exact type is a GobEncoder.
		return enc.sendActualType(w, state, ut, ut.user)
	}

	// It's a concrete value, so drill down to the base type.
	switch rt := ut.base.(type) {
	default:
		// Basic types and interfaces do not need to be described.
		return
	case *reflect.SliceType:
		// If it's []uint8, don't send; it's considered basic.
		if rt.Elem().Kind() == reflect.Uint8 {
			return
		}
		// Otherwise we do send.
		break
	case *reflect.ArrayType:
		// arrays must be sent so we know their lengths and element types.
		break
	case *reflect.MapType:
		// maps must be sent so we know their lengths and key/value types.
		break
	case *reflect.StructType:
		// structs must be sent so we know their fields.
		break
	case *reflect.ChanType, *reflect.FuncType:
		// Probably a bad field in a struct.
		enc.badType(rt)
		return
	}

	return enc.sendActualType(w, state, ut, ut.base)
}

// Encode transmits the data item represented by the empty interface value,
// guaranteeing that all necessary type information has been transmitted first.
func (enc *Encoder) Encode(e interface{}) os.Error {
	return enc.EncodeValue(reflect.NewValue(e))
}

// sendTypeDescriptor makes sure the remote side knows about this type.
// It will send a descriptor if this is the first time the type has been
// sent.
func (enc *Encoder) sendTypeDescriptor(w io.Writer, state *encoderState, ut *userTypeInfo) {
	// Make sure the type is known to the other side.
	// First, have we already sent this type?
	rt := ut.base
	if ut.isGobEncoder {
		rt = ut.user
	}
	if _, alreadySent := enc.sent[rt]; !alreadySent {
		// No, so send it.
		sent := enc.sendType(w, state, rt)
		if enc.err != nil {
			return
		}
		// If the type info has still not been transmitted, it means we have
		// a singleton basic type (int, []byte etc.) at top level.  We don't
		// need to send the type info but we do need to update enc.sent.
		if !sent {
			typeLock.Lock()
			info, err := getTypeInfo(ut)
			typeLock.Unlock()
			if err != nil {
				enc.setError(err)
				return
			}
			enc.sent[rt] = info.id
		}
	}
}

// sendTypeId sends the id, which must have already been defined.
func (enc *Encoder) sendTypeId(state *encoderState, ut *userTypeInfo) {
	// Identify the type of this top-level value.
	state.encodeInt(int64(enc.sent[ut.base]))
}

// EncodeValue transmits the data item represented by the reflection value,
// guaranteeing that all necessary type information has been transmitted first.
func (enc *Encoder) EncodeValue(value reflect.Value) os.Error {
	// Make sure we're single-threaded through here, so multiple
	// goroutines can share an encoder.
	enc.mutex.Lock()
	defer enc.mutex.Unlock()

	// Remove any nested writers remaining due to previous errors.
	enc.w = enc.w[0:1]

	ut, err := validUserType(value.Type())
	if err != nil {
		return err
	}

	enc.err = nil
	state := newEncoderState(enc, new(bytes.Buffer))

	enc.sendTypeDescriptor(enc.writer(), state, ut)
	enc.sendTypeId(state, ut)
	if enc.err != nil {
		return enc.err
	}

	// Encode the object.
	err = enc.encode(state.b, value, ut)
	if err != nil {
		enc.setError(err)
	} else {
		enc.writeMessage(enc.writer(), state.b)
	}

	return enc.err
}
