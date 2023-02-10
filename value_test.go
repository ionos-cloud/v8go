// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v8go_test

import (
	"bytes"
	"fmt"
	"math"
	"math/big"
	"reflect"
	"runtime"
	"testing"

	v8 "github.com/snej/v8go"
)

func TestValueNewBaseCases(t *testing.T) {
	t.Parallel()
	if _, err := v8.NewValue(nil, ""); err == nil {
		t.Error("expected error, but got <nil>")
	}
	iso := v8.NewIsolate()
	defer iso.Dispose()
	if _, err := v8.NewValue(iso, nil); err == nil {
		t.Error("expected error, but got <nil>")
	}
	if _, err := v8.NewValue(iso, struct{}{}); err == nil {
		t.Error("expected error, but got <nil>")
	}
}

func TestValueNew(t *testing.T) {
	iso := v8.NewIsolate()
	defer iso.Dispose()
	biggie, _ := new(big.Int).SetString("1234567890123456789012345678901234567890", 10)
	value, _ := v8.NewValue(iso, "hi")

	context := v8.NewContext(iso)
	defer context.Close()

	tests := [...]struct {
		input interface{}
		str   string
	}{
		{false, "false"},
		{true, "true"},
		{0, "0"},
		{1, "1"},
		{-1, "-1"},
		{int32(0x7FFFFFFF), "2147483647"},
		{int32(-0x80000000), "-2147483648"},
		{int64(0x7FFFFFFF), "2147483647"},
		{int64(-0x80000000), "-2147483648"},
		{1<<53 - 1, "9007199254740991"},
		{-(1<<53 - 1), "-9007199254740991"},
		{math.MaxInt64, "9223372036854775807"},
		{math.MinInt64, "-9223372036854775808"},
		{uint64(math.MaxUint64), "18446744073709551615"},
		{4321.125, "4321.125"},
		{6.02e23, "6.02e+23"},
		{biggie, "1234567890123456789012345678901234567890"},
		{"", ""},
		{"foobar", "foobar"},
		{value, "hi"},
	}

	for _, tt := range tests {
		t.Run(tt.str, func(t *testing.T) {
			val, err := v8.NewValue(iso, tt.input)
			if err != nil {
				t.Errorf("NewValue(%v) failed, %v", val, err)
				return
			}
			if str := val.DetailString(); str != tt.str {
				t.Errorf("unexpected DetailString `%s`", str)
			}
		})
	}

	for _, tt := range tests {
		t.Run("Context_"+tt.str, func(t *testing.T) {
			val, err := context.NewValue(tt.input)
			if err != nil {
				t.Errorf("Context.NewValue(%v) failed, %v", val, err)
				return
			}
			if str := val.DetailString(); str != tt.str {
				t.Errorf("unexpected DetailString `%s`", str)
			}
		})
	}
}

func TestValueFormatting(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source          string
		defaultVerb     string
		defaultVerbFlag string
		stringVerb      string
		quoteVerb       string
	}{
		{"new Object()", "[object Object]", "#<Object>", "[object Object]", `"[object Object]"`},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			if s := fmt.Sprintf("%v", val); s != tt.defaultVerb {
				t.Errorf("incorrect format for %%v: %s", s)
			}
			if s := fmt.Sprintf("%+v", val); s != tt.defaultVerbFlag {
				t.Errorf("incorrect format for %%+v: %s", s)
			}
			if s := fmt.Sprintf("%s", val); s != tt.stringVerb {
				t.Errorf("incorrect format for %%s: %s", s)
			}
			if s := fmt.Sprintf("%q", val); s != tt.quoteVerb {
				t.Errorf("incorrect format for %%q: %s", s)
			}
		})
	}
}

func TestValueString(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		name   string
		source string
		out    string
	}{
		{"Number", `13 * 2`, "26"},
		{"String", `"string"`, "string"},
		{"String with latin unicode", `"“Gemütlich”"`, "“Gemütlich”"},
		{"String with null character and non-latin unicode", `"a\x00Ω"`, "a\x00Ω"},
		{"Object", `let obj = {}; obj`, "[object Object]"},
		{"Function", `let fn = function(){}; fn`, "function(){}"},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.name, func(t *testing.T) {
			result, _ := ctx.RunScript(tt.source, "test.js")
			str := result.String()
			if str != tt.out {
				t.Errorf("unexpected result: expected %q, got %q", tt.out, str)
			}
		})
	}
}

