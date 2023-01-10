// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.hh"

namespace v8go {

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

  RtnError ExceptionError(TryCatch& try_catch,
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

}


/********** v8::V8 **********/

const char* Version() {
  return V8::GetVersion();
}

void SetFlags(const char* flags) {
  V8::SetFlagsFromString(flags);
}
