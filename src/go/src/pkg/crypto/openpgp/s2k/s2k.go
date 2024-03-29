// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// This package implements the various OpenPGP string-to-key transforms as
// specified in RFC 4800 section 3.7.1.
package s2k

import (
	"crypto"
	"crypto/openpgp/error"
	"hash"
	"io"
	"os"
	"strconv"
)

// Simple writes to out the result of computing the Simple S2K function (RFC
// 4880, section 3.7.1.1) using the given hash and input passphrase.
func Simple(out []byte, h hash.Hash, in []byte) {
	Salted(out, h, in, nil)
}

var zero [1]byte

// Salted writes to out the result of computing the Salted S2K function (RFC
// 4880, section 3.7.1.2) using the given hash, input passphrase and salt.
func Salted(out []byte, h hash.Hash, in []byte, salt []byte) {
	done := 0

	for i := 0; done < len(out); i++ {
		h.Reset()
		for j := 0; j < i; j++ {
			h.Write(zero[:])
		}
		h.Write(salt)
		h.Write(in)
		n := copy(out[done:], h.Sum())
		done += n
	}
}

// Iterated writes to out the result of computing the Iterated and Salted S2K
// function (RFC 4880, section 3.7.1.3) using the given hash, input passphrase,
// salt and iteration count.
func Iterated(out []byte, h hash.Hash, in []byte, salt []byte, count int) {
	combined := make([]byte, len(in)+len(salt))
	copy(combined, salt)
	copy(combined[len(salt):], in)

	if count < len(combined) {
		count = len(combined)
	}

	done := 0
	for i := 0; done < len(out); i++ {
		h.Reset()
		for j := 0; j < i; j++ {
			h.Write(zero[:])
		}
		written := 0
		for written < count {
			if written+len(combined) > count {
				todo := count - written
				h.Write(combined[:todo])
				written = count
			} else {
				h.Write(combined)
				written += len(combined)
			}
		}
		n := copy(out[done:], h.Sum())
		done += n
	}
}

// Parse reads a binary specification for a string-to-key transformation from r
// and returns a function which performs that transform.
func Parse(r io.Reader) (f func(out, in []byte), err os.Error) {
	var buf [9]byte

	_, err = io.ReadFull(r, buf[:2])
	if err != nil {
		return
	}

	hash, ok := HashIdToHash(buf[1])
	if !ok {
		return nil, error.UnsupportedError("hash for S2K function: " + strconv.Itoa(int(buf[1])))
	}
	h := hash.New()
	if h == nil {
		return nil, error.UnsupportedError("hash not availible: " + strconv.Itoa(int(hash)))
	}

	switch buf[0] {
	case 1:
		f := func(out, in []byte) {
			Simple(out, h, in)
		}
		return f, nil
	case 2:
		_, err := io.ReadFull(r, buf[:8])
		if err != nil {
			return
		}
		f := func(out, in []byte) {
			Salted(out, h, in, buf[:8])
		}
		return f, nil
	case 3:
		_, err := io.ReadFull(r, buf[:9])
		if err != nil {
			return
		}
		count := (16 + int(buf[8]&15)) << (uint32(buf[8]>>4) + 6)
		f := func(out, in []byte) {
			Iterated(out, h, in, buf[:8], count)
		}
		return f, nil
	}

	return nil, error.UnsupportedError("S2K function")
}

// hashToHashIdMapping contains pairs relating OpenPGP's hash identifier with
// Go's crypto.Hash type. See RFC 4880, section 9.4.
var hashToHashIdMapping = []struct {
	id   byte
	hash crypto.Hash
}{
	{1, crypto.MD5},
	{2, crypto.SHA1},
	{3, crypto.RIPEMD160},
	{8, crypto.SHA256},
	{9, crypto.SHA384},
	{10, crypto.SHA512},
	{11, crypto.SHA224},
}

// HashIdToHash returns a crypto.Hash which corresponds to the given OpenPGP
// hash id.
func HashIdToHash(id byte) (h crypto.Hash, ok bool) {
	for _, m := range hashToHashIdMapping {
		if m.id == id {
			return m.hash, true
		}
	}
	return 0, false
}

// HashIdToHash returns an OpenPGP hash id which corresponds the given Hash.
func HashToHashId(h crypto.Hash) (id byte, ok bool) {
	for _, m := range hashToHashIdMapping {
		if m.hash == h {
			return m.id, true
		}
	}
	return 0, false
}
