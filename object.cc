// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.hh"


struct WithObject : public WithValue {
WithObject(ValuePtr ptr)
:WithValue(ptr)
,obj(value.As<Object>())
{ }

Local<Object> const obj;
};


/********** Object **********/

ValueRef NewObject(ContextPtr ctx) {
  WithContext _with(ctx);
  return _with.returnValue(Object::New(_with.iso()));
}

void ObjectSet(ValuePtr ptr, const char* key, int keyLen, ValuePtr prop_val) {
  WithObject _with(ptr);
  Local<String> key_val = _with.makeString(key, NewStringType::kInternalized, keyLen);
  _with.obj->Set(_with.local_ctx, key_val, Deref(prop_val)).Check();
}

int ObjectSetKey(ValuePtr ptr, ValuePtr key_val, ValuePtr prop_val) {
  WithObject _with(ptr);
  Local<Value> key = Deref(key_val);
  return key->IsName()
      && _with.obj->Set(_with.local_ctx, key, Deref(prop_val)).ToChecked();
}

void ObjectSetIdx(ValuePtr ptr, uint32_t idx, ValuePtr prop_val) {
  WithObject _with(ptr);
  _with.obj->Set(_with.local_ctx, idx, Deref(prop_val)).Check();
}


RtnValue ObjectGet(ValuePtr ptr, const char* key, int keyLen) {
  WithObject _with(ptr);
  Local<String> key_val = _with.makeString(key, NewStringType::kInternalized, keyLen);
  return _with.returnValue(_with.obj->Get(_with.local_ctx, key_val));
}

RtnValue ObjectGetKey(ValuePtr ptr, ValuePtr key) {
  WithObject _with(ptr);
  return _with.returnValue(_with.obj->Get(_with.local_ctx, Deref(key)));
}

RtnValue ObjectGetIdx(ValuePtr ptr, uint32_t idx) {
  WithObject _with(ptr);
  return _with.returnValue(_with.obj->Get(_with.local_ctx, idx));
}


int ObjectHas(ValuePtr ptr, const char* key, int keyLen) {
  WithObject _with(ptr);
  Local<String> key_val = _with.makeString(key, NewStringType::kInternalized, keyLen);
  return _with.obj->Has(_with.local_ctx, key_val).ToChecked();
}

int ObjectHasKey(ValuePtr ptr, ValuePtr key_val) {
  WithObject _with(ptr);
  return _with.obj->Has(_with.local_ctx, Deref(key_val)).ToChecked();
}

int ObjectHasIdx(ValuePtr ptr, uint32_t idx) {
  WithObject _with(ptr);
  return _with.obj->Has(_with.local_ctx, idx).ToChecked();
}


int ObjectDelete(ValuePtr ptr, const char* key, int keyLen) {
  WithObject _with(ptr);
  Local<String> key_val = _with.makeString(key, NewStringType::kInternalized, keyLen);
  return _with.obj->Delete(_with.local_ctx, key_val).ToChecked();
}

int ObjectDeleteKey(ValuePtr ptr, ValuePtr key_val) {
  WithObject _with(ptr);
  return _with.obj->Delete(_with.local_ctx, Deref(key_val)).ToChecked();
}

int ObjectDeleteIdx(ValuePtr ptr, uint32_t idx) {
  WithObject _with(ptr);
  return _with.obj->Delete(_with.local_ctx, idx).ToChecked();
}


/********** Object Internal Fields **********/

int ObjectSetInternalField(ValuePtr ptr, int idx, ValuePtr val_ptr) {
  WithObject _with(ptr);

  if (idx >= _with.obj->InternalFieldCount()) {
    return 0;
  }

  _with.obj->SetInternalField(idx, Deref(val_ptr));

  return 1;
}

