// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8go.hh"

namespace v8go {

  static inline bool isAscii(RtnString str) {
    // Yes, this is faster than testing every char, because it avoids conditional jumps.
    char merged = 0;
    for (size_t i = 0; i < str.length; ++i) {
      merged |= str.data[i];
    }
    return (merged & 0x80) == 0;
  }

  RtnString CopyString(Isolate *iso, Local<String> str, char *buffer, size_t bufferSize) {
    // Note: This is performance-sensitive, since it's how V8 strings get returned to Go.
    RtnString result;
    if (str->IsOneByte()) {
      // String is known to be ISO-8859-1 compatible; assume it's ASCII and copy it:
      void *alloced = nullptr;
      result.length = str->Length();
      if (result.length < bufferSize) {
        result.data = buffer;
      } else {
        alloced = malloc(result.length + 1);
        result.data = (char*)alloced;
      }
      str->WriteOneByte(iso, (uint8_t*)result.data);
      if (isAscii(result)) {
        return result;
      }
      // Oops, it's not ASCII. Start over...
      free(alloced);
    }

    // General case; do the slower UTF-8 conversion:
    result.length = str->Utf8Length(iso);
    if (result.length < bufferSize) {
      result.data = buffer;
    } else {
      result.data = (char*)malloc(result.length + 1);
    }
    str->WriteUtf8(iso, (char*)result.data);
    return result;
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
