// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.hh"


struct WithTemplate : public WithIsolate {
  WithTemplate(TemplatePtr tmpl_ptr)
  :WithIsolate(tmpl_ptr->iso)
  ,iso(tmpl_ptr->iso)
  ,tmpl(tmpl_ptr->ptr.Get(iso))
  { }

  Isolate* const  iso;
  Local<Template> tmpl;
};


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

  V8GoTemplate* ot = new V8GoTemplate;
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

  rtn.value = ctx->addValue(obj);
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

namespace v8go {
  // declared in v8go.hh
  void FunctionTemplateCallback(const FunctionCallbackInfo<Value>& info) {
    Isolate* iso = info.GetIsolate();
    WithIsolate _withiso(iso);

    // This callback function can be called from any Context, which we only know
    // at runtime. We extract the Context reference from the embedder data so that
    // we can use the context registry to match the Context on the Go side
    Local<Context> local_ctx = iso->GetCurrentContext();
    V8GoContext* ctx = V8GoContext::fromContext(local_ctx);

    int callback_ref = info.Data().As<Integer>()->Value();

    ValueRef _this = ctx->addValue(info.This());

    int args_count = info.Length();
    ValueRef thisAndArgs[args_count + 1];
    thisAndArgs[0] = _this;
    for (int i = 0; i < args_count; i++) {
      thisAndArgs[1+i] = ctx->addValue(info[i]);
    }

    ValuePtr val = goFunctionCallback(ctx->goRef, callback_ref, thisAndArgs, args_count);
    if (val.ctx != nullptr) {
      info.GetReturnValue().Set(Deref(val));
    } else {
      info.GetReturnValue().SetUndefined();
    }
  }
}

TemplatePtr NewFunctionTemplate(IsolatePtr iso, int callback_ref) {
  WithIsolate _withiso(iso);

  // (rogchap) We only need to store one value, callback_ref, into the
  // C++ callback function data, but if we needed to store more items we could
  // use an V8::Array; this would require the internal context from
  // iso->GetData(0)
  Local<Integer> cbData = Integer::New(iso, callback_ref);

  V8GoTemplate* ot = new V8GoTemplate;
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

  rtn.value = ctx->addValue(fn);
  return rtn;
}