func TestNewValue(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	iso := ctx.Isolate()
	defer iso.Dispose()
	defer ctx.Close()

	tests := []struct {
		name      string
		input     interface{}
		predicate string
	}{
		{"string", "s\x00s\x00", `str => str === "s\x00s\x00"`},
		{"int32", int32(36), `int => int === 36`},
		{"bool", true, `b => b === true`},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.name, func(t *testing.T) {
			val, err := ctx.RunScript(tt.predicate, "test.js")
			if err != nil {
				t.Fatal(err)
			}
			fn, err := val.AsFunction()
			if err != nil {
				t.Fatal(err)
			}

			jsVal, err := v8.NewValue(iso, tt.input)
			if err != nil {
				t.Fatal(err)
			}

			result, err := fn.Call(ctx.Global(), jsVal)
			if err != nil {
				t.Fatal(err)
			}
			if !result.Boolean() {
				t.Fatal("unexpected result: expected true, got false")
			}
		})
	}
}

func TestValueDetailString(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		name   string
		source string
		out    string
	}{
		{"Number", `13 * 2`, "26"},
		{"String", `"a string"`, "a string"},
		{"Empty String", `""`, ""},
		{"Object", `let obj = {}; obj`, "#<Object>"},
		{"Function", `let fn = function(){}; fn`, "function(){}"},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.name, func(t *testing.T) {
			result, _ := ctx.RunScript(tt.source, "test.js")
			str := result.DetailString()
			if str != tt.out {
				t.Errorf("unexpected result: expected %q, got %q", tt.out, str)
			}
		})
	}
}

func TestValueBoolean(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source string
		out    bool
	}{
		{"true", true},
		{"false", false},
		{"1", true},
		{"0", false},
		{"null", false},
		{"undefined", false},
		{"''", false},
		{"'foo'", true},
		{"() => {}", true},
		{"{}", false},
		{"{foo:'bar'}", true},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			if b := val.Boolean(); b != tt.out {
				t.Errorf("unexpected value: expected %v, got %v", tt.out, b)
			}
		})
	}
}

func TestValueConstants(t *testing.T) {
	t.Parallel()
	iso := v8.NewIsolate()
	defer iso.Dispose()
	ctx := v8.NewContext(iso)
	defer ctx.Close()

	tests := [...]struct {
		source string
		value  *v8.Value
		same   bool
	}{
		{"undefined", v8.Undefined(iso), true},
		{"null", v8.Null(iso), true},
		{"undefined", v8.Null(iso), false},
	}

	for _, tt := range tests {
		tt := tt

		val, err := ctx.RunScript(tt.source, "test.js")
		fatalIf(t, err)

		if tt.value.SameValue(val) != tt.same {
			t.Errorf("SameValue on JS `%s` and V8 value %+v didn't return %v",
				tt.source, tt.value, tt.same)
		}
	}
}

func TestValueArrayIndex(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source string
		idx    uint32
		ok     bool
	}{
		{"1", 1, true},
		{"0", 0, true},
		{"-1", 0, false},
		{"'1'", 1, true},
		{"'-1'", 0, false},
		{"'a'", 0, false},
		{"[1]", 1, true},
		{"['1']", 1, true},
		{"[1, 1]", 0, false},
		{"{}", 0, false},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			idx, ok := val.ArrayIndex()
			if ok != tt.ok {
				t.Errorf("unexpected ok: expected %v, got %v", tt.ok, ok)
			}
			if idx != tt.idx {
				t.Errorf("unexpected array index: expected %v, got %v", tt.idx, idx)
			}
		})
	}
}

func TestValueInt32(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source   string
		expected int32
	}{
		{"0", 0},
		{"1", 1},
		{"-1", -1},
		{"'1'", 1},
		{"1.5", 1},
		{"-1.5", -1},
		{"'a'", 0},
		{"[1]", 1},
		{"[1,1]", 0},
		{"Infinity", 0},
		{"Number.MAX_SAFE_INTEGER", -1},
		{"Number.MIN_SAFE_INTEGER", 1},
		{"Number.NaN", 0},
		{"2_147_483_647", 1<<31 - 1},
		{"-2_147_483_648", -1 << 31},
		{"2_147_483_648", -1 << 31}, // overflow
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			if i32 := val.Int32(); i32 != tt.expected {
				t.Errorf("unexpected value: expected %v, got %v", tt.expected, i32)
			}
		})
	}
}

