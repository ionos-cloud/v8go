// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.h"

#include <stdio.h>

#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "_cgo_export.h"

using namespace v8;

auto default_platform = platform::NewDefaultPlatform();
auto default_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();

const int ScriptCompilerNoCompileOptions = ScriptCompiler::kNoCompileOptions;
const int ScriptCompilerConsumeCodeCache = ScriptCompiler::kConsumeCodeCache;
const int ScriptCompilerEagerCompile = ScriptCompiler::kEagerCompile;


struct m_unboundScript {
  Persistent<UnboundScript, CopyablePersistentTraits<UnboundScript>> const ptr;

  m_unboundScript(Isolate *iso, Local<UnboundScript> &&script)
  :ptr(iso, std::move(script))
  { }

  // Prevents `new m_unboundScript()` -- call m_ctx::newUnboundScript() instead.
  static void* operator new(size_t) = delete;
};


struct m_value {
  ValueRef ref;
  Persistent<Value, CopyablePersistentTraits<Value>> ptr;

  template <class T>
  explicit m_value(Isolate *iso, ValueRef ref_, T &&val) noexcept
  :ref(ref_)
  ,ptr(iso, std::forward<T>(val))
  { }

  // only present because vector::resize needs it to exist; not called
  m_value() :ref{} { }
  // Prevents `new m_value()` -- call m_ctx::newValue() instead.
  static void* operator new(size_t) = delete;
};


struct m_ctx {
  Isolate* iso;
  Persistent<Context> ptr;

  static m_ctx* forV8Context(Local<Context> const& ctx) {
    return (m_ctx*) ctx->GetAlignedPointerFromEmbedderData(2);
  }

  static m_ctx* currentForIsolate(Isolate *iso) {
    if (iso->InContext())
      return forV8Context(iso->GetCurrentContext());
    else
      return nullptr;
  }

  m_ctx(Isolate *iso_, Local<Context> const& context_)
  :iso(iso_)
  ,ptr(iso_, context_)
  {
    context()->SetAlignedPointerInEmbedderData(2, this);
  }

  ~m_ctx() {
    ptr.Reset(); // (~Persistent does not do this due to NonCopyable traits)
  #ifdef CTX_LOG_VALUES
    fprintf(stderr, "*** m_ctx created %zu values, max table size %zu\n", _nValues, _maxValues);
  #endif
  }

  Local<Context> context() {return ptr.Get(iso);}

  ValueRef newValue(Local<Value> const& val) {
    ValueRef ref {_curScope, uint32_t(_values.size())};
    _values.emplace_back(iso, ref, val);
  #ifdef CTX_LOG_VALUES
    ++_nValues;
    if (ref.index >= _maxValues)
      _maxValues = ref.index + 1;
  #endif
    return ref;
  }

  Local<Value> getValue(ValueRef ref) {
    if (ref.index < _values.size()) {
      V8GoValue &value = _values[ref.index];
      if (value.ref.scope == ref.scope) {
        return value.ptr.Get(iso);
      }
    }
    fprintf(stderr, "***** ILLEGAL USE OF OBSOLETE v8go.Value[#%d @%d]; returning `undefined`\n",
      ref.index, ref.scope);
    return v8::Undefined(iso);
  }

  uint32_t pushValueScope() {
    _savedscopes.emplace_back(_curScope, _values.size());
    _curScope = ++_latestScope;
    return _curScope;
  }

  bool popValueScope(uint32_t scopeID) {
    if (scopeID != _curScope || _savedscopes.empty()) {
      return false;
    }
    size_t size;
    std::tie(_curScope, size) = _savedscopes.back();
    _savedscopes.pop_back();
    _values.resize(size);
    return true;
  }

  m_unboundScript* newUnboundScript(Local<UnboundScript> &&script) {
    _unboundScripts.emplace_back(iso, std::move(script));
    return &_unboundScripts.back();
  }

private:
  std::vector<m_value> _values;
  std::vector<std::pair<ValueScope, size_t>> _savedscopes;
  ValueScope _latestScope = 1, _curScope = 1;
  std::deque<V8GoUnboundScript> _unboundScripts; // (deque does not invalidate refs when it grows)
#ifdef CTX_LOG_VALUES
  size_t _nValues = 0, _maxValues = 0;
#endif
};


