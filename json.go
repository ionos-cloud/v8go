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
	if val == nil || val.value() == nil {
		return "", errors.New("v8go: Value is required")
	}
	str := C.JSONStringify(val.value().valuePtr())
	if str == nil {
		return "", errors.New("v8go could not encode Value to JSON")
	}
	defer C.free(unsafe.Pointer(str))
	return C.GoString(str), nil
}
