// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// This package implements parsing and serialisation of OpenPGP packets, as
// specified in RFC 4880.
package packet

import (
	"crypto/aes"
	"crypto/cast5"
	"crypto/cipher"
	"crypto/openpgp/error"
	"io"
	"os"
)

// readFull is the same as io.ReadFull except that reading zero bytes returns
// ErrUnexpectedEOF rather than EOF.
func readFull(r io.Reader, buf []byte) (n int, err os.Error) {
	n, err = io.ReadFull(r, buf)
	if err == os.EOF {
		err = io.ErrUnexpectedEOF
	}
	return
}

// readLength reads an OpenPGP length from r. See RFC 4880, section 4.2.2.
func readLength(r io.Reader) (length int64, isPartial bool, err os.Error) {
	var buf [4]byte
	_, err = readFull(r, buf[:1])
	if err != nil {
		return
	}
	switch {
	case buf[0] < 192:
		length = int64(buf[0])
	case buf[0] < 224:
		length = int64(buf[0]-192) << 8
		_, err = readFull(r, buf[0:1])
		if err != nil {
			return
		}
		length += int64(buf[0]) + 192
	case buf[0] < 255:
		length = int64(1) << (buf[0] & 0x1f)
		isPartial = true
	default:
		_, err = readFull(r, buf[0:4])
		if err != nil {
			return
		}
		length = int64(buf[0])<<24 |
			int64(buf[1])<<16 |
			int64(buf[2])<<8 |
			int64(buf[3])
	}
	return
}

// partialLengthReader wraps an io.Reader and handles OpenPGP partial lengths.
// The continuation lengths are parsed and removed from the stream and EOF is
// returned at the end of the packet. See RFC 4880, section 4.2.2.4.
type partialLengthReader struct {
	r         io.Reader
	remaining int64
	isPartial bool
}

func (r *partialLengthReader) Read(p []byte) (n int, err os.Error) {
	for r.remaining == 0 {
		if !r.isPartial {
			return 0, os.EOF
		}
		r.remaining, r.isPartial, err = readLength(r.r)
		if err != nil {
			return 0, err
		}
	}

	toRead := int64(len(p))
	if toRead > r.remaining {
		toRead = r.remaining
	}

	n, err = r.r.Read(p[:int(toRead)])
	r.remaining -= int64(n)
	if n < int(toRead) && err == os.EOF {
		err = io.ErrUnexpectedEOF
	}
	return
}

// A spanReader is an io.LimitReader, but it returns ErrUnexpectedEOF if the
// underlying Reader returns EOF before the limit has been reached.
type spanReader struct {
	r io.Reader
	n int64
}

func (l *spanReader) Read(p []byte) (n int, err os.Error) {
	if l.n <= 0 {
		return 0, os.EOF
	}
	if int64(len(p)) > l.n {
		p = p[0:l.n]
	}
	n, err = l.r.Read(p)
	l.n -= int64(n)
	if l.n > 0 && err == os.EOF {
		err = io.ErrUnexpectedEOF
	}
	return
}

// readHeader parses a packet header and returns an io.Reader which will return
// the contents of the packet. See RFC 4880, section 4.2.
func readHeader(r io.Reader) (tag packetType, length int64, contents io.Reader, err os.Error) {
	var buf [4]byte
	_, err = io.ReadFull(r, buf[:1])
	if err != nil {
		return
	}
	if buf[0]&0x80 == 0 {
		err = error.StructuralError("tag byte does not have MSB set")
		return
	}
	if buf[0]&0x40 == 0 {
		// Old format packet
		tag = packetType((buf[0] & 0x3f) >> 2)
		lengthType := buf[0] & 3
		if lengthType == 3 {
			length = -1
			contents = r
			return
		}
		lengthBytes := 1 << lengthType
		_, err = readFull(r, buf[0:lengthBytes])
		if err != nil {
			return
		}
		for i := 0; i < lengthBytes; i++ {
			length <<= 8
			length |= int64(buf[i])
		}
		contents = &spanReader{r, length}
		return
	}

	// New format packet
	tag = packetType(buf[0] & 0x3f)
	length, isPartial, err := readLength(r)
	if err != nil {
		return
	}
	if isPartial {
		contents = &partialLengthReader{
			remaining: length,
			isPartial: true,
			r:         r,
		}
		length = -1
	} else {
		contents = &spanReader{r, length}
	}
	return
}

// serializeHeader writes an OpenPGP packet header to w. See RFC 4880, section
// 4.2.
func serializeHeader(w io.Writer, ptype packetType, length int) (err os.Error) {
	var buf [6]byte
	var n int

	buf[0] = 0x80 | 0x40 | byte(ptype)
	if length < 192 {
		buf[1] = byte(length)
		n = 2
	} else if length < 8384 {
		length -= 192
		buf[1] = 192 + byte(length>>8)
		buf[2] = byte(length)
		n = 3
	} else {
		buf[1] = 255
		buf[2] = byte(length >> 24)
		buf[3] = byte(length >> 16)
		buf[4] = byte(length >> 8)
		buf[5] = byte(length)
		n = 6
	}

	_, err = w.Write(buf[:n])
	return
}

// Packet represents an OpenPGP packet. Users are expected to try casting
// instances of this interface to specific packet types.
type Packet interface {
	parse(io.Reader) os.Error
}

// consumeAll reads from the given Reader until error, returning the number of
// bytes read.
func consumeAll(r io.Reader) (n int64, err os.Error) {
	var m int
	var buf [1024]byte

	for {
		m, err = r.Read(buf[:])
		n += int64(m)
		if err == os.EOF {
			err = nil
			return
		}
		if err != nil {
			return
		}
	}

	panic("unreachable")
}

