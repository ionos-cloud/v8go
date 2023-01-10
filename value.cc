// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.hh"


/********** Value Creation **********/

ValueRef NewValueInteger(ContextPtr ctx, int32_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->addValue(Integer::New(ctx->iso, v));
}

ValueRef NewValueIntegerFromUnsigned(ContextPtr ctx, uint32_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->addValue(Integer::NewFromUnsigned(ctx->iso, v));
}

RtnValue NewValueString(ContextPtr ctx, const char* v, int v_length) {
  WithContext _with(ctx);
  RtnValue rtn = {};
  Local<String> str;
  if (!String::NewFromUtf8(_with.iso, v, NewStringType::kNormal, v_length)
           .ToLocal(&str)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ctx->addValue(str);
  return rtn;
}

ValueRef NewValueNumber(ContextPtr ctx, double v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->addValue(Number::New(ctx->iso, v));
}

ValueRef NewValueBigInt(ContextPtr ctx, int64_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->addValue(BigInt::New(ctx->iso, v));
}

ValueRef NewValueBigIntFromUnsigned(ContextPtr ctx, uint64_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->addValue(BigInt::NewFromUnsigned(ctx->iso, v));
}

RtnValue NewValueBigIntFromWords(ContextPtr ctx,
                                 int sign_bit,
                                 int word_count,
                                 const uint64_t* words) {
  WithContext _with(ctx);

  RtnValue rtn = {};
  Local<BigInt> bigint;
  if (!BigInt::NewFromWords(_with.local_ctx, sign_bit, word_count, words)
           .ToLocal(&bigint)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ctx->addValue(bigint);
  return rtn;
}


/********** Value Conversion **********/

const uint32_t* ValueToArrayIndex(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Uint32> array_index;
  if (!_with.value->ToArrayIndex(_with.local_ctx).ToLocal(&array_index)) {
    return nullptr;
  }

  uint32_t* idx = (uint32_t*)malloc(sizeof(uint32_t));
  *idx = array_index->Value();
  return idx;
}

int ValueToBoolean(ValuePtr ptr) {
  WithValue _with(ptr);
  return _with.value->BooleanValue(_with.iso);
}

int32_t ValueToInt32(ValuePtr ptr) {
  WithValue _with(ptr);
  return _with.value->Int32Value(_with.local_ctx).ToChecked();
}

int64_t ValueToInteger(ValuePtr ptr) {
  WithValue _with(ptr);
  return _with.value->IntegerValue(_with.local_ctx).ToChecked();
}

double ValueToNumber(ValuePtr ptr) {
  WithValue _with(ptr);
  return _with.value->NumberValue(_with.local_ctx).ToChecked();
}

RtnString ValueToDetailString(ValuePtr ptr) {
  WithValue _with(ptr);
  RtnString rtn = {0};
  Local<String> str;
  if (!_with.value->ToDetailString(_with.local_ctx).ToLocal(&str)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  return CopyString(_with.iso, str);
}

RtnString ValueToString(ValuePtr ptr) {
  WithValue _with(ptr);
  RtnString rtn = {0};
  Local<String> str;
  if (!_with.value->ToString(_with.local_ctx).ToLocal(&str)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  return CopyString(_with.iso, str);
}

uint32_t ValueToUint32(ValuePtr ptr) {
  WithValue _with(ptr);
  return _with.value->Uint32Value(_with.local_ctx).ToChecked();
}

ValueBigInt ValueToBigInt(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<BigInt> bint;
  if (!_with.value->ToBigInt(_with.local_ctx).ToLocal(&bint)) {
    return {nullptr, 0};
  }

  int word_count = bint->WordCount();
  int sign_bit = 0;
  uint64_t* words = (uint64_t*)malloc(sizeof(uint64_t) * word_count);
  bint->ToWordsArray(&sign_bit, &word_count, words);
  ValueBigInt rtn = {words, word_count, sign_bit};
  return rtn;
}

RtnValue ValueToObject(ValuePtr ptr) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Object> obj;
  if (!_with.value->ToObject(_with.local_ctx).ToLocal(&obj)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->addValue(obj);
  return rtn;
}


/********** Value Tests **********/

int ValueSameValue(ValuePtr val1, ValuePtr val2) {
  Isolate* iso = val1.ctx->iso;
  if (iso != val2.ctx->iso) {
    return false;
  }
  WithIsolate _withiso(iso);
  Local<Value> value1 = Deref(val1);
  Local<Value> value2 = Deref(val2);

  return value1->SameValue(value2);
}

using ValuePredicate = bool (Value::*)() const;

// The guts of ValueIsXXXX(). Takes a pointer to Value::IsXXXX().
static int ValueIs(ValuePtr ptr, ValuePredicate pred) {
  WithValue _with(ptr);
  Value *rawValue = _with.value.operator->();
  return (rawValue->*pred)();
}

int ValueIsUndefined(ValuePtr ptr) {return ValueIs(ptr, &Value::IsUndefined);}
int ValueIsNull(ValuePtr ptr) {return ValueIs(ptr, &Value::IsNull);}
int ValueIsNullOrUndefined(ValuePtr ptr) {return ValueIs(ptr, &Value::IsNullOrUndefined);}
int ValueIsTrue(ValuePtr ptr) {return ValueIs(ptr, &Value::IsTrue);}
int ValueIsFalse(ValuePtr ptr) {return ValueIs(ptr, &Value::IsFalse);}
int ValueIsName(ValuePtr ptr) {return ValueIs(ptr, &Value::IsName);}
int ValueIsString(ValuePtr ptr) {return ValueIs(ptr, &Value::IsString);}
int ValueIsSymbol(ValuePtr ptr) {return ValueIs(ptr, &Value::IsSymbol);}
int ValueIsFunction(ValuePtr ptr) {return ValueIs(ptr, &Value::IsFunction);}
int ValueIsObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsObject);}
int ValueIsBigInt(ValuePtr ptr) {return ValueIs(ptr, &Value::IsBigInt);}
int ValueIsBoolean(ValuePtr ptr) {return ValueIs(ptr, &Value::IsBoolean);}
int ValueIsNumber(ValuePtr ptr) {return ValueIs(ptr, &Value::IsNumber);}
int ValueIsExternal(ValuePtr ptr) {return ValueIs(ptr, &Value::IsExternal);}
int ValueIsInt32(ValuePtr ptr) {return ValueIs(ptr, &Value::IsInt32);}
int ValueIsUint32(ValuePtr ptr) {return ValueIs(ptr, &Value::IsUint32);}
int ValueIsDate(ValuePtr ptr) {return ValueIs(ptr, &Value::IsDate);}
int ValueIsArgumentsObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsArgumentsObject);}
int ValueIsBigIntObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsBigIntObject);}
int ValueIsNumberObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsNumberObject);}
int ValueIsStringObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsStringObject);}
int ValueIsSymbolObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsSymbolObject);}
int ValueIsNativeError(ValuePtr ptr) {return ValueIs(ptr, &Value::IsNativeError);}
int ValueIsRegExp(ValuePtr ptr) {return ValueIs(ptr, &Value::IsRegExp);}
int ValueIsAsyncFunction(ValuePtr ptr) {return ValueIs(ptr, &Value::IsAsyncFunction);}
int ValueIsGeneratorFunction(ValuePtr ptr) {return ValueIs(ptr, &Value::IsGeneratorFunction);}
int ValueIsGeneratorObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsGeneratorObject);}
int ValueIsPromise(ValuePtr ptr) {return ValueIs(ptr, &Value::IsPromise);}
int ValueIsMap(ValuePtr ptr) {return ValueIs(ptr, &Value::IsMap);}
int ValueIsSet(ValuePtr ptr) {return ValueIs(ptr, &Value::IsSet);}
int ValueIsMapIterator(ValuePtr ptr) {return ValueIs(ptr, &Value::IsMapIterator);}
int ValueIsSetIterator(ValuePtr ptr) {return ValueIs(ptr, &Value::IsSetIterator);}
int ValueIsWeakMap(ValuePtr ptr) {return ValueIs(ptr, &Value::IsWeakMap);}
int ValueIsWeakSet(ValuePtr ptr) {return ValueIs(ptr, &Value::IsWeakSet);}
int ValueIsArray(ValuePtr ptr) {return ValueIs(ptr, &Value::IsArray);}
int ValueIsArrayBuffer(ValuePtr ptr) {return ValueIs(ptr, &Value::IsArrayBuffer);}
int ValueIsArrayBufferView(ValuePtr ptr) {return ValueIs(ptr, &Value::IsArrayBufferView);}
int ValueIsTypedArray(ValuePtr ptr) {return ValueIs(ptr, &Value::IsTypedArray);}
int ValueIsUint8Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsUint8Array);}
int ValueIsUint8ClampedArray(ValuePtr ptr) {return ValueIs(ptr, &Value::IsUint8ClampedArray);}
int ValueIsInt8Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsInt8Array);}
int ValueIsUint16Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsUint16Array);}
int ValueIsInt16Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsInt16Array);}
int ValueIsUint32Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsUint32Array);}
int ValueIsInt32Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsInt32Array);}
int ValueIsFloat32Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsFloat32Array);}
int ValueIsFloat64Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsFloat64Array);}
int ValueIsBigInt64Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsBigInt64Array);}
int ValueIsBigUint64Array(ValuePtr ptr) {return ValueIs(ptr, &Value::IsBigUint64Array);}
int ValueIsDataView(ValuePtr ptr) {return ValueIs(ptr, &Value::IsDataView);}
int ValueIsSharedArrayBuffer(ValuePtr ptr) {return ValueIs(ptr, &Value::IsSharedArrayBuffer);}
int ValueIsProxy(ValuePtr ptr) {return ValueIs(ptr, &Value::IsProxy);}
int ValueIsWasmModuleObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsWasmModuleObject);}
int ValueIsModuleNamespaceObject(ValuePtr ptr) {return ValueIs(ptr, &Value::IsModuleNamespaceObject);}

int /*ValueType*/ ValueGetType(ValuePtr ptr) {
  WithValue _with(ptr);
  if (_with.value->IsObject()) {
    if (_with.value->IsFunction())  return Function_val;
    if (_with.value->IsGeneratorFunction())  return Function_val;
    return Object_val;
  } else {
    if (_with.value->IsString())    return String_val;
    if (_with.value->IsNumber())    return Number_val;
    if (_with.value->IsTrue())      return True_val;
    if (_with.value->IsFalse())     return False_val;
    if (_with.value->IsUndefined()) return Undefined_val;
    if (_with.value->IsNull())      return Null_val;
    if (_with.value->IsSymbol())    return Symbol_val;
    if (_with.value->IsBigInt())    return BigInt_val;
    return Other_val;
  }
}