func TestValueInteger(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source   string
		expected int64
	}{
		{"0", 0},
		{"1", 1},
		{"-1", -1},
		{"'1'", 1},
		{"1.5", 1},
		{"-1.5", -1},
		{"'a'", 0},
		{"[1]", 1},
		{"[1,1]", 0},
		{"Infinity", 1<<63 - 1},
		{"Number.MAX_SAFE_INTEGER", 1<<53 - 1},
		{"Number.MIN_SAFE_INTEGER", -(1<<53 - 1)},
		{"Number.NaN", 0},
		{"9_007_199_254_740_991", 1<<53 - 1},
		{"-9_007_199_254_740_991", -(1<<53 - 1)},
		{"9_223_372_036_854_775_810", 1<<63 - 1}, // does not overflow, pinned at 2^64 -1
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			if i64 := val.Integer(); i64 != tt.expected {
				t.Errorf("unexpected value: expected %v, got %v", tt.expected, i64)
			}
		})
	}
}

func TestValueNumber(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source   string
		expected float64
	}{
		{"0", 0},
		{"1", 1},
		{"-1", -1},
		{"'1'", 1},
		{"1.5", 1.5},
		{"-1.5", -1.5},
		{"'a'", math.NaN()},
		{"[1]", 1},
		{"[1,1]", math.NaN()},
		{"Infinity", math.Inf(0)},
		{"Number.MAX_VALUE", 1.7976931348623157e+308},
		{"Number.MIN_VALUE", 5e-324},
		{"Number.MAX_SAFE_INTEGER", 1<<53 - 1},
		{"Number.NaN", math.NaN()},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			f64 := val.Number()
			if math.IsNaN(tt.expected) {
				if !math.IsNaN(f64) {
					t.Errorf("unexpected value: expected NaN, got %v", f64)
				}
				return
			}
			if f64 != tt.expected {
				t.Errorf("unexpected value: expected %v, got %v", tt.expected, f64)
			}
		})
	}
}

func TestValueUint32(t *testing.T) {
	t.Parallel()
	ctx := v8.NewContext(nil)
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	tests := [...]struct {
		source   string
		expected uint32
	}{
		{"0", 0},
		{"1", 1},
		{"-1", 1<<32 - 1}, // overflow
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			val, _ := ctx.RunScript(tt.source, "test.js")
			if u32 := val.Uint32(); u32 != tt.expected {
				t.Errorf("unexpected value: expected %v, got %v", tt.expected, u32)
			}
		})
	}
}

func TestValueBigInt(t *testing.T) {
	t.Parallel()
	iso := v8.NewIsolate()
	defer iso.Dispose()

	x, _ := new(big.Int).SetString("36893488147419099136", 10) // larger than a single word size (64bit)

	tests := [...]struct {
		source   string
		expected *big.Int
	}{
		{"BigInt(0)", &big.Int{}},
		{"-1n", big.NewInt(-1)},
		{"new BigInt(1)", nil}, // bad syntax
		{"BigInt(Number.MAX_SAFE_INTEGER)", big.NewInt(1<<53 - 1)},
		{"BigInt(Number.MIN_SAFE_INTEGER)", new(big.Int).Neg(big.NewInt(1<<53 - 1))},
		{"BigInt(Number.MAX_SAFE_INTEGER) * 2n", big.NewInt(1<<54 - 2)},
		{"BigInt(Number.MAX_SAFE_INTEGER) * 4096n", x},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			ctx := v8.NewContext(iso)
			defer ctx.Close()

			val, _ := ctx.RunScript(tt.source, "test.js")
			b := val.BigInt()
			if b == nil && tt.expected != nil {
				t.Errorf("uexpected <nil> value")
				return
			}
			if b != nil && tt.expected == nil {
				t.Errorf("expected <nil>, but got value: %v", b)
				return
			}
			if b != nil && b.Cmp(tt.expected) != 0 {
				t.Errorf("unexpected value: expected %v, got %v", tt.expected, b)
			}
		})
	}
}

func TestValueObject(t *testing.T) {
	t.Parallel()

	ctx := v8.NewContext()
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	val, _ := ctx.RunScript("1", "")
	if _, err := val.AsObject(); err == nil {
		t.Error("Expected error but got <nil>")
	}
	if obj := val.Object(); obj.String() != "1" {
		t.Errorf("unexpected object value: %v", obj)
	}
}

