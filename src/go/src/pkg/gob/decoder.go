// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package gob

import (
	"bufio"
	"bytes"
	"io"
	"os"
	"reflect"
	"sync"
)

// A Decoder manages the receipt of type and data information read from the
// remote side of a connection.
type Decoder struct {
	mutex        sync.Mutex                              // each item must be received atomically
	r            io.Reader                               // source of the data
	buf          bytes.Buffer                            // buffer for more efficient i/o from r
	wireType     map[typeId]*wireType                    // map from remote ID to local description
	decoderCache map[reflect.Type]map[typeId]**decEngine // cache of compiled engines
	ignorerCache map[typeId]**decEngine                  // ditto for ignored objects
	countState   *decoderState                           // reads counts from wire
	countBuf     []byte                                  // used for decoding integers while parsing messages
	tmp          []byte                                  // temporary storage for i/o; saves reallocating
	err          os.Error
}

// NewDecoder returns a new decoder that reads from the io.Reader.
func NewDecoder(r io.Reader) *Decoder {
	dec := new(Decoder)
	dec.r = bufio.NewReader(r)
	dec.wireType = make(map[typeId]*wireType)
	dec.decoderCache = make(map[reflect.Type]map[typeId]**decEngine)
	dec.ignorerCache = make(map[typeId]**decEngine)
	dec.countBuf = make([]byte, 9) // counts may be uint64s (unlikely!), require 9 bytes

	return dec
}

// recvType loads the definition of a type.
func (dec *Decoder) recvType(id typeId) {
	// Have we already seen this type?  That's an error
	if id < firstUserId || dec.wireType[id] != nil {
		dec.err = os.ErrorString("gob: duplicate type received")
		return
	}

	// Type:
	wire := new(wireType)
	dec.err = dec.decodeValue(tWireType, reflect.NewValue(wire))
	if dec.err != nil {
		return
	}
	// Remember we've seen this type.
	dec.wireType[id] = wire
}

// recvMessage reads the next count-delimited item from the input. It is the converse
// of Encoder.writeMessage. It returns false on EOF or other error reading the message.
func (dec *Decoder) recvMessage() bool {
	// Read a count.
	nbytes, _, err := decodeUintReader(dec.r, dec.countBuf)
	if err != nil {
		dec.err = err
		return false
	}
	dec.readMessage(int(nbytes))
	return dec.err == nil
}

// readMessage reads the next nbytes bytes from the input.
func (dec *Decoder) readMessage(nbytes int) {
	// Allocate the buffer.
	if cap(dec.tmp) < nbytes {
		dec.tmp = make([]byte, nbytes+100) // room to grow
	}
	dec.tmp = dec.tmp[:nbytes]

	// Read the data
	_, dec.err = io.ReadFull(dec.r, dec.tmp)
	if dec.err != nil {
		if dec.err == os.EOF {
			dec.err = io.ErrUnexpectedEOF
		}
		return
	}
	dec.buf.Write(dec.tmp)
}

// toInt turns an encoded uint64 into an int, according to the marshaling rules.
func toInt(x uint64) int64 {
	i := int64(x >> 1)
	if x&1 != 0 {
		i = ^i
	}
	return i
}

func (dec *Decoder) nextInt() int64 {
	n, _, err := decodeUintReader(&dec.buf, dec.countBuf)
	if err != nil {
		dec.err = err
	}
	return toInt(n)
}

func (dec *Decoder) nextUint() uint64 {
	n, _, err := decodeUintReader(&dec.buf, dec.countBuf)
	if err != nil {
		dec.err = err
	}
	return n
}

// decodeTypeSequence parses:
// TypeSequence
//	(TypeDefinition DelimitedTypeDefinition*)?
// and returns the type id of the next value.  It returns -1 at
// EOF.  Upon return, the remainder of dec.buf is the value to be
// decoded.  If this is an interface value, it can be ignored by
// simply resetting that buffer.
func (dec *Decoder) decodeTypeSequence(isInterface bool) typeId {
	for dec.err == nil {
		if dec.buf.Len() == 0 {
			if !dec.recvMessage() {
				break
			}
		}
		// Receive a type id.
		id := typeId(dec.nextInt())
		if id >= 0 {
			// Value follows.
			return id
		}
		// Type definition for (-id) follows.
		dec.recvType(-id)
		// When decoding an interface, after a type there may be a
		// DelimitedValue still in the buffer.  Skip its count.
		// (Alternatively, the buffer is empty and the byte count
		// will be absorbed by recvMessage.)
		if dec.buf.Len() > 0 {
			if !isInterface {
				dec.err = os.ErrorString("extra data in buffer")
				break
			}
			dec.nextUint()
		}
	}
	return -1
}

// Decode reads the next value from the connection and stores
// it in the data represented by the empty interface value.
// If e is nil, the value will be discarded. Otherwise,
// the value underlying e must either be the correct type for the next
// data item received, and must be a pointer.
func (dec *Decoder) Decode(e interface{}) os.Error {
	if e == nil {
		return dec.DecodeValue(nil)
	}
	value := reflect.NewValue(e)
	// If e represents a value as opposed to a pointer, the answer won't
	// get back to the caller.  Make sure it's a pointer.
	if value.Type().Kind() != reflect.Ptr {
		dec.err = os.ErrorString("gob: attempt to decode into a non-pointer")
		return dec.err
	}
	return dec.DecodeValue(value)
}

// DecodeValue reads the next value from the connection and stores
// it in the data represented by the reflection value.
// The value must be the correct type for the next
// data item received, or it may be nil, which means the
// value will be discarded.
func (dec *Decoder) DecodeValue(value reflect.Value) os.Error {
	// Make sure we're single-threaded through here.
	dec.mutex.Lock()
	defer dec.mutex.Unlock()

	dec.buf.Reset() // In case data lingers from previous invocation.
	dec.err = nil
	id := dec.decodeTypeSequence(false)
	if dec.err == nil {
		dec.err = dec.decodeValue(id, value)
	}
	return dec.err
}

// If debug.go is compiled into the program , debugFunc prints a human-readable
// representation of the gob data read from r by calling that file's Debug function.
// Otherwise it is nil.
var debugFunc func(io.Reader)