ValuePtr ObjectGetInternalField(ValuePtr ptr, int idx) {
  WithObject _with(ptr);

  if (idx >= _with.obj->InternalFieldCount()) {
    return {};
  }

  Local<Value> result = _with.obj->GetInternalField(idx);

  return {ptr.ctx, ptr.ctx->addValue(result)};
}

int ObjectInternalFieldCount(ValuePtr ptr) {
  WithObject _with(ptr);
  return _with.obj->InternalFieldCount();
}


/********** Promise **********/

RtnValue NewPromiseResolver(ContextPtr ctx) {
  WithContext _with(ctx);
  return _with.returnValue(Promise::Resolver::New(_with.local_ctx));
}

ValueRef PromiseResolverGetPromise(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Promise::Resolver> resolver = _with.value.As<Promise::Resolver>();
  Local<Promise> promise = resolver->GetPromise();
  return _with.returnValue(promise);
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
  Local<Integer> cbData = Integer::New(_with.iso(), callback_ref);
  Local<Function> func;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, cbData)
           .ToLocal(&func)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  return _with.returnValue(promise->Then(_with.local_ctx, func));
}

RtnValue PromiseThen2(ValuePtr ptr, int on_fulfilled_ref, int on_rejected_ref) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Promise> promise = _with.value.As<Promise>();
  Local<Integer> onFulfilledData = Integer::New(_with.iso(), on_fulfilled_ref);
  Local<Function> onFulfilledFunc;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, onFulfilledData)
           .ToLocal(&onFulfilledFunc)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  Local<Integer> onRejectedData = Integer::New(_with.iso(), on_rejected_ref);
  Local<Function> onRejectedFunc;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, onRejectedData)
           .ToLocal(&onRejectedFunc)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  return _with.returnValue(promise->Then(_with.local_ctx, onFulfilledFunc, onRejectedFunc));
}

RtnValue PromiseCatch(ValuePtr ptr, int callback_ref) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Promise> promise = _with.value.As<Promise>();
  Local<Integer> cbData = Integer::New(_with.iso(), callback_ref);
  Local<Function> func;
  if (!Function::New(_with.local_ctx, FunctionTemplateCallback, cbData)
           .ToLocal(&func)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  return _with.returnValue(promise->Catch(_with.local_ctx, func));
}

ValueRef PromiseResult(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Promise> promise = _with.value.As<Promise>();
  return _with.returnValue(promise->Result());
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
  buildCallArguments(_with.iso(), argv, argc, args);

  Local<Value> local_recv = Deref(recv);

  Local<Value> result;
  if (!fn->Call(_with.local_ctx, local_recv, argc, argv).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->addValue(result);
  return rtn;
}

RtnValue FunctionNewInstance(ValuePtr ptr, int argc, ValuePtr args[]) {
  WithValue _with(ptr);
  RtnValue rtn = {};
  Local<Function> fn = Local<Function>::Cast(_with.value);
  Local<Value> argv[argc];
  buildCallArguments(_with.iso(), argv, argc, args);
  Local<Object> result;
  if (!fn->NewInstance(_with.local_ctx, argc, argv).ToLocal(&result)) {
    rtn.error = _with.exceptionError();
    return rtn;
  }
  rtn.value = ptr.ctx->addValue(result);
  return rtn;
}

ValueRef FunctionSourceMapUrl(ValuePtr ptr) {
  WithValue _with(ptr);
  Local<Function> fn = Local<Function>::Cast(_with.value);
  Local<Value> result = fn->GetScriptOrigin().SourceMapUrl();
  return ptr.ctx->addValue(result);
}

/********** Array **********/

extern ValueRef NewArray(ContextPtr ctx, uint32_t length) {
  WithContext _with(ctx);
  return _with.returnValue(Array::New(_with.iso(), length));
}

uint32_t ArrayLength(ValuePtr ptr) {
  WithObject _with(ptr);
  if (_with.obj->IsArray()) {
    return _with.obj.As<Array>()->Length();
  } else {
    return 0;
  }
}
