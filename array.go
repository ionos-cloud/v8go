// Copyright 2021 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v8go

/* #include "v8go.h" */
import "C"

// Array is a JavaScript Array object, a subtype of Object.
type Array struct {
	Object
}

// Creates a new Array with the given length.
// If the length is positive, the array will be populated with `undefined` values.
func NewArray(iso *Isolate, length int) *Array {
	return iso.internalContext.NewArray(length)
}

// Creates a new Array with the given length.
// If the length is positive, the array will be populated with `undefined` values.
func (c *Context) NewArray(length int) *Array {
	return &Array{Object{Value: &Value{ctx: c, ref: C.NewArray(c.ptr, C.uint(length))}}}
}

// Returns the length of the Array (the number of items)
func (a *Array) Length() uint32 {
	return uint32(C.ArrayLength(a.valuePtr()))
}
