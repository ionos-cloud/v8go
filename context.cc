// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.hh"


/********** V8GoContext Implementation **********/

namespace v8go {

  V8GoContext::V8GoContext(Isolate *iso_, Local<Context> context_)
  :iso(iso_)
  ,ptr(iso_, context_)
  {
    context()->SetAlignedPointerInEmbedderData(2, this);
  }

  V8GoContext::~V8GoContext() {
    ptr.Reset(); // (~Persistent does not do this due to NonCopyable traits)
  #ifdef CTX_LOG_VALUES
    fprintf(stderr, "*** m_ctx created %zu values, max table size %zu\n", _nValues, _maxValues);
  #endif
  }

  ValueRef V8GoContext::addValue(Local<Value> val) {
    ValueRef ref {_curScope, uint32_t(_values.size())};
    _values.emplace_back(PersistentValue(iso, val));
  #ifdef CTX_LOG_VALUES
    ++_nValues;
    if (ref.index >= _maxValues)
      _maxValues = ref.index + 1;
  #endif
    return ref;
  }

  Local<Value> V8GoContext::getValue(ValueRef ref) {
    if (ref.index < _values.size()) {
      auto scope = _curScope;
      for (auto i = _savedScopes.rbegin(); i != _savedScopes.rend(); ++i) {
        if (ref.index >= i->index) {
          break;
        }
        scope = i->scope;
      }
      if (ref.scope == scope) {
        return _values[ref.index].Get(iso);
      }
    }

    fprintf(stderr, "***** ILLEGAL USE OF OBSOLETE v8go.Value[#%d @%d]; returning `undefined`\n",
            ref.index, ref.scope);
    return v8::Undefined(iso);
  }

  uint32_t V8GoContext::pushValueScope() {
    _savedScopes.push_back(ValueRef{_curScope, uint32_t(_values.size())});
    _curScope = ++_latestScope;
    return _curScope;
  }

  bool V8GoContext::popValueScope(uint32_t scopeID) {
    if (scopeID != _curScope || _savedScopes.empty()) {
      return false;
    }
    ValueRef r = _savedScopes.back();
    _savedScopes.pop_back();
    _curScope = r.scope;
    _values.resize(r.index);
    return true;
  }

  V8GoUnboundScript* V8GoContext::newUnboundScript(Local<UnboundScript> script) {
    _unboundScripts.emplace_back(iso, std::move(script));
    return &_unboundScripts.back();
  }

}


/********** Context **********/

ContextPtr NewContext(IsolatePtr iso,
                      TemplatePtr global_template_ptr,
                      int ref) {
  WithIsolate _with(iso);

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

  return new V8GoContext(iso, std::move(local_ctx));
}

void ContextFree(ContextPtr ctx) {
  delete ctx;
}

ValueRef ContextGlobal(ContextPtr ctx) {
  WithContext _with(ctx);
  return ctx->addValue(_with.local_ctx->Global());
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
  rtn.value = ctx->addValue(result);
  return rtn;
}

/********** JSON **********/

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
  rtn.value = ctx->addValue(result);
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

/********** ValueScope **********/

ValueScope PushValueScope(ContextPtr ctx) {
  Locker locker(ctx->iso);
  return ctx->pushValueScope();
}

Bool PopValueScope(ContextPtr ctx, ValueScope scope) {
  WithIsolate _withiso(ctx->iso);

  return ctx->popValueScope(scope);
}
