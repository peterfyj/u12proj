// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package lzw

import (
	"io"
	"io/ioutil"
	"os"
	"runtime"
	"testing"
)

var filenames = []string{
	"../testdata/e.txt",
	"../testdata/pi.txt",
}

// testFile tests that compressing and then decompressing the given file with
// the given options yields equivalent bytes to the original file.
func testFile(t *testing.T, fn string, order Order, litWidth int) {
	// Read the file, as golden output.
	golden, err := os.Open(fn, os.O_RDONLY, 0400)
	if err != nil {
		t.Errorf("%s (order=%d litWidth=%d): %v", fn, order, litWidth, err)
		return
	}
	defer golden.Close()

	// Read the file again, and push it through a pipe that compresses at the write end, and decompresses at the read end.
	raw, err := os.Open(fn, os.O_RDONLY, 0400)
	if err != nil {
		t.Errorf("%s (order=%d litWidth=%d): %v", fn, order, litWidth, err)
		return
	}

	piper, pipew := io.Pipe()
	defer piper.Close()
	go func() {
		defer raw.Close()
		defer pipew.Close()
		lzww := NewWriter(pipew, order, litWidth)
		defer lzww.Close()
		var b [4096]byte
		for {
			n, err0 := raw.Read(b[:])
			if err0 != nil && err0 != os.EOF {
				t.Errorf("%s (order=%d litWidth=%d): %v", fn, order, litWidth, err0)
				return
			}
			_, err1 := lzww.Write(b[:n])
			if err1 == os.EPIPE {
				// Fail, but do not report the error, as some other (presumably reportable) error broke the pipe.
				return
			}
			if err1 != nil {
				t.Errorf("%s (order=%d litWidth=%d): %v", fn, order, litWidth, err1)
				return
			}
			if err0 == os.EOF {
				break
			}
		}
	}()
	lzwr := NewReader(piper, order, litWidth)
	defer lzwr.Close()

	// Compare the two.
	b0, err0 := ioutil.ReadAll(golden)
	b1, err1 := ioutil.ReadAll(lzwr)
	if err0 != nil {
		t.Errorf("%s (order=%d litWidth=%d): %v", fn, order, litWidth, err0)
		return
	}
	if err1 != nil {
		t.Errorf("%s (order=%d litWidth=%d): %v", fn, order, litWidth, err1)
		return
	}
	if len(b0) != len(b1) {
		t.Errorf("%s (order=%d litWidth=%d): length mismatch %d versus %d", fn, order, litWidth, len(b0), len(b1))
		return
	}
	for i := 0; i < len(b0); i++ {
		if b0[i] != b1[i] {
			t.Errorf("%s (order=%d litWidth=%d): mismatch at %d, 0x%02x versus 0x%02x\n", fn, order, litWidth, i, b0[i], b1[i])
			return
		}
	}
}

func TestWriter(t *testing.T) {
	for _, filename := range filenames {
		for _, order := range [...]Order{LSB, MSB} {
			// The test data "2.71828 etcetera" is ASCII text requiring at least 6 bits.
			for _, litWidth := range [...]int{6, 7, 8} {
				testFile(t, filename, order, litWidth)
			}
		}
	}
}

func benchmarkEncoder(b *testing.B, n int) {
	b.StopTimer()
	b.SetBytes(int64(n))
	buf0, _ := ioutil.ReadFile("../testdata/e.txt")
	buf0 = buf0[:10000]
	buf1 := make([]byte, n)
	for i := 0; i < n; i += len(buf0) {
		copy(buf1[i:], buf0)
	}
	buf0 = nil
	runtime.GC()
	b.StartTimer()
	for i := 0; i < b.N; i++ {
		w := NewWriter(devNull{}, LSB, 8)
		w.Write(buf1)
		w.Close()
	}
}

func BenchmarkEncoder1e4(b *testing.B) {
	benchmarkEncoder(b, 1e4)
}

func BenchmarkEncoder1e5(b *testing.B) {
	benchmarkEncoder(b, 1e5)
}

func BenchmarkEncoder1e6(b *testing.B) {
	benchmarkEncoder(b, 1e6)
}
