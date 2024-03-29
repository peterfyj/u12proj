// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Tests for CGI (the child process perspective)

package cgi

import (
	"testing"
)

func TestRequest(t *testing.T) {
	env := map[string]string{
		"REQUEST_METHOD":  "GET",
		"HTTP_HOST":       "example.com",
		"HTTP_REFERER":    "elsewhere",
		"HTTP_USER_AGENT": "goclient",
		"HTTP_FOO_BAR":    "baz",
		"REQUEST_URI":     "/path?a=b",
		"CONTENT_LENGTH":  "123",
	}
	req, err := requestFromEnvironment(env)
	if err != nil {
		t.Fatalf("requestFromEnvironment: %v", err)
	}
	if g, e := req.UserAgent, "goclient"; e != g {
		t.Errorf("expected UserAgent %q; got %q", e, g)
	}
	if g, e := req.Method, "GET"; e != g {
		t.Errorf("expected Method %q; got %q", e, g)
	}
	if g, e := req.Header.Get("User-Agent"), ""; e != g {
		// Tests that we don't put recognized headers in the map
		t.Errorf("expected User-Agent %q; got %q", e, g)
	}
	if g, e := req.ContentLength, int64(123); e != g {
		t.Errorf("expected ContentLength %d; got %d", e, g)
	}
	if g, e := req.Referer, "elsewhere"; e != g {
		t.Errorf("expected Referer %q; got %q", e, g)
	}
	if req.Header == nil {
		t.Fatalf("unexpected nil Header")
	}
	if g, e := req.Header.Get("Foo-Bar"), "baz"; e != g {
		t.Errorf("expected Foo-Bar %q; got %q", e, g)
	}
	if g, e := req.RawURL, "http://example.com/path?a=b"; e != g {
		t.Errorf("expected RawURL %q; got %q", e, g)
	}
	if g, e := req.URL.String(), "http://example.com/path?a=b"; e != g {
		t.Errorf("expected URL %q; got %q", e, g)
	}
	if g, e := req.FormValue("a"), "b"; e != g {
		t.Errorf("expected FormValue(a) %q; got %q", e, g)
	}
	if req.Trailer == nil {
		t.Errorf("unexpected nil Trailer")
	}
}

func TestRequestWithoutHost(t *testing.T) {
	env := map[string]string{
		"HTTP_HOST":      "",
		"REQUEST_METHOD": "GET",
		"REQUEST_URI":    "/path?a=b",
		"CONTENT_LENGTH": "123",
	}
	req, err := requestFromEnvironment(env)
	if err != nil {
		t.Fatalf("requestFromEnvironment: %v", err)
	}
	if g, e := req.RawURL, "/path?a=b"; e != g {
		t.Errorf("expected RawURL %q; got %q", e, g)
	}
	if req.URL == nil {
		t.Fatalf("unexpected nil URL")
	}
	if g, e := req.URL.String(), "/path?a=b"; e != g {
		t.Errorf("expected URL %q; got %q", e, g)
	}
}
