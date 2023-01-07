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

#include "_cgo_export.h"

using namespace v8;

auto default_platform = platform::NewDefaultPlatform();
auto default_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();

const int ScriptCompilerNoCompileOptions = ScriptCompiler::kNoCompileOptions;
const int ScriptCompilerConsumeCodeCache = ScriptCompiler::kConsumeCodeCache;
const int ScriptCompilerEagerCompile = ScriptCompiler::kEagerCompile;


static void Panic(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fputs("*** v8go PANIC: ", stderr);
  vfprintf(stderr, fmt, args);
  fputs("\n", stderr);
  va_end(args);
  abort();  //TODO: Is there a friendlier way to trigger a Go panic?
}


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

  m_value()
  :ref{}
  { }

  template <class T>
  explicit m_value(Isolate *iso, ValueRef ref_, T &&val) noexcept
  :ref(ref_)
  ,ptr(iso, std::forward<T>(val))
  { }

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
    if (ref.index >= _values.size()) {
      Panic("Attempt to use obsolete v8go Value");
    }
    m_value &value = _values[ref.index];
    if (value.ref.scope != ref.scope) {
      Panic("Attempt to use obsolete v8go Value");
    }
    return value.ptr.Get(iso);
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
  std::deque<m_unboundScript> _unboundScripts;
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


const char* CopyString(std::string str) {
  int len = str.length();
  char* mem = (char*)malloc(len + 1);
  memcpy(mem, str.data(), len);
  mem[len] = 0;
  return mem;
}

const char* CopyString(String::Utf8Value& value) {
  if (value.length() == 0) {
    return nullptr;
  }
  return CopyString(std::string(*value, value.length()));
}

static RtnError ExceptionError(TryCatch& try_catch,
                               Isolate* iso,
                               Local<Context> ctx) {
  HandleScope handle_scope(iso);

  RtnError rtn = {nullptr, nullptr, nullptr};

  if (try_catch.HasTerminated()) {
    rtn.msg =
        CopyString("ExecutionTerminated: script execution has been terminated");
    return rtn;
  }

  String::Utf8Value exception(iso, try_catch.Exception());
  rtn.msg = CopyString(exception);

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
    rtn.location = CopyString(sb.str());
  }

  Local<Value> mstack;
  if (try_catch.StackTrace(ctx).ToLocal(&mstack)) {
    String::Utf8Value stack(iso, mstack);
    rtn.stack = CopyString(stack);
  }

  return rtn;
}


/********** Isolate **********/

#define ISOLATE_SCOPE(iso)           \
  Locker locker(iso);                \
  Isolate::Scope isolate_scope(iso); \
  HandleScope handle_scope(iso);

#define ISOLATE_SCOPE_INTERNAL_CONTEXT(iso) \
  ISOLATE_SCOPE(iso);                       \
  m_ctx* ctx = isolateInternalContext(iso);

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
  Locker locker(iso);
  Isolate::Scope isolate_scope(iso);
  HandleScope handle_scope(iso);

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

void IsolatePerformMicrotaskCheckpoint(IsolatePtr iso) {
  ISOLATE_SCOPE(iso)
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
                                             const char* s,
                                             const char* o,
                                             CompileOptions opts) {
  ISOLATE_SCOPE_INTERNAL_CONTEXT(iso);
  TryCatch try_catch(iso);
  Local<Context> local_ctx = ctx->context();
  Context::Scope context_scope(local_ctx);

  RtnUnboundScript rtn = {};

  Local<String> src =
      String::NewFromUtf8(iso, s, NewStringType::kNormal).ToLocalChecked();
  Local<String> ogn =
      String::NewFromUtf8(iso, o, NewStringType::kNormal).ToLocalChecked();

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
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
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
  ISOLATE_SCOPE(iso);
  Local<Value> throw_ret_val = iso->ThrowException(Deref(value));
  return value.ctx->newValue(throw_ret_val);
}

/********** CpuProfiler **********/