func TestValuePromise(t *testing.T) {
	t.Parallel()

	ctx := v8.NewContext()
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	val, _ := ctx.RunScript("1", "")
	if _, err := val.AsPromise(); err == nil {
		t.Error("Expected error but got <nil>")
	}
	if _, err := ctx.RunScript("new Promise(()=>{})", ""); err != nil {
		t.Errorf("Unexpected error: %v", err)
	}

}

func TestValueFunction(t *testing.T) {
	t.Parallel()

	ctx := v8.NewContext()
	defer ctx.Isolate().Dispose()
	defer ctx.Close()

	val, _ := ctx.RunScript("1", "")
	if _, err := val.AsFunction(); err == nil {
		t.Error("Expected error but got <nil>")
	}
	val, err := ctx.RunScript("(a, b) => { return a + b; }", "")
	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	if _, err := val.AsFunction(); err != nil {
		t.Errorf("Expected success but got: %v", err)
	}

}

func TestValueSameValue(t *testing.T) {
	t.Parallel()
	iso := v8.NewIsolate()
	defer iso.Dispose()
	ctx := v8.NewContext(iso)
	defer ctx.Close()

	objTempl := v8.NewObjectTemplate(iso)
	obj1, err := objTempl.NewInstance(ctx)
	fatalIf(t, err)
	obj2, err := objTempl.NewInstance(ctx)
	fatalIf(t, err)

	if obj1.Value.SameValue(obj2.Value) != false {
		t.Errorf("SameValue on two different values didn't return false")
	}
	if obj1.Value.SameValue(obj1.Value) != true {
		t.Errorf("SameValue on two of the same value didn't return true")
	}
}

