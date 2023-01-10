// Copyright 2021 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v8go

// #include <stdlib.h>
// #include "v8go.h"
// static RtnValue ObjectGetGo(ValuePtr ptr, _GoString_ key) {
//		return ObjectGet(ptr, _GoStringPtr(key), _GoStringLen(key)); }
// static int ObjectHasGo(ValuePtr ptr, _GoString_ key) {
//		return ObjectHas(ptr, _GoStringPtr(key), _GoStringLen(key)); }
// static void ObjectSetGo(ValuePtr ptr, _GoString_ key, ValuePtr val_ptr) {
//		ObjectSet(ptr, _GoStringPtr(key), _GoStringLen(key), val_ptr); }
// static int ObjectDeleteGo(ValuePtr ptr, _GoString_ key) {
//		return ObjectDelete(ptr, _GoStringPtr(key), _GoStringLen(key)); }
import "C"
import (
	"fmt"
)

// Object is a JavaScript object (ECMA-262, 4.3.3)
type Object struct {
	*Value
}

func (o *Object) MethodCall(methodName string, args ...Valuer) (*Value, error) {
	getRtn := C.ObjectGetGo(o.valuePtr(), methodName)
	prop, err := valueResult(o.ctx, getRtn)
	if err != nil {
		return nil, err
	}
	fn, err := prop.AsFunction()
	if err != nil {
		return nil, err
	}
	return fn.Call(o, args...)
}

// Set will set a property on the Object to a given value.
// Supports all value types, eg: Object, Array, Date, Set, Map etc
// If the value passed is a Go supported primitive (string, int32, uint32, int64, uint64, float64, big.Int)
// then a *Value will be created and set as the value property.
func (o *Object) Set(key string, val interface{}) error {
	value, err := o.ctx.NewValue(val)
	if err != nil {
		return err
	}

	C.ObjectSetGo(o.valuePtr(), key, value.valuePtr())
	return nil
}

// Set will set a given index on the Object to a given value.
// Supports all value types, eg: Object, Array, Date, Set, Map etc
// If the value passed is a Go supported primitive (string, int32, uint32, int64, uint64, float64, big.Int)
// then a *Value will be created and set as the value property.
func (o *Object) SetIdx(idx uint32, val interface{}) error {
	value, err := o.ctx.NewValue(val)
	if err != nil {
		return err
	}

	C.ObjectSetIdx(o.valuePtr(), C.uint32_t(idx), value.valuePtr())

	return nil
}

// SetInternalField sets the value of an internal field for an ObjectTemplate instance.
// Panics if the index isn't in the range set by (*ObjectTemplate).SetInternalFieldCount.
func (o *Object) SetInternalField(idx uint32, val interface{}) error {
	value, err := o.ctx.NewValue(val)

	if err != nil {
		return err
	}

	inserted := C.ObjectSetInternalField(o.valuePtr(), C.int(idx), value.valuePtr())

	if inserted == 0 {
		panic(fmt.Errorf("index out of range [%v] with length %v", idx, o.InternalFieldCount()))
	}

	return nil
}

// InternalFieldCount returns the number of internal fields this Object has.
func (o *Object) InternalFieldCount() uint32 {
	count := C.ObjectInternalFieldCount(o.valuePtr())
	return uint32(count)
}

// Get tries to get a Value for a given Object property key.
func (o *Object) Get(key string) (*Value, error) {
	rtn := C.ObjectGetGo(o.valuePtr(), key)
	return valueResult(o.ctx, rtn)
}

// GetInternalField gets the Value set by SetInternalField for the given index
// or the JS undefined value if the index hadn't been set.
// Panics if given an out of range index.
func (o *Object) GetInternalField(idx uint32) *Value {
	rtn := C.ObjectGetInternalField(o.valuePtr(), C.int(idx))
	if rtn.ctx == nil {
		panic(fmt.Errorf("index out of range [%v] with length %v", idx, o.InternalFieldCount()))
	}
	return &Value{rtn.ref, o.ctx}

}

// GetIdx tries to get a Value at a give Object index.
func (o *Object) GetIdx(idx uint32) (*Value, error) {
	rtn := C.ObjectGetIdx(o.valuePtr(), C.uint32_t(idx))
	return valueResult(o.ctx, rtn)
}

// Has calls the abstract operation HasProperty(O, P) described in ECMA-262, 7.3.10.
// Returns true, if the object has the property, either own or on the prototype chain.
func (o *Object) Has(key string) bool {
	return C.ObjectHasGo(o.valuePtr(), key) != 0
}

// HasIdx returns true if the object has a value at the given index.
func (o *Object) HasIdx(idx uint32) bool {
	return C.ObjectHasIdx(o.valuePtr(), C.uint32_t(idx)) != 0
}

// Delete returns true if successful in deleting a named property on the object.
func (o *Object) Delete(key string) bool {
	return C.ObjectDeleteGo(o.valuePtr(), key) != 0
}

// DeleteIdx returns true if successful in deleting a value at a given index of the object.
func (o *Object) DeleteIdx(idx uint32) bool {
	return C.ObjectDeleteIdx(o.valuePtr(), C.uint32_t(idx)) != 0
}