// packetType represents the numeric ids of the different OpenPGP packet types. See
// http://www.iana.org/assignments/pgp-parameters/pgp-parameters.xhtml#pgp-parameters-2
type packetType uint8

const (
	packetTypeEncryptedKey              packetType = 1
	packetTypeSignature                 packetType = 2
	packetTypeSymmetricKeyEncrypted     packetType = 3
	packetTypeOnePassSignature          packetType = 4
	packetTypePrivateKey                packetType = 5
	packetTypePublicKey                 packetType = 6
	packetTypePrivateSubkey             packetType = 7
	packetTypeCompressed                packetType = 8
	packetTypeSymmetricallyEncrypted    packetType = 9
	packetTypeLiteralData               packetType = 11
	packetTypeUserId                    packetType = 13
	packetTypePublicSubkey              packetType = 14
	packetTypeSymmetricallyEncryptedMDC packetType = 18
)

// Read reads a single OpenPGP packet from the given io.Reader. If there is an
// error parsing a packet, the whole packet is consumed from the input.
func Read(r io.Reader) (p Packet, err os.Error) {
	tag, _, contents, err := readHeader(r)
	if err != nil {
		return
	}

	switch tag {
	case packetTypeEncryptedKey:
		p = new(EncryptedKey)
	case packetTypeSignature:
		p = new(Signature)
	case packetTypeSymmetricKeyEncrypted:
		p = new(SymmetricKeyEncrypted)
	case packetTypeOnePassSignature:
		p = new(OnePassSignature)
	case packetTypePrivateKey, packetTypePrivateSubkey:
		pk := new(PrivateKey)
		if tag == packetTypePrivateSubkey {
			pk.IsSubkey = true
		}
		p = pk
	case packetTypePublicKey, packetTypePublicSubkey:
		pk := new(PublicKey)
		if tag == packetTypePublicSubkey {
			pk.IsSubkey = true
		}
		p = pk
	case packetTypeCompressed:
		p = new(Compressed)
	case packetTypeSymmetricallyEncrypted:
		p = new(SymmetricallyEncrypted)
	case packetTypeLiteralData:
		p = new(LiteralData)
	case packetTypeUserId:
		p = new(UserId)
	case packetTypeSymmetricallyEncryptedMDC:
		se := new(SymmetricallyEncrypted)
		se.MDC = true
		p = se
	default:
		err = error.UnknownPacketTypeError(tag)
	}
	if p != nil {
		err = p.parse(contents)
	}
	if err != nil {
		consumeAll(contents)
	}
	return
}

// SignatureType represents the different semantic meanings of an OpenPGP
// signature. See RFC 4880, section 5.2.1.
type SignatureType uint8

const (
	SigTypeBinary        SignatureType = 0
	SigTypeText          = 1
	SigTypeGenericCert   = 0x10
	SigTypePersonaCert   = 0x11
	SigTypeCasualCert    = 0x12
	SigTypePositiveCert  = 0x13
	SigTypeSubkeyBinding = 0x18
)

// PublicKeyAlgorithm represents the different public key system specified for
// OpenPGP. See
// http://www.iana.org/assignments/pgp-parameters/pgp-parameters.xhtml#pgp-parameters-12
type PublicKeyAlgorithm uint8

const (
	PubKeyAlgoRSA            PublicKeyAlgorithm = 1
	PubKeyAlgoRSAEncryptOnly PublicKeyAlgorithm = 2
	PubKeyAlgoRSASignOnly    PublicKeyAlgorithm = 3
	PubKeyAlgoElgamal        PublicKeyAlgorithm = 16
	PubKeyAlgoDSA            PublicKeyAlgorithm = 17
)

// CipherFunction represents the different block ciphers specified for OpenPGP. See
// http://www.iana.org/assignments/pgp-parameters/pgp-parameters.xhtml#pgp-parameters-13
type CipherFunction uint8

const (
	CipherCAST5  = 3
	CipherAES128 = 7
	CipherAES192 = 8
	CipherAES256 = 9
)

// keySize returns the key size, in bytes, of cipher.
func (cipher CipherFunction) keySize() int {
	switch cipher {
	case CipherCAST5:
		return cast5.KeySize
	case CipherAES128:
		return 16
	case CipherAES192:
		return 24
	case CipherAES256:
		return 32
	}
	return 0
}

// blockSize returns the block size, in bytes, of cipher.
func (cipher CipherFunction) blockSize() int {
	switch cipher {
	case CipherCAST5:
		return 8
	case CipherAES128, CipherAES192, CipherAES256:
		return 16
	}
	return 0
}

// new returns a fresh instance of the given cipher.
func (cipher CipherFunction) new(key []byte) (block cipher.Block) {
	switch cipher {
	case CipherCAST5:
		block, _ = cast5.NewCipher(key)
	case CipherAES128, CipherAES192, CipherAES256:
		block, _ = aes.NewCipher(key)
	}
	return
}

// readMPI reads a big integer from r. The bit length returned is the bit
// length that was specified in r. This is preserved so that the integer can be
// reserialized exactly.
func readMPI(r io.Reader) (mpi []byte, bitLength uint16, err os.Error) {
	var buf [2]byte
	_, err = readFull(r, buf[0:])
	if err != nil {
		return
	}
	bitLength = uint16(buf[0])<<8 | uint16(buf[1])
	numBytes := (int(bitLength) + 7) / 8
	mpi = make([]byte, numBytes)
	_, err = readFull(r, mpi)
	return
}

// writeMPI serializes a big integer to r.
func writeMPI(w io.Writer, bitLength uint16, mpiBytes []byte) (err os.Error) {
	_, err = w.Write([]byte{byte(bitLength >> 8), byte(bitLength)})
	if err == nil {
		_, err = w.Write(mpiBytes)
	}
	return
}
