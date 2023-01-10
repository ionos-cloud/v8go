// Copyright 2021 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v8go

// #include "v8go.h"
import "C"
import (
	"runtime"
	"unsafe"
)

// Function is a JavaScript function.
type Function struct {
	*Value
}

func convertArgs(args []Valuer) ([]C.ValuePtr, *C.ValuePtr) {
	if len(args) == 0 {
		return nil, nil
	}
	var cArgs = make([]C.ValuePtr, len(args))
	for i, arg := range args {
		cArgs[i] = arg.value().valuePtr()
	}
	return cArgs, (*C.ValuePtr)(unsafe.Pointer(&cArgs[0]))
}

// Call this JavaScript function with the given arguments.
func (fn *Function) Call(recv Valuer, args ...Valuer) (*Value, error) {
	cArgs, argptr := convertArgs(args)
	rtn := C.FunctionCall(fn.valuePtr(), recv.value().valuePtr(), C.int(len(args)), argptr)
	runtime.KeepAlive(cArgs)
	return valueResult(fn.ctx, rtn)
}

// Invoke a constructor function to create an object instance.
func (fn *Function) NewInstance(args ...Valuer) (*Object, error) {
	cArgs, argptr := convertArgs(args)
	rtn := C.FunctionNewInstance(fn.valuePtr(), C.int(len(args)), argptr)
	runtime.KeepAlive(cArgs)
	return objectResult(fn.ctx, rtn)
}

// Return the source map url for a function.
func (fn *Function) SourceMapUrl() *Value {
	ptr := C.FunctionSourceMapUrl(fn.valuePtr())
	return &Value{ptr, fn.ctx}
}