func TestValueIsXXX(t *testing.T) {
	t.Parallel()
	iso := v8.NewIsolate()
	defer iso.Dispose()
	tests := [...]struct {
		source       string
		assert       func(*v8.Value) bool
		expectedType v8.ValueType
	}{
		{"", (*v8.Value).IsUndefined, v8.UndefinedType},
		{"let v; v", (*v8.Value).IsUndefined, v8.UndefinedType},
		{"null", (*v8.Value).IsNull, v8.NullType},
		{"let v; v", (*v8.Value).IsNullOrUndefined, v8.UndefinedType},
		{"let v = null; v", (*v8.Value).IsNullOrUndefined, v8.NullType},
		{"true", (*v8.Value).IsTrue, v8.TrueType},
		{"false", (*v8.Value).IsFalse, v8.FalseType},
		{"'name'", (*v8.Value).IsName, v8.StringType},
		{"Symbol()", (*v8.Value).IsName, v8.SymbolType},
		{`"double quote"`, (*v8.Value).IsString, v8.StringType},
		{"'single quote'", (*v8.Value).IsString, v8.StringType},
		{"`string literal`", (*v8.Value).IsString, v8.StringType},
		{"Symbol()", (*v8.Value).IsSymbol, v8.SymbolType},
		{"Symbol('foo')", (*v8.Value).IsSymbol, v8.SymbolType},
		{"() => {}", (*v8.Value).IsFunction, v8.FunctionType},
		{"function v() {}; v", (*v8.Value).IsFunction, v8.FunctionType},
		{"const v = function() {}; v", (*v8.Value).IsFunction, v8.FunctionType},
		{"console.log", (*v8.Value).IsFunction, v8.FunctionType},
		{"Object", (*v8.Value).IsFunction, v8.FunctionType},
		{"class Foo {}; Foo", (*v8.Value).IsFunction, v8.FunctionType},
		{"class Foo { bar() {} }; (new Foo()).bar", (*v8.Value).IsFunction, v8.FunctionType},
		{"function* v(){}; v", (*v8.Value).IsFunction, v8.FunctionType},
		{"async function v(){}; v", (*v8.Value).IsFunction, v8.FunctionType},
		{"Object()", (*v8.Value).IsObject, v8.ObjectType},
		{"new Object", (*v8.Value).IsObject, v8.ObjectType},
		{"var v = {}; v", (*v8.Value).IsObject, v8.ObjectType},
		{"10n", (*v8.Value).IsBigInt, v8.BigIntType},
		{"BigInt(1)", (*v8.Value).IsBigInt, v8.BigIntType},
		{"true", (*v8.Value).IsBoolean, v8.TrueType},
		{"false", (*v8.Value).IsBoolean, v8.FalseType},
		{"Boolean()", (*v8.Value).IsBoolean, v8.FalseType},
		{"(new Boolean).valueOf()", (*v8.Value).IsBoolean, v8.FalseType},
		{"1", (*v8.Value).IsNumber, v8.NumberType},
		{"1.1", (*v8.Value).IsNumber, v8.NumberType},
		{"1_1", (*v8.Value).IsNumber, v8.NumberType},
		{".1", (*v8.Value).IsNumber, v8.NumberType},
		{"2e4", (*v8.Value).IsNumber, v8.NumberType},
		{"0x2", (*v8.Value).IsNumber, v8.NumberType},
		{"NaN", (*v8.Value).IsNumber, v8.NumberType},
		{"Infinity", (*v8.Value).IsNumber, v8.NumberType},
		{"Number(1)", (*v8.Value).IsNumber, v8.NumberType},
		{"(new Number()).valueOf()", (*v8.Value).IsNumber, v8.NumberType},
		{"1", (*v8.Value).IsInt32, v8.NumberType},
		{"-1", (*v8.Value).IsInt32, v8.NumberType},
		{"1", (*v8.Value).IsUint32, v8.NumberType},
		{"new Date", (*v8.Value).IsDate, v8.ObjectType},
		{"function foo(){ return arguments }; foo()", (*v8.Value).IsArgumentsObject, v8.ObjectType},
		{"Object(1n)", (*v8.Value).IsBigIntObject, v8.ObjectType},
		{"Object(1)", (*v8.Value).IsNumberObject, v8.ObjectType},
		{"new Number", (*v8.Value).IsNumberObject, v8.ObjectType},
		{"new String", (*v8.Value).IsStringObject, v8.ObjectType},
		{"Object('')", (*v8.Value).IsStringObject, v8.ObjectType},
		{"Object(Symbol())", (*v8.Value).IsSymbolObject, v8.ObjectType},
		{"Error()", (*v8.Value).IsNativeError, v8.ObjectType},
		{"TypeError()", (*v8.Value).IsNativeError, v8.ObjectType},
		{"SyntaxError()", (*v8.Value).IsNativeError, v8.ObjectType},
		{"/./", (*v8.Value).IsRegExp, v8.ObjectType},
		{"RegExp()", (*v8.Value).IsRegExp, v8.ObjectType},
		{"async function v(){}; v", (*v8.Value).IsAsyncFunction, v8.FunctionType},
		{"let v = async () => {}; v", (*v8.Value).IsAsyncFunction, v8.FunctionType},
		{"function* v(){}; v", (*v8.Value).IsGeneratorFunction, v8.FunctionType},
		{"function* v(){}; v()", (*v8.Value).IsGeneratorObject, v8.ObjectType},
		{"new Promise(()=>{})", (*v8.Value).IsPromise, v8.ObjectType},
		{"new Map", (*v8.Value).IsMap, v8.ObjectType},
		{"new Set", (*v8.Value).IsSet, v8.ObjectType},
		{"(new Map).entries()", (*v8.Value).IsMapIterator, v8.ObjectType},
		{"(new Set).entries()", (*v8.Value).IsSetIterator, v8.ObjectType},
		{"new WeakMap", (*v8.Value).IsWeakMap, v8.ObjectType},
		{"new WeakSet", (*v8.Value).IsWeakSet, v8.ObjectType},
		{"new Array", (*v8.Value).IsArray, v8.ObjectType},
		{"Array()", (*v8.Value).IsArray, v8.ObjectType},
		{"[]", (*v8.Value).IsArray, v8.ObjectType},
		{"new ArrayBuffer", (*v8.Value).IsArrayBuffer, v8.ObjectType},
		{"new Int8Array", (*v8.Value).IsArrayBufferView, v8.ObjectType},
		{"new Int8Array", (*v8.Value).IsTypedArray, v8.ObjectType},
		{"new Uint32Array", (*v8.Value).IsTypedArray, v8.ObjectType},
		{"new Uint8Array", (*v8.Value).IsUint8Array, v8.ObjectType},
		{"new Uint8ClampedArray", (*v8.Value).IsUint8ClampedArray, v8.ObjectType},
		{"new Int8Array", (*v8.Value).IsInt8Array, v8.ObjectType},
		{"new Uint16Array", (*v8.Value).IsUint16Array, v8.ObjectType},
		{"new Int16Array", (*v8.Value).IsInt16Array, v8.ObjectType},
		{"new Uint32Array", (*v8.Value).IsUint32Array, v8.ObjectType},
		{"new Int32Array", (*v8.Value).IsInt32Array, v8.ObjectType},
		{"new Float32Array", (*v8.Value).IsFloat32Array, v8.ObjectType},
		{"new Float64Array", (*v8.Value).IsFloat64Array, v8.ObjectType},
		{"new BigInt64Array", (*v8.Value).IsBigInt64Array, v8.ObjectType},
		{"new BigUint64Array", (*v8.Value).IsBigUint64Array, v8.ObjectType},
		{"new DataView(new ArrayBuffer)", (*v8.Value).IsDataView, v8.ObjectType},
		{"new SharedArrayBuffer", (*v8.Value).IsSharedArrayBuffer, v8.ObjectType},
		{"new Proxy({},{})", (*v8.Value).IsProxy, v8.ObjectType},
	}
	for _, tt := range tests {
		tt := tt
		t.Run(tt.source, func(t *testing.T) {
			ctx := v8.NewContext(iso)
			defer ctx.Close()

			val, err := ctx.RunScript(tt.source, "test.js")
			if err != nil {
				t.Fatalf("failed to run script: %v", err)
			}
			if !tt.assert(val) {
				t.Errorf("value is false for %s", runtime.FuncForPC(reflect.ValueOf(tt.assert).Pointer()).Name())
			}
			if tt.expectedType != val.GetType() {
				t.Errorf("value type is %v, expected %v", val.GetType(), tt.expectedType)
			}
		})
	}
}

