// Copyright 2021 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v8go

/*
#include <stdlib.h>
#include "v8go.h"
static RtnValue JSONParseGo(ContextPtr ctx, _GoString_ str) {
	return JSONParse(ctx, _GoStringPtr(str), _GoStringLen(str)); }
*/
import "C"
import (
	"errors"
	"unsafe"
)

// JSONParse tries to parse the string and returns it as *Value if successful.
// Any JS errors will be returned as `JSError`.
func JSONParse(ctx *Context, str string) (*Value, error) {
	if ctx == nil {
		return nil, errors.New("v8go: Context is required")
	}
	rtn := C.JSONParseGo(ctx.ptr, str)
	return valueResult(ctx, rtn)
}

// JSONStringify tries to stringify the JSON-serializable object value and returns it as string.
func JSONStringify(ctx *Context, val Valuer) (string, error) {
	var v *Value
	if val != nil {
		v = val.value()
	}
	if v == nil {
		return "", errors.New("v8go: Value is required")
	}
	// It's OK to use the Isolate's shared buffer because we already require that client code can
	// only access an Isolate, and Values derived from it, on a single goroutine at a time.
	buffer := v.ctx.iso.stringBuffer
	bufPtr := unsafe.Pointer(&buffer[0])

	s := C.JSONStringify(v.valuePtr(), bufPtr, C.int(len(buffer)))
	if s.data == nil {
		return "", errors.New("v8go could not encode Value to JSON")
	} else if unsafe.Pointer(s.data) == bufPtr {
		return string(buffer[0:s.length]), nil
	} else {
		// Result was too big for buffer, so the C++ code malloc-ed its own
		defer C.free(unsafe.Pointer(s.data))
		return C.GoStringN(s.data, C.int(s.length)), nil
	}
}