CPUProfiler* NewCPUProfiler(IsolatePtr iso_ptr) {
  Isolate* iso = static_cast<Isolate*>(iso_ptr);
  Locker locker(iso);
  Isolate::Scope isolate_scope(iso);
  HandleScope handle_scope(iso);

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

  Locker locker(profiler->iso);
  Isolate::Scope isolate_scope(profiler->iso);
  HandleScope handle_scope(profiler->iso);

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

  Locker locker(profiler->iso);
  Isolate::Scope isolate_scope(profiler->iso);
  HandleScope handle_scope(profiler->iso);

  Local<String> title_str =
      String::NewFromUtf8(profiler->iso, title, NewStringType::kNormal)
          .ToLocalChecked();

  CPUProfile* profile = new CPUProfile;
  profile->ptr = profiler->ptr->StopProfiling(title_str);

  Local<String> str = profile->ptr->GetTitle();
  String::Utf8Value t(profiler->iso, str);
  profile->title = CopyString(t);

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

#define LOCAL_TEMPLATE(tmpl_ptr)     \
  Isolate* iso = tmpl_ptr->iso;      \
  Locker locker(iso);                \
  Isolate::Scope isolate_scope(iso); \
  HandleScope handle_scope(iso);     \
  Local<Template> tmpl = tmpl_ptr->ptr.Get(iso);

void TemplateFreeWrapper(TemplatePtr tmpl) {
  tmpl->ptr.Empty();  // Just does `val_ = 0;` without calling V8::DisposeGlobal
  delete tmpl;
}

void TemplateSetValue(TemplatePtr ptr,
                      const char* name,
                      ValuePtr val,
                      int attributes) {
  LOCAL_TEMPLATE(ptr);

  Local<String> prop_name =
      String::NewFromUtf8(iso, name, NewStringType::kNormal).ToLocalChecked();
  tmpl->Set(prop_name, Deref(val), (PropertyAttribute)attributes);
}

void TemplateSetTemplate(TemplatePtr ptr,
                         const char* name,
                         TemplatePtr obj,
                         int attributes) {
  LOCAL_TEMPLATE(ptr);

  Local<String> prop_name =
      String::NewFromUtf8(iso, name, NewStringType::kNormal).ToLocalChecked();
  tmpl->Set(prop_name, obj->ptr.Get(iso), (PropertyAttribute)attributes);
}

/********** ObjectTemplate **********/

TemplatePtr NewObjectTemplate(IsolatePtr iso) {
  Locker locker(iso);
  Isolate::Scope isolate_scope(iso);
  HandleScope handle_scope(iso);

  m_template* ot = new m_template;
  ot->iso = iso;
  ot->ptr.Reset(iso, ObjectTemplate::New(iso));
  return ot;
}

RtnValue ObjectTemplateNewInstance(TemplatePtr ptr, ContextPtr ctx) {
  LOCAL_TEMPLATE(ptr);
  TryCatch try_catch(iso);
  Local<Context> local_ctx = ctx->context();
  Context::Scope context_scope(local_ctx);

  RtnValue rtn = {};

  Local<ObjectTemplate> obj_tmpl = tmpl.As<ObjectTemplate>();
  Local<Object> obj;
  if (!obj_tmpl->NewInstance(local_ctx).ToLocal(&obj)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }

  rtn.value = ctx->newValue(obj);
  return rtn;
}

void ObjectTemplateSetInternalFieldCount(TemplatePtr ptr, int field_count) {
  LOCAL_TEMPLATE(ptr);

  Local<ObjectTemplate> obj_tmpl = tmpl.As<ObjectTemplate>();
  obj_tmpl->SetInternalFieldCount(field_count);
}

int ObjectTemplateInternalFieldCount(TemplatePtr ptr) {
  LOCAL_TEMPLATE(ptr);

  Local<ObjectTemplate> obj_tmpl = tmpl.As<ObjectTemplate>();
  return obj_tmpl->InternalFieldCount();
}

/********** FunctionTemplate **********/

static void FunctionTemplateCallback(const FunctionCallbackInfo<Value>& info) {
  Isolate* iso = info.GetIsolate();
  ISOLATE_SCOPE(iso);

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
  Locker locker(iso);
  Isolate::Scope isolate_scope(iso);
  HandleScope handle_scope(iso);

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
  LOCAL_TEMPLATE(ptr);
  TryCatch try_catch(iso);
  Local<Context> local_ctx = ctx->context();
  Context::Scope context_scope(local_ctx);

  Local<FunctionTemplate> fn_tmpl = tmpl.As<FunctionTemplate>();
  RtnValue rtn = {};
  Local<Function> fn;
  if (!fn_tmpl->GetFunction(local_ctx).ToLocal(&fn)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }

  rtn.value = ctx->newValue(fn);;
  return rtn;
}

/********** Context **********/

#define LOCAL_CONTEXT(ctx)                      \
  Isolate* iso = ctx->iso;                      \
  Locker locker(iso);                           \
  Isolate::Scope isolate_scope(iso);            \
  HandleScope handle_scope(iso);                \
  TryCatch try_catch(iso);                      \
  Local<Context> local_ctx = ctx->context(); \
  Context::Scope context_scope(local_ctx);

ContextPtr NewContext(IsolatePtr iso,
                      TemplatePtr global_template_ptr,
                      int ref) {
  Locker locker(iso);
  Isolate::Scope isolate_scope(iso);
  HandleScope handle_scope(iso);

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

RtnValue RunScript(ContextPtr ctx, const char* source, const char* origin) {
  LOCAL_CONTEXT(ctx);

  RtnValue rtn = {};

  MaybeLocal<String> maybeSrc =
      String::NewFromUtf8(iso, source, NewStringType::kNormal);
  MaybeLocal<String> maybeOgn =
      String::NewFromUtf8(iso, origin, NewStringType::kNormal);
  Local<String> src, ogn;
  if (!maybeSrc.ToLocal(&src) || !maybeOgn.ToLocal(&ogn)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }

  ScriptOrigin script_origin(ogn);
  Local<Script> script;
  if (!Script::Compile(local_ctx, src, &script_origin).ToLocal(&script)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  Local<Value> result;
  if (!script->Run(local_ctx).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

/********** UnboundScript & ScriptCompilerCachedData **********/

ScriptCompilerCachedData* UnboundScriptCreateCodeCache(
    IsolatePtr iso,
    UnboundScriptPtr us_ptr) {
  ISOLATE_SCOPE(iso);

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
  LOCAL_CONTEXT(ctx)

  RtnValue rtn = {};

  Local<UnboundScript> unbound_script = us_ptr->ptr.Get(iso);

  Local<Script> script = unbound_script->BindToCurrentContext();
  Local<Value> result;
  if (!script->Run(local_ctx).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

RtnValue JSONParse(ContextPtr ctx, const char* str) {
  LOCAL_CONTEXT(ctx);
  RtnValue rtn = {};

  Local<String> v8Str;
  if (!String::NewFromUtf8(iso, str, NewStringType::kNormal).ToLocal(&v8Str)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
  }

  Local<Value> result;
  if (!JSON::Parse(local_ctx, v8Str).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

const char* JSONStringify(ValuePtr val) {
  Isolate* iso = val.ctx->iso;
  Locker locker(iso);
  Isolate::Scope isolate_scope(iso);
  HandleScope handle_scope(iso);

  Local<Context> local_ctx = val.ctx->context();
  Context::Scope context_scope(local_ctx);

  Local<String> str;
  if (!JSON::Stringify(local_ctx, Deref(val)).ToLocal(&str)) {
    return nullptr;
  }
  String::Utf8Value json(iso, str);
  return CopyString(json);
}

ValueRef ContextGlobal(ContextPtr ctx) {
  LOCAL_CONTEXT(ctx);
  return ctx->newValue(local_ctx->Global());
}

/********** ValueScope **********/

ValueScope PushValueScope(ContextPtr ctx) {
  Locker locker(ctx->iso);
  return ctx->pushValueScope();
}

Bool PopValueScope(ContextPtr ctx, ValueScope scope) {
  Locker locker(ctx->iso);
  Isolate::Scope isolate_scope(ctx->iso);
  HandleScope handle_scope(ctx->iso);

  return ctx->popValueScope(scope);
}


/********** Value **********/

#define LOCAL_VALUE(val)                   \
  m_ctx* ctx = val.ctx;                   \
  Isolate* iso = ctx->iso;                 \
  Locker locker(iso);                      \
  Isolate::Scope isolate_scope(iso);       \
  HandleScope handle_scope(iso);           \
  TryCatch try_catch(iso);                 \
  Local<Context> local_ctx = ctx->context(); \
  Context::Scope context_scope(local_ctx); \
  Local<Value> value = Deref(val);

ValueRef NewValueInteger(ContextPtr ctx, int32_t v) {
  ISOLATE_SCOPE(ctx->iso);
  return ctx->newValue(Integer::New(ctx->iso, v));
}

ValueRef NewValueIntegerFromUnsigned(ContextPtr ctx, uint32_t v) {
  ISOLATE_SCOPE(ctx->iso);
  return ctx->newValue(Integer::NewFromUnsigned(ctx->iso, v));
}

RtnValue NewValueString(ContextPtr ctx, const char* v, int v_length) {
  auto iso = ctx->iso;
  ISOLATE_SCOPE(iso);
  TryCatch try_catch(iso);
  RtnValue rtn = {};
  Local<String> str;
  if (!String::NewFromUtf8(iso, v, NewStringType::kNormal, v_length)
           .ToLocal(&str)) {
    rtn.error = ExceptionError(try_catch, iso, ctx->context());
    return rtn;
  }
  rtn.value = ctx->newValue(str);
  return rtn;
}

ValueRef NewValueNumber(ContextPtr ctx, double v) {
  ISOLATE_SCOPE(ctx->iso);
  return ctx->newValue(Number::New(ctx->iso, v));
}

ValueRef NewValueBigInt(ContextPtr ctx, int64_t v) {
  ISOLATE_SCOPE(ctx->iso);
  return ctx->newValue(BigInt::New(ctx->iso, v));
}

ValueRef NewValueBigIntFromUnsigned(ContextPtr ctx, uint64_t v) {
  ISOLATE_SCOPE(ctx->iso);
  return ctx->newValue(BigInt::NewFromUnsigned(ctx->iso, v));
}

RtnValue NewValueBigIntFromWords(ContextPtr ctx,
                                 int sign_bit,
                                 int word_count,
                                 const uint64_t* words) {
  Isolate *iso = ctx->iso;
  ISOLATE_SCOPE(iso);
  TryCatch try_catch(iso);
  Local<Context> local_ctx = ctx->context();

  RtnValue rtn = {};
  Local<BigInt> bigint;
  if (!BigInt::NewFromWords(local_ctx, sign_bit, word_count, words)
           .ToLocal(&bigint)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(bigint);
  return rtn;
}

const uint32_t* ValueToArrayIndex(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  Local<Uint32> array_index;
  if (!value->ToArrayIndex(local_ctx).ToLocal(&array_index)) {
    return nullptr;
  }

  uint32_t* idx = (uint32_t*)malloc(sizeof(uint32_t));
  *idx = array_index->Value();
  return idx;
}

int ValueToBoolean(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  return value->BooleanValue(iso);
}

int32_t ValueToInt32(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  return value->Int32Value(local_ctx).ToChecked();
}

int64_t ValueToInteger(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  return value->IntegerValue(local_ctx).ToChecked();
}

double ValueToNumber(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  return value->NumberValue(local_ctx).ToChecked();
}

RtnString ValueToDetailString(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  RtnString rtn = {0};
  Local<String> str;
  if (!value->ToDetailString(local_ctx).ToLocal(&str)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  String::Utf8Value ds(iso, str);
  rtn.data = CopyString(ds);
  rtn.length = ds.length();
  return rtn;
}

RtnString ValueToString(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  RtnString rtn = {0};
  // String::Utf8Value will result in an empty string if conversion to a string
  // fails
  // TODO: Consider propagating the JS error. A fallback value could be returned
  // in Value.String()
  String::Utf8Value src(iso, value);
  char* data = static_cast<char*>(malloc(src.length()));
  memcpy(data, *src, src.length());
  rtn.data = data;
  rtn.length = src.length();
  return rtn;
}

uint32_t ValueToUint32(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  return value->Uint32Value(local_ctx).ToChecked();
}

ValueBigInt ValueToBigInt(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  Local<BigInt> bint;
  if (!value->ToBigInt(local_ctx).ToLocal(&bint)) {
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
  LOCAL_VALUE(ptr);
  RtnValue rtn = {};
  Local<Object> obj;
  if (!value->ToObject(local_ctx).ToLocal(&obj)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(obj);
  return rtn;
}

int ValueSameValue(ValuePtr val1, ValuePtr val2) {
  Isolate* iso = val1.ctx->iso;
  if (iso != val2.ctx->iso) {
    return false;
  }
  ISOLATE_SCOPE(iso);
  Local<Value> value1 = Deref(val1);
  Local<Value> value2 = Deref(val2);

  return value1->SameValue(value2);
}

using ValuePredicate = bool (Value::*)() const;

// The guts of ValueIsXXXX(). Takes a pointer to Value::IsXXXX().
static int ValueIs(ValuePtr ptr, ValuePredicate pred) {
  LOCAL_VALUE(ptr);
  Value *rawValue = value.operator->();
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
  LOCAL_VALUE(ptr);
  if (value->IsObject()) {
    if (value->IsFunction())  return Function_val;
    if (value->IsGeneratorFunction())  return Function_val;
    return Object_val;
  } else {
    if (value->IsString())    return String_val;
    if (value->IsNumber())    return Number_val;
    if (value->IsTrue())      return True_val;
    if (value->IsFalse())     return False_val;
    if (value->IsUndefined()) return Undefined_val;
    if (value->IsNull())      return Null_val;
    if (value->IsSymbol())    return Symbol_val;
    if (value->IsBigInt())    return BigInt_val;
    return Other_val;
  }
}


/********** Object **********/

#define LOCAL_OBJECT(ptr) \
  LOCAL_VALUE(ptr)        \
  Local<Object> obj = value.As<Object>()

void ObjectSet(ValuePtr ptr, const char* key, ValuePtr prop_val) {
  LOCAL_OBJECT(ptr);
  Local<String> key_val =
      String::NewFromUtf8(iso, key, NewStringType::kNormal).ToLocalChecked();
  obj->Set(local_ctx, key_val, Deref(prop_val)).Check();
}

void ObjectSetIdx(ValuePtr ptr, uint32_t idx, ValuePtr prop_val) {
  LOCAL_OBJECT(ptr);
  obj->Set(local_ctx, idx, Deref(prop_val)).Check();
}

int ObjectSetInternalField(ValuePtr ptr, int idx, ValuePtr val_ptr) {
  LOCAL_OBJECT(ptr);

  if (idx >= obj->InternalFieldCount()) {
    return 0;
  }

  obj->SetInternalField(idx, Deref(val_ptr));

  return 1;
}

int ObjectInternalFieldCount(ValuePtr ptr) {
  LOCAL_OBJECT(ptr);
  return obj->InternalFieldCount();
}

RtnValue ObjectGet(ValuePtr ptr, const char* key) {
  LOCAL_OBJECT(ptr);
  RtnValue rtn = {};

  Local<String> key_val;
  if (!String::NewFromUtf8(iso, key, NewStringType::kNormal)
           .ToLocal(&key_val)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  Local<Value> result;
  if (!obj->Get(local_ctx, key_val).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

ValuePtr ObjectGetInternalField(ValuePtr ptr, int idx) {
  LOCAL_OBJECT(ptr);

  if (idx >= obj->InternalFieldCount()) {
    return {};
  }

  Local<Value> result = obj->GetInternalField(idx);

  return {ctx, ctx->newValue(result)};
}

RtnValue ObjectGetIdx(ValuePtr ptr, uint32_t idx) {
  LOCAL_OBJECT(ptr);
  RtnValue rtn = {};

  Local<Value> result;
  if (!obj->Get(local_ctx, idx).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

int ObjectHas(ValuePtr ptr, const char* key) {
  LOCAL_OBJECT(ptr);
  Local<String> key_val =
      String::NewFromUtf8(iso, key, NewStringType::kNormal).ToLocalChecked();
  return obj->Has(local_ctx, key_val).ToChecked();
}

int ObjectHasIdx(ValuePtr ptr, uint32_t idx) {
  LOCAL_OBJECT(ptr);
  return obj->Has(local_ctx, idx).ToChecked();
}

int ObjectDelete(ValuePtr ptr, const char* key) {
  LOCAL_OBJECT(ptr);
  Local<String> key_val =
      String::NewFromUtf8(iso, key, NewStringType::kNormal).ToLocalChecked();
  return obj->Delete(local_ctx, key_val).ToChecked();
}

int ObjectDeleteIdx(ValuePtr ptr, uint32_t idx) {
  LOCAL_OBJECT(ptr);
  return obj->Delete(local_ctx, idx).ToChecked();
}

/********** Promise **********/

RtnValue NewPromiseResolver(ContextPtr ctx) {
  LOCAL_CONTEXT(ctx);
  RtnValue rtn = {};
  Local<Promise::Resolver> resolver;
  if (!Promise::Resolver::New(local_ctx).ToLocal(&resolver)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(resolver);
  return rtn;
}

ValueRef PromiseResolverGetPromise(ValuePtr ptr) {
  LOCAL_VALUE(ptr);
  Local<Promise::Resolver> resolver = value.As<Promise::Resolver>();
  Local<Promise> promise = resolver->GetPromise();
  return ctx->newValue(promise);
}

int PromiseResolverResolve(ValuePtr ptr, ValuePtr resolve_val) {
  LOCAL_VALUE(ptr);
  Local<Promise::Resolver> resolver = value.As<Promise::Resolver>();
  return resolver->Resolve(local_ctx, Deref(resolve_val)).ToChecked();
}

int PromiseResolverReject(ValuePtr ptr, ValuePtr reject_val) {
  LOCAL_VALUE(ptr);
  Local<Promise::Resolver> resolver = value.As<Promise::Resolver>();
  return resolver->Reject(local_ctx, Deref(reject_val)).ToChecked();
}

int PromiseState(ValuePtr ptr) {
  LOCAL_VALUE(ptr)
  Local<Promise> promise = value.As<Promise>();
  return promise->State();
}

RtnValue PromiseThen(ValuePtr ptr, int callback_ref) {
  LOCAL_VALUE(ptr)
  RtnValue rtn = {};
  Local<Promise> promise = value.As<Promise>();
  Local<Integer> cbData = Integer::New(iso, callback_ref);
  Local<Function> func;
  if (!Function::New(local_ctx, FunctionTemplateCallback, cbData)
           .ToLocal(&func)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  Local<Promise> result;
  if (!promise->Then(local_ctx, func).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

RtnValue PromiseThen2(ValuePtr ptr, int on_fulfilled_ref, int on_rejected_ref) {
  LOCAL_VALUE(ptr)
  RtnValue rtn = {};
  Local<Promise> promise = value.As<Promise>();
  Local<Integer> onFulfilledData = Integer::New(iso, on_fulfilled_ref);
  Local<Function> onFulfilledFunc;
  if (!Function::New(local_ctx, FunctionTemplateCallback, onFulfilledData)
           .ToLocal(&onFulfilledFunc)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  Local<Integer> onRejectedData = Integer::New(iso, on_rejected_ref);
  Local<Function> onRejectedFunc;
  if (!Function::New(local_ctx, FunctionTemplateCallback, onRejectedData)
           .ToLocal(&onRejectedFunc)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  Local<Promise> result;
  if (!promise->Then(local_ctx, onFulfilledFunc, onRejectedFunc)
           .ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

RtnValue PromiseCatch(ValuePtr ptr, int callback_ref) {
  LOCAL_VALUE(ptr)
  RtnValue rtn = {};
  Local<Promise> promise = value.As<Promise>();
  Local<Integer> cbData = Integer::New(iso, callback_ref);
  Local<Function> func;
  if (!Function::New(local_ctx, FunctionTemplateCallback, cbData)
           .ToLocal(&func)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  Local<Promise> result;
  if (!promise->Catch(local_ctx, func).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

ValueRef PromiseResult(ValuePtr ptr) {
  LOCAL_VALUE(ptr)
  Local<Promise> promise = value.As<Promise>();
  Local<Value> result = promise->Result();
  return ctx->newValue(result);
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
  LOCAL_VALUE(ptr)

  RtnValue rtn = {};
  Local<Function> fn = Local<Function>::Cast(value);
  Local<Value> argv[argc];
  buildCallArguments(iso, argv, argc, args);

  Local<Value> local_recv = Deref(recv);

  Local<Value> result;
  if (!fn->Call(local_ctx, local_recv, argc, argv).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

RtnValue FunctionNewInstance(ValuePtr ptr, int argc, ValuePtr args[]) {
  LOCAL_VALUE(ptr)
  RtnValue rtn = {};
  Local<Function> fn = Local<Function>::Cast(value);
  Local<Value> argv[argc];
  buildCallArguments(iso, argv, argc, args);
  Local<Object> result;
  if (!fn->NewInstance(local_ctx, argc, argv).ToLocal(&result)) {
    rtn.error = ExceptionError(try_catch, iso, local_ctx);
    return rtn;
  }
  rtn.value = ctx->newValue(result);
  return rtn;
}

ValueRef FunctionSourceMapUrl(ValuePtr ptr) {
  LOCAL_VALUE(ptr)
  Local<Function> fn = Local<Function>::Cast(value);
  Local<Value> result = fn->GetScriptOrigin().SourceMapUrl();
  return ctx->newValue(result);
}

/********** v8::V8 **********/

const char* Version() {
  return V8::GetVersion();
}

void SetFlags(const char* flags) {
  V8::SetFlagsFromString(flags);
}
