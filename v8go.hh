// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8GO_HH
#define V8GO_HH

#include "libplatform/libplatform.h"
#include "v8.h"
#include "v8-profiler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


/********** Typedefs For C API (v8go.h) **********/

typedef v8::Isolate* IsolatePtr;
typedef v8::CpuProfiler* CpuProfilerPtr;
typedef v8::CpuProfile* CpuProfilePtr;
typedef const v8::CpuProfileNode* CpuProfileNodePtr;
typedef v8::ScriptCompiler::CachedData* ScriptCompilerCachedDataPtr;

namespace v8go {
  struct WithIsolate;
  struct V8GoContext;
  struct V8GoTemplate;
  struct V8GoUnboundScript;
}
typedef struct v8go::WithIsolate* WithIsolatePtr;
typedef struct v8go::V8GoContext* ContextPtr;
typedef struct v8go::V8GoTemplate* TemplatePtr;
typedef struct v8go::V8GoUnboundScript* UnboundScriptPtr;


#include "v8go.h"
#include "_cgo_export.h"


using namespace v8;

namespace v8go {

  /********** Utility Functions **********/

  RtnString CopyString(Isolate *iso, Local<String> str);

  RtnString CopyString(Isolate *iso, Local<Value> val);

  RtnError ExceptionError(TryCatch&, Isolate*, Local<Context>);

  void FunctionTemplateCallback(const FunctionCallbackInfo<Value>& info);


  /********** Internal Types **********/

  struct V8GoUnboundScript {
    Persistent<UnboundScript, CopyablePersistentTraits<UnboundScript>> const ptr;

    V8GoUnboundScript(Isolate *iso, Local<UnboundScript> script)
    :ptr(iso, script)
    { }

    // Prevents `new V8GoUnboundScript()` -- call m_ctx::newUnboundScript() instead.
    static void* operator new(size_t) = delete;
  };


  struct V8GoContext {
    V8GoContext(Isolate*, Local<Context>);
    ~V8GoContext();

    Local<Context> context() {return ptr.Get(iso);}

    ValueRef addValue(Local<Value>);

    Local<Value> getValue(ValueRef);

    uint32_t pushValueScope();
    bool popValueScope(uint32_t scopeID);

    V8GoUnboundScript* newUnboundScript(Local<UnboundScript>);

    Isolate* const iso;
    Persistent<Context> ptr;

  private:
    using PersistentValue = Persistent<Value, CopyablePersistentTraits<Value>>;

    std::vector<PersistentValue> _values;
    std::vector<ValueRef> _savedScopes;
    ValueScope _latestScope = 1, _curScope = 1;
    std::deque<V8GoUnboundScript> _unboundScripts; // (deque does not invalidate refs when it grows)
  #ifdef CTX_LOG_VALUES
    size_t _nValues = 0, _maxValues = 0;
  #endif
  };


  struct V8GoTemplate {
    Isolate* iso;
    Persistent<Template> ptr;
  };


  static inline Local<Value> Deref(ValuePtr const& ptr) {
    return ptr.ctx->getValue(ptr.ref);
  }


  /********** "With..." Scope Classes **********/

  struct WithIsolate {
    WithIsolate(Isolate *iso)
    :locker(iso)
    ,isolate_scope(iso)
    ,handle_scope(iso)
    { }

  private:
    Locker          locker;
    Isolate::Scope  isolate_scope;
    HandleScope     handle_scope;
  };


  struct WithContext : public WithIsolate {
    WithContext(V8GoContext *ctx)
    :WithIsolate(ctx->iso)
    ,iso(ctx->iso)
    ,try_catch(ctx->iso)
    ,local_ctx(ctx->context())
    ,context_scope(local_ctx)
    { }

    RtnError exceptionError() {return ExceptionError(try_catch, iso, local_ctx);}

    Isolate* const  iso;
    TryCatch        try_catch;
    Local<Context>  local_ctx;
    Context::Scope  context_scope;
  };


  struct WithValue : public WithContext {
    WithValue(ValuePtr val)
    :WithContext(val.ctx)
    ,value(Deref(val))
    { }

    Local<Value> const value;
  };


} // end namespace v8go

using namespace v8go;

#endif // V8GO_HH