struct m_template {
  Isolate* iso;
  Persistent<Template> ptr;
};


static Local<Value> Deref(ValuePtr ptr) {
  return ptr.ctx->getValue(ptr.ref);
}


RtnString CopyString(Isolate *iso, Local<String> str) {
  int len = str->Utf8Length(iso);
  char* mem = (char*)malloc(len + 1);
  str->WriteUtf8(iso, mem);
  return {mem, len};
}

RtnString CopyString(Isolate *iso, Local<Value> val) {
  TryCatch tc(iso);
  Local<Context> context = iso->GetCurrentContext();
  Local<String> str;
  if (!val->ToString(context).ToLocal(&str)) {
    return {nullptr, 0};
  }
  return CopyString(iso, str);
}

static RtnError ExceptionError(TryCatch& try_catch,
                               Isolate* iso,
                               Local<Context> ctx) {
  HandleScope handle_scope(iso);

  RtnError rtn = {nullptr, nullptr, nullptr};

  if (try_catch.HasTerminated()) {
    rtn.msg = strdup("ExecutionTerminated: script execution has been terminated");
    return rtn;
  }

  rtn.msg = CopyString(iso, try_catch.Exception()).data;

  Local<Message> msg = try_catch.Message();
  if (!msg.IsEmpty()) {
    String::Utf8Value origin(iso, msg->GetScriptOrigin().ResourceName());
    std::ostringstream sb;
    sb << *origin;
    Maybe<int> line = try_catch.Message()->GetLineNumber(ctx);
    if (line.IsJust()) {
      sb << ":" << line.ToChecked();
    }
    Maybe<int> start = try_catch.Message()->GetStartColumn(ctx);
    if (start.IsJust()) {
      sb << ":"
         << start.ToChecked() + 1;  // + 1 to match output from stack trace
    }
    rtn.location = strdup(sb.str().c_str());
  }

  Local<Value> mstack;
  if (try_catch.StackTrace(ctx).ToLocal(&mstack)) {
    rtn.stack = CopyString(iso, mstack).data;
  }

  return rtn;
}


/********** Scope Classes **********/

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


struct WithTemplate : public WithIsolate {
  WithTemplate(TemplatePtr tmpl_ptr)
  :WithIsolate(tmpl_ptr->iso)
  ,iso(tmpl_ptr->iso)
  ,tmpl(tmpl_ptr->ptr.Get(iso))
  { }

  Isolate* const  iso;
  Local<Template> tmpl;
};


struct WithContext : public WithIsolate {
  WithContext(m_ctx *ctx)
  :WithIsolate(ctx->iso)
  ,iso(ctx->iso)
  ,try_catch(ctx->iso)
  ,local_ctx(ctx->context())
  ,context_scope(local_ctx)
  { }

  RtnError exceptionError() {
    return ExceptionError(try_catch, iso, local_ctx);
  }

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


struct WithObject : public WithValue {
  WithObject(ValuePtr ptr)
  :WithValue(ptr)
  ,obj(value.As<Object>())
  { }

