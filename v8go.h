// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8GO_H
#define V8GO_H

#ifdef __cplusplus
extern "C" {
#else
// Opaque to cgo, but useful to treat it as a pointer to a distinct type
typedef struct v8Isolate v8Isolate;
typedef v8Isolate* IsolatePtr;

typedef struct v8CpuProfiler v8CpuProfiler;
typedef v8CpuProfiler* CpuProfilerPtr;

typedef struct v8CpuProfile v8CpuProfile;
typedef v8CpuProfile* CpuProfilePtr;

typedef struct v8CpuProfileNode v8CpuProfileNode;
typedef const v8CpuProfileNode* CpuProfileNodePtr;

typedef struct v8ScriptCompilerCachedData v8ScriptCompilerCachedData;
typedef const v8ScriptCompilerCachedData* ScriptCompilerCachedDataPtr;

typedef struct WithIsolate* WithIsolatePtr;
typedef struct V8GoContext* ContextPtr;
typedef struct V8GoTemplate* TemplatePtr;
typedef struct V8GoUnboundScript* UnboundScriptPtr;

#endif

#include <stddef.h>
#include <stdint.h>

typedef uint8_t Bool;  // cgo does not like true `bool`

// ScriptCompiler::CompileOptions values
extern const int ScriptCompilerNoCompileOptions;
extern const int ScriptCompilerConsumeCodeCache;
extern const int ScriptCompilerEagerCompile;

typedef uint32_t ValueScope;
typedef uint32_t ValueIndex;

// A reference to a value, within a context. Needs a ContextPtr to fully resolve it.
typedef struct {
  ValueScope scope;
  ValueIndex index;
} ValueRef;

// An absolute reference to a value.
typedef struct {
  ContextPtr ctx;
  ValueRef ref;
} ValuePtr;

typedef struct {
  const char* msg;
  const char* location;
  const char* stack;
} RtnError;

typedef struct {
  UnboundScriptPtr ptr;
  int cachedDataRejected;
  RtnError error;
} RtnUnboundScript;

typedef struct {
  ScriptCompilerCachedDataPtr ptr;
  const uint8_t* data;
  int length;
  int rejected;
} ScriptCompilerCachedData;

typedef struct {
  ScriptCompilerCachedData cachedData;
  int compileOption;
} CompileOptions;

typedef struct {
  CpuProfilerPtr ptr;
  IsolatePtr iso;
} CPUProfiler;

typedef struct CPUProfileNode {
  CpuProfileNodePtr ptr;
  unsigned nodeId;
  int scriptId;
  const char* scriptResourceName;
  const char* functionName;
  int lineNumber;
  int columnNumber;
  unsigned hitCount;
  const char* bailoutReason;
  int childrenCount;
  struct CPUProfileNode** children;
} CPUProfileNode;

typedef struct {
  CpuProfilePtr ptr;
  const char* title;
  CPUProfileNode* root;
  int64_t startTime;
  int64_t endTime;
} CPUProfile;

typedef struct {
  ValueRef value;
  RtnError error;
} RtnValue;

typedef struct {
  const char* data;
  int length;
  RtnError error;
} RtnString;

typedef struct {
  size_t total_heap_size;
  size_t total_heap_size_executable;
  size_t total_physical_size;
  size_t total_available_size;
  size_t used_heap_size;
  size_t heap_size_limit;
  size_t malloced_memory;
  size_t external_memory;
  size_t peak_malloced_memory;
  size_t number_of_native_contexts;
  size_t number_of_detached_contexts;
} IsolateHStatistics;

typedef struct {
  const uint64_t* word_array;
  int word_count;
  int sign_bit;
} ValueBigInt;

typedef enum {    // This MUST be kept in sync with `ValueType` in value.go!
  Other_val = 0,
  Undefined_val,
  Null_val,
  True_val,
  False_val,
  Number_val,
  BigInt_val,
  String_val,
  Symbol_val,
  Function_val,
  Object_val,
} ValueType;

typedef struct {
  IsolatePtr isolate;
  ContextPtr internalContext;
  ValueRef undefinedVal, nullVal, falseVal, trueVal;
} NewIsolateResult;

extern void Init();
extern NewIsolateResult NewIsolate();
extern void IsolatePerformMicrotaskCheckpoint(IsolatePtr ptr);
extern void IsolateDispose(IsolatePtr ptr);
extern WithIsolatePtr IsolateLock(IsolatePtr);
extern void IsolateUnlock(WithIsolatePtr);
extern void IsolateTerminateExecution(IsolatePtr ptr);
extern int IsolateIsExecutionTerminating(IsolatePtr ptr);
extern IsolateHStatistics IsolationGetHeapStatistics(IsolatePtr ptr);

extern ValueRef IsolateThrowException(IsolatePtr iso, ValuePtr value);

extern RtnUnboundScript IsolateCompileUnboundScript(IsolatePtr iso_ptr,
                                                    const char* source, int sourceLen,
                                                    const char* origin, int originLen,
                                                    CompileOptions options);
extern ScriptCompilerCachedData* UnboundScriptCreateCodeCache(
    IsolatePtr iso_ptr,
    UnboundScriptPtr us_ptr);
extern void ScriptCompilerCachedDataDelete(
    ScriptCompilerCachedData* cached_data);
extern RtnValue UnboundScriptRun(ContextPtr ctx_ptr, UnboundScriptPtr us_ptr);

extern CPUProfiler* NewCPUProfiler(IsolatePtr iso_ptr);
extern void CPUProfilerDispose(CPUProfiler* ptr);
extern void CPUProfilerStartProfiling(CPUProfiler* ptr, const char* title);
extern CPUProfile* CPUProfilerStopProfiling(CPUProfiler* ptr,
                                            const char* title);
extern void CPUProfileDelete(CPUProfile* ptr);

extern ContextPtr NewContext(IsolatePtr iso_ptr,
                             TemplatePtr global_template_ptr,
                             uintptr_t ref);
extern void ContextFree(ContextPtr ptr);
extern RtnValue RunScript(ContextPtr ctx_ptr,
                          const char* source, int sourceLen,
                          const char* origin, int originLen);
extern RtnValue JSONParse(ContextPtr ctx_ptr, const char* str, int len);
extern RtnString JSONStringify(ValuePtr, void *buffer, int bufferSize);
extern ValueRef ContextGlobal(ContextPtr ctx_ptr);

extern void TemplateFreeWrapper(TemplatePtr ptr);
extern void TemplateSetValue(TemplatePtr ptr,
                             const char* name, int nameLen,
                             ValuePtr val_ptr,
                             int attributes);
extern void TemplateSetTemplate(TemplatePtr ptr,
                                const char* name, int nameLen,
                                TemplatePtr obj_ptr,
                                int attributes);

extern TemplatePtr NewObjectTemplate(IsolatePtr iso_ptr);
extern RtnValue ObjectTemplateNewInstance(TemplatePtr ptr, ContextPtr ctx_ptr);
extern void ObjectTemplateSetInternalFieldCount(TemplatePtr ptr,
                                                int field_count);
extern int ObjectTemplateInternalFieldCount(TemplatePtr ptr);

extern TemplatePtr NewFunctionTemplate(IsolatePtr iso_ptr, int callback_ref);
extern RtnValue FunctionTemplateGetFunction(TemplatePtr ptr,
                                            ContextPtr ctx_ptr);

extern ValueScope PushValueScope(ContextPtr);
extern Bool PopValueScope(ContextPtr, ValueScope);

extern ValueRef NewValueInteger(ContextPtr, int32_t v);
extern ValueRef NewValueIntegerFromUnsigned(ContextPtr, uint32_t v);
extern RtnValue NewValueString(ContextPtr, const char* v, int v_length);
extern ValueRef NewValueNumber(ContextPtr, double v);
extern ValueRef NewValueBigInt(ContextPtr, int64_t v);
extern ValueRef NewValueBigIntFromUnsigned(ContextPtr, uint64_t v);
extern RtnValue NewValueBigIntFromWords(ContextPtr,
                                        int sign_bit,
                                        int word_count,
                                        const uint64_t* words);
extern RtnString ValueToString(ValuePtr ptr, void *buffer, int bufferSize);
const uint32_t* ValueToArrayIndex(ValuePtr ptr);
int ValueToBoolean(ValuePtr ptr);
int32_t ValueToInt32(ValuePtr ptr);
int64_t ValueToInteger(ValuePtr ptr);
double ValueToNumber(ValuePtr ptr);
RtnString ValueToDetailString(ValuePtr ptr);
uint32_t ValueToUint32(ValuePtr ptr);
extern ValueBigInt ValueToBigInt(ValuePtr ptr);
extern RtnValue ValueToObject(ValuePtr ptr);
int ValueSameValue(ValuePtr ptr, ValuePtr otherPtr);
int ValueIsUndefined(ValuePtr ptr);
int ValueIsNull(ValuePtr ptr);
int ValueIsNullOrUndefined(ValuePtr ptr);
int ValueIsTrue(ValuePtr ptr);
int ValueIsFalse(ValuePtr ptr);
int ValueIsName(ValuePtr ptr);
int ValueIsString(ValuePtr ptr);
int ValueIsSymbol(ValuePtr ptr);
int ValueIsFunction(ValuePtr ptr);
int ValueIsObject(ValuePtr ptr);
int ValueIsBigInt(ValuePtr ptr);
int ValueIsBoolean(ValuePtr ptr);
int ValueIsNumber(ValuePtr ptr);
int ValueIsExternal(ValuePtr ptr);
int ValueIsInt32(ValuePtr ptr);
int ValueIsUint32(ValuePtr ptr);
int ValueIsDate(ValuePtr ptr);
int ValueIsArgumentsObject(ValuePtr ptr);
int ValueIsBigIntObject(ValuePtr ptr);
int ValueIsNumberObject(ValuePtr ptr);
int ValueIsStringObject(ValuePtr ptr);
int ValueIsSymbolObject(ValuePtr ptr);
int ValueIsNativeError(ValuePtr ptr);
int ValueIsRegExp(ValuePtr ptr);
int ValueIsAsyncFunction(ValuePtr ptr);
int ValueIsGeneratorFunction(ValuePtr ptr);
int ValueIsGeneratorObject(ValuePtr ptr);
int ValueIsPromise(ValuePtr ptr);
int ValueIsMap(ValuePtr ptr);
int ValueIsSet(ValuePtr ptr);
int ValueIsMapIterator(ValuePtr ptr);
int ValueIsSetIterator(ValuePtr ptr);
int ValueIsWeakMap(ValuePtr ptr);
int ValueIsWeakSet(ValuePtr ptr);
int ValueIsArray(ValuePtr ptr);
int ValueIsArrayBuffer(ValuePtr ptr);
int ValueIsArrayBufferView(ValuePtr ptr);
int ValueIsTypedArray(ValuePtr ptr);
int ValueIsUint8Array(ValuePtr ptr);
int ValueIsUint8ClampedArray(ValuePtr ptr);
int ValueIsInt8Array(ValuePtr ptr);
int ValueIsUint16Array(ValuePtr ptr);
int ValueIsInt16Array(ValuePtr ptr);
int ValueIsUint32Array(ValuePtr ptr);
int ValueIsInt32Array(ValuePtr ptr);
int ValueIsFloat32Array(ValuePtr ptr);
int ValueIsFloat64Array(ValuePtr ptr);
int ValueIsBigInt64Array(ValuePtr ptr);
int ValueIsBigUint64Array(ValuePtr ptr);
int ValueIsDataView(ValuePtr ptr);
int ValueIsSharedArrayBuffer(ValuePtr ptr);
int ValueIsProxy(ValuePtr ptr);
int ValueIsWasmModuleObject(ValuePtr ptr);
int ValueIsModuleNamespaceObject(ValuePtr ptr);
int /*ValueType*/ ValueGetType(ValuePtr ptr);

extern ValueRef NewObject(ContextPtr);
extern void ObjectSet(ValuePtr obj, const char* key, int keyLen, ValuePtr val_ptr);
extern int ObjectSetKey(ValuePtr obj, ValuePtr key, ValuePtr val_ptr);
extern void ObjectSetIdx(ValuePtr obj, uint32_t idx, ValuePtr val_ptr);
extern int ObjectSetInternalField(ValuePtr obj, int idx, ValuePtr val_ptr);
extern int ObjectInternalFieldCount(ValuePtr obj);
extern RtnValue ObjectGet(ValuePtr obj, const char* key, int keyLen);
extern RtnValue ObjectGetKey(ValuePtr obj, ValuePtr key);
extern RtnValue ObjectGetIdx(ValuePtr obj, uint32_t idx);
extern ValuePtr ObjectGetInternalField(ValuePtr obj, int idx);
extern int ObjectHas(ValuePtr obj, const char* key, int keyLen);
extern int ObjectHasKey(ValuePtr obj, ValuePtr key);
extern int ObjectHasIdx(ValuePtr obj, uint32_t idx);
extern int ObjectDelete(ValuePtr obj, const char* key, int keyLen);
extern int ObjectDeleteKey(ValuePtr obj, ValuePtr key);
extern int ObjectDeleteIdx(ValuePtr obj, uint32_t idx);

extern ValueRef NewArray(ContextPtr, uint32_t length);
extern uint32_t ArrayLength(ValuePtr ptr);

extern RtnValue NewPromiseResolver(ContextPtr ctx_ptr);
extern ValueRef PromiseResolverGetPromise(ValuePtr ptr);
int PromiseResolverResolve(ValuePtr ptr, ValuePtr val_ptr);
int PromiseResolverReject(ValuePtr ptr, ValuePtr val_ptr);
int PromiseState(ValuePtr ptr);
RtnValue PromiseThen(ValuePtr ptr, int callback_ref);
RtnValue PromiseThen2(ValuePtr ptr, int on_fulfilled_ref, int on_rejected_ref);
RtnValue PromiseCatch(ValuePtr ptr, int callback_ref);
extern ValueRef PromiseResult(ValuePtr ptr);

extern RtnValue FunctionCall(ValuePtr ptr,
                             ValuePtr recv,
                             int argc,
                             ValuePtr argv[]);
RtnValue FunctionNewInstance(ValuePtr ptr, int argc, ValuePtr args[]);
ValueRef FunctionSourceMapUrl(ValuePtr ptr);

const char* V8Version();
extern void SetV8Flags(const char* flags);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // V8GO_H