func TestValueMarshalJSON(t *testing.T) {
	t.Parallel()
	iso := v8.NewIsolate()
	defer iso.Dispose()

	tests := [...]struct {
		name     string
		val      func(*v8.Context) *v8.Value
		expected []byte
	}{
		{
			"primitive",
			func(ctx *v8.Context) *v8.Value {
				val, _ := v8.NewValue(iso, int32(0))
				return val
			},
			[]byte("0"),
		},
		{
			"object",
			func(ctx *v8.Context) *v8.Value {
				val, _ := ctx.RunScript("let foo = {a:1, b:2}; foo", "test.js")
				return val
			},
			[]byte(`{"a":1,"b":2}`),
		},
		{
			"objectFunc",
			func(ctx *v8.Context) *v8.Value {
				val, _ := ctx.RunScript("let foo = {a:1, b:()=>{}}; foo", "test.js")
				return val
			},
			[]byte(`{"a":1}`),
		},
		{
			"nil",
			func(ctx *v8.Context) *v8.Value { return nil },
			[]byte(""),
		},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.name, func(t *testing.T) {
			ctx := v8.NewContext(iso)
			defer ctx.Close()
			val := tt.val(ctx)
			json, _ := val.MarshalJSON()
			if !bytes.Equal(json, tt.expected) {
				t.Errorf("unexpected JSON value: %s", string(json))
			}

		})
	}
}

func TestValueScopes(t *testing.T) {
	t.Parallel()
	iso := v8.NewIsolate()
	defer iso.Dispose()
	ctx := v8.NewContext(iso)
	defer ctx.Close()

	val1, _ := ctx.NewValue("value 1")
	if val1.DetailString() != "value 1" {
		t.Error("Unexpected value 1")
	}

	var val2 *v8.Value
	ctx.WithTemporaryValues(func() {
		val2, _ = ctx.NewValue("value 2")
		if val2.DetailString() != "value 2" {
			t.Error("Unexpected value 2")
		}
	})

	val3, _ := ctx.NewValue("value 3")
	if val3.DetailString() != "value 3" {
		t.Error("Unexpected value 3")
	}

	if val1.DetailString() != "value 1" {
		t.Error("Unexpected value 1, second try")
	}

	if !val2.IsUndefined() {
		t.Errorf("Unexpected obsolete val2; should now be undefined")
	}
	if deets := val2.DetailString(); deets != "undefined" {
		t.Errorf("Unexpected DetailString of obsolete val2; got %q, should be %q", deets, "undefined")
	}
}

func BenchmarkV8ToGoString(b *testing.B) {
	var kTestString = "This is an ASCII string of nontrivial but not excessive length."
	iso := v8.NewIsolate()
	defer iso.Dispose()
	iso.Lock()
	defer iso.Unlock()

	v8Str, _ := v8.NewValue(iso, kTestString)

	b.ResetTimer()
	for i := b.N; i > 0; i-- {
		goStr := v8Str.String()
		if goStr != kTestString {
			b.FailNow()
		}
	}
}