  Local<Object> const obj;
};


/********** Isolate **********/

void Init() {
#ifdef _WIN32
  V8::InitializeExternalStartupData(".");
#endif
  V8::InitializePlatform(default_platform.get());
  V8::Initialize();
  return;
}

NewIsolateResult NewIsolate() {
  Isolate::CreateParams params;
  params.array_buffer_allocator = default_allocator;
  Isolate* iso = Isolate::New(params);
  WithIsolate _with(iso);

  iso->SetCaptureStackTraceForUncaughtExceptions(true);

  // Create a Context for internal use
  m_ctx* ctx = new m_ctx(iso, Context::New(iso));
  iso->SetData(0, ctx);

  NewIsolateResult result;
  result.isolate = iso;
  result.internalContext = ctx;
  result.undefinedVal = ctx->newValue(Undefined(iso));
  result.nullVal = ctx->newValue(Null(iso));
  result.falseVal = ctx->newValue(Boolean::New(iso, false));
  result.trueVal = ctx->newValue(Boolean::New(iso, true));
  return result;
}

static inline m_ctx* isolateInternalContext(Isolate* iso) {
  return static_cast<m_ctx*>(iso->GetData(0));
}

WithIsolatePtr IsolateLock(Isolate *iso) {
  return new WithIsolate(iso);
}

void IsolateUnlock(WithIsolatePtr w) {
  delete w;
}

void IsolatePerformMicrotaskCheckpoint(IsolatePtr iso) {
  WithIsolate _withiso(iso);
  iso->PerformMicrotaskCheckpoint();
}

void IsolateDispose(IsolatePtr iso) {
  if (iso == nullptr) {
    return;
  }
  ContextFree(isolateInternalContext(iso));

  iso->Dispose();
}

void IsolateTerminateExecution(IsolatePtr iso) {
  iso->TerminateExecution();
}

int IsolateIsExecutionTerminating(IsolatePtr iso) {
  return iso->IsExecutionTerminating();
}

IsolateHStatistics IsolationGetHeapStatistics(IsolatePtr iso) {
  if (iso == nullptr) {
    return IsolateHStatistics{0};
  }
  v8::HeapStatistics hs;
  iso->GetHeapStatistics(&hs);

  return IsolateHStatistics{hs.total_heap_size(),
                            hs.total_heap_size_executable(),
                            hs.total_physical_size(),
                            hs.total_available_size(),
                            hs.used_heap_size(),
                            hs.heap_size_limit(),
                            hs.malloced_memory(),
                            hs.external_memory(),
                            hs.peak_malloced_memory(),
                            hs.number_of_native_contexts(),
                            hs.number_of_detached_contexts()};
}

RtnUnboundScript IsolateCompileUnboundScript(IsolatePtr iso,
                                             const char* s, int sLen,
                                             const char* o, int oLen,
                                             CompileOptions opts) {
  m_ctx *ctx = isolateInternalContext(iso);
  WithContext _with(ctx);

  RtnUnboundScript rtn = {};

  Local<String> src =
      String::NewFromUtf8(iso, s, NewStringType::kNormal, sLen).ToLocalChecked();
  Local<String> ogn =
      String::NewFromUtf8(iso, o, NewStringType::kNormal, oLen).ToLocalChecked();

  ScriptCompiler::CompileOptions option =
      static_cast<ScriptCompiler::CompileOptions>(opts.compileOption);

  ScriptCompiler::CachedData* cached_data = nullptr;

  if (opts.cachedData.data) {
    cached_data = new ScriptCompiler::CachedData(opts.cachedData.data,
                                                 opts.cachedData.length);
  }

  ScriptOrigin script_origin(ogn);

  ScriptCompiler::Source source(src, script_origin, cached_data);

  Local<UnboundScript> unbound_script;
  if (!ScriptCompiler::CompileUnboundScript(iso, &source, option)
           .ToLocal(&unbound_script)) {
    rtn.error = _with.exceptionError();
    return rtn;
  };

  if (cached_data) {
    rtn.cachedDataRejected = cached_data->rejected;
  }

  rtn.ptr = ctx->newUnboundScript(std::move(unbound_script));
  return rtn;
}

/********** Exceptions & Errors **********/

ValueRef IsolateThrowException(IsolatePtr iso, ValuePtr value) {
  WithIsolate _withiso(iso);
  Local<Value> throw_ret_val = iso->ThrowException(Deref(value));
  return value.ctx->newValue(throw_ret_val);
}

/********** CpuProfiler **********/

CPUProfiler* NewCPUProfiler(IsolatePtr iso) {
  WithIsolate _withiso(iso);

  CPUProfiler* c = new CPUProfiler;
  c->iso = iso;
  c->ptr = CpuProfiler::New(iso);
  return c;
}

void CPUProfilerDispose(CPUProfiler* profiler) {
  if (profiler->ptr == nullptr) {
    return;
  }
  profiler->ptr->Dispose();

  delete profiler;
}

void CPUProfilerStartProfiling(CPUProfiler* profiler, const char* title) {
  if (profiler->iso == nullptr) {
    return;
  }

  WithIsolate _withiso(profiler->iso);

  Local<String> title_str =
      String::NewFromUtf8(profiler->iso, title, NewStringType::kNormal)
          .ToLocalChecked();
  profiler->ptr->StartProfiling(title_str);
}

CPUProfileNode* NewCPUProfileNode(const CpuProfileNode* ptr_) {
  int count = ptr_->GetChildrenCount();
  CPUProfileNode** children = new CPUProfileNode*[count];
  for (int i = 0; i < count; ++i) {
    children[i] = NewCPUProfileNode(ptr_->GetChild(i));
  }

  CPUProfileNode* root = new CPUProfileNode{
      ptr_,
      ptr_->GetNodeId(),
      ptr_->GetScriptId(),
      ptr_->GetScriptResourceNameStr(),
      ptr_->GetFunctionNameStr(),
      ptr_->GetLineNumber(),
      ptr_->GetColumnNumber(),
      ptr_->GetHitCount(),
      ptr_->GetBailoutReason(),
      count,
      children,
  };
  return root;
}

CPUProfile* CPUProfilerStopProfiling(CPUProfiler* profiler, const char* title) {
  if (profiler->iso == nullptr) {
    return nullptr;
  }

  WithIsolate _withiso(profiler->iso);

  Local<String> title_str =
      String::NewFromUtf8(profiler->iso, title, NewStringType::kNormal)
          .ToLocalChecked();

  CPUProfile* profile = new CPUProfile;
  profile->ptr = profiler->ptr->StopProfiling(title_str);

  Local<String> str = profile->ptr->GetTitle();
  profile->title = CopyString(profiler->iso, str).data;

  CPUProfileNode* root = NewCPUProfileNode(profile->ptr->GetTopDownRoot());
  profile->root = root;

  profile->startTime = profile->ptr->GetStartTime();
  profile->endTime = profile->ptr->GetEndTime();

  return profile;
}

void CPUProfileNodeDelete(CPUProfileNode* node) {
  for (int i = 0; i < node->childrenCount; ++i) {
    CPUProfileNodeDelete(node->children[i]);
  }

  delete[] node->children;
  delete node;
}

void CPUProfileDelete(CPUProfile* profile) {
  if (profile->ptr == nullptr) {
    return;
  }
  profile->ptr->Delete();
  free((void*)profile->title);

  CPUProfileNodeDelete(profile->root);

  delete profile;
}

/********** Template **********/

void TemplateFreeWrapper(TemplatePtr tmpl) {
  tmpl->ptr.Empty();  // Just does `val_ = 0;` without calling V8::DisposeGlobal
  delete tmpl;
}

void TemplateSetValue(TemplatePtr ptr,
                      const char* name, int nameLen,
                      ValuePtr val,
                      int attributes) {
  WithTemplate _with(ptr);

  Local<String> prop_name =
      String::NewFromUtf8(_with.iso, name, NewStringType::kNormal, nameLen).ToLocalChecked();
  _with.tmpl->Set(prop_name, Deref(val), (PropertyAttribute)attributes);
}

void TemplateSetTemplate(TemplatePtr ptr,
                         const char* name, int nameLen,
                         TemplatePtr obj,
                         int attributes) {
  WithTemplate _with(ptr);

  Local<String> prop_name =
      String::NewFromUtf8(_with.iso, name, NewStringType::kNormal, nameLen).ToLocalChecked();
  _with.tmpl->Set(prop_name, obj->ptr.Get(_with.iso), (PropertyAttribute)attributes);
}

/********** ObjectTemplate **********/

TemplatePtr NewObjectTemplate(IsolatePtr iso) {
  WithIsolate _withiso(iso);

  m_template* ot = new m_template;
  ot->iso = iso;
  ot->ptr.Reset(iso, ObjectTemplate::New(iso));
  return ot;
}

RtnValue ObjectTemplateNewInstance(TemplatePtr ptr, ContextPtr ctx) {
  WithContext _with(ctx);
  Local<Template> tmpl(ptr->ptr.Get(_with.iso));
  Local<ObjectTemplate> obj_tmpl = tmpl.As<ObjectTemplate>();

  RtnValue rtn = {};
  Local<Object> obj;
  if (!obj_tmpl->NewInstance(_with.local_ctx).ToLocal(&obj)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }

  rtn.value = ctx->newValue(obj);
  return rtn;
}

void ObjectTemplateSetInternalFieldCount(TemplatePtr ptr, int field_count) {
  WithTemplate _with(ptr);
  Local<ObjectTemplate> obj_tmpl = _with.tmpl.As<ObjectTemplate>();

  obj_tmpl->SetInternalFieldCount(field_count);
}

int ObjectTemplateInternalFieldCount(TemplatePtr ptr) {
  WithTemplate _with(ptr);
  Local<ObjectTemplate> obj_tmpl = _with.tmpl.As<ObjectTemplate>();

  return obj_tmpl->InternalFieldCount();
}

/********** FunctionTemplate **********/

static void FunctionTemplateCallback(const FunctionCallbackInfo<Value>& info) {
  Isolate* iso = info.GetIsolate();
  WithIsolate _withiso(iso);

  // This callback function can be called from any Context, which we only know
  // at runtime. We extract the Context reference from the embedder data so that
  // we can use the context registry to match the Context on the Go side
  Local<Context> local_ctx = iso->GetCurrentContext();
  int ctx_ref = local_ctx->GetEmbedderData(1).As<Integer>()->Value();
  m_ctx* ctx = goContext(ctx_ref);

  int callback_ref = info.Data().As<Integer>()->Value();

  ValueRef _this = ctx->newValue(info.This());

  int args_count = info.Length();
  ValueRef thisAndArgs[args_count + 1];
  thisAndArgs[0] = _this;
  for (int i = 0; i < args_count; i++) {
    thisAndArgs[1+i] = ctx->newValue(info[i]);
  }

  ValuePtr val = goFunctionCallback(ctx_ref, callback_ref, thisAndArgs, args_count);
  if (val.ctx != nullptr) {
    info.GetReturnValue().Set(Deref(val));
  } else {
    info.GetReturnValue().SetUndefined();
  }
}

TemplatePtr NewFunctionTemplate(IsolatePtr iso, int callback_ref) {
  WithIsolate _withiso(iso);

  // (rogchap) We only need to store one value, callback_ref, into the
  // C++ callback function data, but if we needed to store more items we could
  // use an V8::Array; this would require the internal context from
  // iso->GetData(0)
  Local<Integer> cbData = Integer::New(iso, callback_ref);

  m_template* ot = new m_template;
  ot->iso = iso;
  ot->ptr.Reset(iso,
                FunctionTemplate::New(iso, FunctionTemplateCallback, cbData));
  return ot;
}

RtnValue FunctionTemplateGetFunction(TemplatePtr ptr, ContextPtr ctx) {
  WithContext _with(ctx);
  Local<Template> tmpl(ptr->ptr.Get(_with.iso));

  Local<FunctionTemplate> fn_tmpl = tmpl.As<FunctionTemplate>();
  RtnValue rtn = {};
  Local<Function> fn;
  if (!fn_tmpl->GetFunction(_with.local_ctx).ToLocal(&fn)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }

  rtn.value = ctx->newValue(fn);
  return rtn;
}

/********** Context **********/

ContextPtr NewContext(IsolatePtr iso,
                      TemplatePtr global_template_ptr,
                      int ref) {
  WithIsolate _withiso(iso);

  Local<ObjectTemplate> global_template;
  if (global_template_ptr != nullptr) {
    global_template = global_template_ptr->ptr.Get(iso).As<ObjectTemplate>();
  } else {
    global_template = ObjectTemplate::New(iso);
  }

  // For function callbacks we need a reference to the context, but because of
  // the complexities of C -> Go function pointers, we store a reference to the
  // context as a simple integer identifier; this can then be used on the Go
  // side to lookup the context in the context registry. We use slot 1 as slot 0
  // has special meaning for the Chrome debugger.
  Local<Context> local_ctx = Context::New(iso, nullptr, global_template);
  local_ctx->SetEmbedderData(1, Integer::New(iso, ref));

  return new m_ctx(iso, std::move(local_ctx));
}

void ContextFree(ContextPtr ctx) {
  delete ctx;
}

RtnValue RunScript(ContextPtr ctx, const char* source, int sourceLen,
                   const char* origin, int originLen) {
  WithContext _with(ctx);
  auto iso = ctx->iso;

  RtnValue rtn = {};

  MaybeLocal<String> maybeSrc =
      String::NewFromUtf8(iso, source, NewStringType::kNormal, sourceLen);
  MaybeLocal<String> maybeOgn =
      String::NewFromUtf8(iso, origin, NewStringType::kNormal, originLen);
  Local<String> src, ogn;
  if (!maybeSrc.ToLocal(&src) || !maybeOgn.ToLocal(&ogn)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }

  ScriptOrigin script_origin(ogn);
  Local<Script> script;
  if (!Script::Compile(_with.local_ctx, src, &script_origin).ToLocal(&script)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Value> result;
  if (!script->Run(_with.local_ctx).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

/********** UnboundScript & ScriptCompilerCachedData **********/

ScriptCompilerCachedData* UnboundScriptCreateCodeCache(
    IsolatePtr iso,
    UnboundScriptPtr us_ptr) {
  WithIsolate _withiso(iso);

  Local<UnboundScript> unbound_script = us_ptr->ptr.Get(iso);

  ScriptCompiler::CachedData* cached_data =
      ScriptCompiler::CreateCodeCache(unbound_script);

  ScriptCompilerCachedData* cd = new ScriptCompilerCachedData;
  cd->ptr = cached_data;
  cd->data = cached_data->data;
  cd->length = cached_data->length;
  cd->rejected = cached_data->rejected;
  return cd;
}

void ScriptCompilerCachedDataDelete(ScriptCompilerCachedData* cached_data) {
  delete cached_data->ptr;
  delete cached_data;
}

// This can only run in contexts that belong to the same isolate
// the script was compiled in
RtnValue UnboundScriptRun(ContextPtr ctx, UnboundScriptPtr us_ptr) {
  WithContext _with(ctx);

  RtnValue rtn = {};

  Local<UnboundScript> unbound_script = us_ptr->ptr.Get(_with.iso);

  Local<Script> script = unbound_script->BindToCurrentContext();
  Local<Value> result;
  if (!script->Run(_with.local_ctx).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

RtnValue JSONParse(ContextPtr ctx, const char* str, int len) {
  WithContext _with(ctx);
  RtnValue rtn = {};

  Local<String> v8Str;
  if (!String::NewFromUtf8(_with.iso, str, NewStringType::kNormal, len).ToLocal(&v8Str)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }

  Local<Value> result;
  if (!JSON::Parse(_with.local_ctx, v8Str).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

const char* JSONStringify(ValuePtr val) {
  WithValue _with(val);

  Local<String> str;
  if (!JSON::Stringify(_with.local_ctx, _with.value).ToLocal(&str)) {
    return nullptr;
  }
  return CopyString(_with.iso, str).data;
}

ValueRef ContextGlobal(ContextPtr ctx) {
  WithContext _with(ctx);
  return ctx->newValue(_with.local_ctx->Global());
}

/********** ValueScope **********/

ValueScope PushValueScope(ContextPtr ctx) {
  Locker locker(ctx->iso);
  return ctx->pushValueScope();
}

Bool PopValueScope(ContextPtr ctx, ValueScope scope) {
  WithIsolate _withiso(ctx->iso);

  return ctx->popValueScope(scope);
}


/********** Value **********/

ValueRef NewValueInteger(ContextPtr ctx, int32_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->newValue(Integer::New(ctx->iso, v));
}

ValueRef NewValueIntegerFromUnsigned(ContextPtr ctx, uint32_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->newValue(Integer::NewFromUnsigned(ctx->iso, v));
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
  rtn.value = ctx->newValue(str);
  return rtn;
}

ValueRef NewValueNumber(ContextPtr ctx, double v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->newValue(Number::New(ctx->iso, v));
}

ValueRef NewValueBigInt(ContextPtr ctx, int64_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->newValue(BigInt::New(ctx->iso, v));
}

ValueRef NewValueBigIntFromUnsigned(ContextPtr ctx, uint64_t v) {
  WithIsolate _withiso(ctx->iso);
  return ctx->newValue(BigInt::NewFromUnsigned(ctx->iso, v));
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
  rtn.value = ctx->newValue(bigint);
  return rtn;
}

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
  rtn.value = ptr.ctx->newValue(obj);
  return rtn;
}

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


/********** Object **********/

void ObjectSet(ValuePtr ptr, const char* key, int keyLen, ValuePtr prop_val) {
  WithObject _with(ptr);
  Local<String> key_val =
      String::NewFromUtf8(_with.iso, key, NewStringType::kNormal, keyLen).ToLocalChecked();
  _with.obj->Set(_with.local_ctx, key_val, Deref(prop_val)).Check();
}

void ObjectSetIdx(ValuePtr ptr, uint32_t idx, ValuePtr prop_val) {
  WithObject _with(ptr);
  _with.obj->Set(_with.local_ctx, idx, Deref(prop_val)).Check();
}

int ObjectSetInternalField(ValuePtr ptr, int idx, ValuePtr val_ptr) {
  WithObject _with(ptr);

  if (idx >= _with.obj->InternalFieldCount()) {
    return 0;
  }

  _with.obj->SetInternalField(idx, Deref(val_ptr));

  return 1;
}

int ObjectInternalFieldCount(ValuePtr ptr) {
  WithObject _with(ptr);
  return _with.obj->InternalFieldCount();
}

RtnValue ObjectGet(ValuePtr ptr, const char* key, int keyLen) {
  WithObject _with(ptr);
  RtnValue rtn = {};

  Local<String> key_val;
  if (!String::NewFromUtf8(_with.iso, key, NewStringType::kNormal, keyLen)
           .ToLocal(&key_val)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Value> result;
  if (!_with.obj->Get(_with.local_ctx, key_val).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

ValuePtr ObjectGetInternalField(ValuePtr ptr, int idx) {
  WithObject _with(ptr);

  if (idx >= _with.obj->InternalFieldCount()) {
    return {};
  }

  Local<Value> result = _with.obj->GetInternalField(idx);

  return {ptr.ctx, ptr.ctx->newValue(result)};
}

RtnValue ObjectGetIdx(ValuePtr ptr, uint32_t idx) {
  WithObject _with(ptr);
  RtnValue rtn = {};

  Local<Value> result;
  if (!_with.obj->Get(_with.local_ctx, idx).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

int ObjectHas(ValuePtr ptr, const char* key, int keyLen) {
  WithObject _with(ptr);
  Local<String> key_val =
      String::NewFromUtf8(_with.iso, key, NewStringType::kNormal, keyLen).ToLocalChecked();
  return _with.obj->Has(_with.local_ctx, key_val).ToChecked();
}

int ObjectHasIdx(ValuePtr ptr, uint32_t idx) {
  WithObject _with(ptr);
  return _with.obj->Has(_with.local_ctx, idx).ToChecked();
}

int ObjectDelete(ValuePtr ptr, const char* key, int keyLen) {
  WithObject _with(ptr);
  Local<String> key_val =
      String::NewFromUtf8(_with.iso, key, NewStringType::kNormal, keyLen).ToLocalChecked();
  return _with.obj->Delete(_with.local_ctx, key_val).ToChecked();
}

int ObjectDeleteIdx(ValuePtr ptr, uint32_t idx) {
  WithObject _with(ptr);
  return _with.obj->Delete(_with.local_ctx, idx).ToChecked();
}

/********** Promise **********/

RtnValue NewPromiseResolver(ContextPtr ctx) {
  WithContext _with(ctx);
  RtnValue rtn = {};
  Local<Promise::Resolver> resolver;
  if (!Promise::Resolver::New(_with.local_ctx).ToLocal(&resolver)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ctx->newValue(resolver);
  return rtn;
}

ValueRef PromiseResolverGetPromise(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Promise::Resolver> resolver = _with.value.As<Promise::Resolver>();
  Local<Promise> promise = resolver->GetPromise();
  return ptr.ctx->newValue(promise);
}

int PromiseResolverResolve(ValuePtr ptr, ValuePtr resolve_val) {
  WithValue _with(ptr);
  Local<Promise::Resolver> resolver = _with.value.As<Promise::Resolver>();
  return resolver->Resolve(_with.local_ctx, Deref(resolve_val)).ToChecked();
}

int PromiseResolverReject(ValuePtr ptr, ValuePtr reject_val) {
  WithValue _with(ptr);
  Local<Promise::Resolver> resolver = _with.value.As<Promise::Resolver>();
  return resolver->Reject(_with.local_ctx, Deref(reject_val)).ToChecked();
}

int PromiseState(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Promise> promise = _with.value.As<Promise>();
  return promise->State();
}

RtnValue PromiseThen(ValuePtr ptr, int callback_ref) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Promise> promise = _with.value.As<Promise>();
  Local<Integer> cbData = Integer::New(_with.iso, callback_ref);
  Local<Function> func;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, cbData)
           .ToLocal(&func)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Promise> result;
  if (!promise->Then(_with.local_ctx, func).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

RtnValue PromiseThen2(ValuePtr ptr, int on_fulfilled_ref, int on_rejected_ref) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Promise> promise = _with.value.As<Promise>();
  Local<Integer> onFulfilledData = Integer::New(_with.iso, on_fulfilled_ref);
  Local<Function> onFulfilledFunc;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, onFulfilledData)
           .ToLocal(&onFulfilledFunc)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Integer> onRejectedData = Integer::New(_with.iso, on_rejected_ref);
  Local<Function> onRejectedFunc;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, onRejectedData)
           .ToLocal(&onRejectedFunc)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Promise> result;
  if (!promise->Then(_with.local_ctx, onFulfilledFunc, onRejectedFunc)
           .ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

RtnValue PromiseCatch(ValuePtr ptr, int callback_ref) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Promise> promise = _with.value.As<Promise>();
  Local<Integer> cbData = Integer::New(_with.iso, callback_ref);
  Local<Function> func;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, cbData)
           .ToLocal(&func)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Promise> result;
  if (!promise->Catch(_with.local_ctx, func).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

ValueRef PromiseResult(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Promise> promise = _with.value.As<Promise>();
  Local<Value> result = promise->Result();
  return ptr.ctx->newValue(result);
}

/********** Function **********/

static void buildCallArguments(Isolate* iso,
                               Local<Value>* argv,
                               int argc,
                               ValuePtr args[]) {
  for (int i = 0; i < argc; i++) {
    argv[i] = Deref(args[i]);
  }
}

RtnValue FunctionCall(ValuePtr ptr, ValuePtr recv, int argc, ValuePtr args[]) {
  WithValue _with(ptr);

  RtnValue rtn = {};
  Local<Function> fn = Local<Function>::Cast(_with.value);
  Local<Value> argv[argc];
  buildCallArguments(_with.iso, argv, argc, args);

  Local<Value> local_recv = Deref(recv);

  Local<Value> result;
  if (!fn->Call(_with.local_ctx, local_recv, argc, argv).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

RtnValue FunctionNewInstance(ValuePtr ptr, int argc, ValuePtr args[]) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Function> fn = Local<Function>::Cast(_with.value);
  Local<Value> argv[argc];
  buildCallArguments(_with.iso, argv, argc, args);
  Local<Object> result;
  if (!fn->NewInstance(_with.local_ctx, argc, argv).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->newValue(result);
  return rtn;
}

ValueRef FunctionSourceMapUrl(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Function> fn = Local<Function>::Cast(_with.value);
  Local<Value> result = fn->GetScriptOrigin().SourceMapUrl();
  return ptr.ctx->newValue(result);
}

/********** v8::V8 **********/

const char* Version() {
  return V8::GetVersion();
}

void SetFlags(const char* flags) {
  V8::SetFlagsFromString(flags);
}
