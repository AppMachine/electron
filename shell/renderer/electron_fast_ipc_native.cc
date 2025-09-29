// Copyright (c) 2024 The Electron Authors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/shell/renderer/electron_fast_ipc_native.h"

#include <iostream>
#include <vector>

#include "base/memory/read_only_shared_memory_region.h"
#include "base/trace_event/trace_event.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_util.h"
#include "shell/common/typed_array_v8_serializer.h"
#include "shell/common/api/api_typed_array_cloneable_message.mojom.h"
#include "third_party/blink/public/web/blink.h"

namespace electron::fast_ipc_native {

namespace {

constexpr std::string_view kFastIpcKey = "fastIpcNative";

// Gets the private object under kFastIpcKey
v8::Local<v8::Object> GetFastIpcObject(const v8::Local<v8::Context>& context) {
  auto* isolate = context->GetIsolate();
  auto binding_key = gin::StringToV8(isolate, kFastIpcKey);
  auto private_binding_key = v8::Private::ForApi(isolate, binding_key);
  auto global_object = context->Global();
  auto value =
      global_object->GetPrivate(context, private_binding_key).ToLocalChecked();
  if (value.IsEmpty() || !value->IsObject()) {
    LOG(ERROR) << "Attempted to get the 'fastIpcNative' object but it was missing";
    return {};
  }
  return value->ToObject(context).ToLocalChecked();
}

void InvokeFastIpcCallback(const v8::Local<v8::Context>& context,
                          const std::string& callback_name,
                          std::vector<v8::Local<v8::Value>> args) {
  TRACE_EVENT0("devtools.timeline", "FastIpcFunctionCall");
  auto* isolate = context->GetIsolate();


  auto fastIpcNative = GetFastIpcObject(context);
  if (fastIpcNative.IsEmpty()) {
    return;
  }

  // Only set up the node::CallbackScope if there's a node environment.
  // Sandboxed renderers don't have a node environment.
  std::unique_ptr<node::CallbackScope> callback_scope;
  if (node::Environment::GetCurrent(context)) {
    callback_scope = std::make_unique<node::CallbackScope>(
        isolate, fastIpcNative, node::async_context{0, 0});
  }

  auto callback_key = gin::ConvertToV8(isolate, callback_name)
                          ->ToString(context)
                          .ToLocalChecked();
  auto callback_value = fastIpcNative->Get(context, callback_key).ToLocalChecked();
  if (!callback_value->IsFunction()) {
    LOG(ERROR) << "FastIPC callback '" << callback_name << "' is not a function";
    return;
  }
  auto callback = callback_value.As<v8::Function>();
  auto result = callback->Call(context, fastIpcNative, args.size(), args.data());
  if (result.IsEmpty()) {
    LOG(ERROR) << "FastIPC callback '" << callback_name << "' failed";
  }
}

v8::Local<v8::Value> DeserializeSharedMemoryData(v8::Isolate* isolate,
                                                  const ::electron::mojom::TypedArrayCloneableMessage& message) {
  TRACE_EVENT0("electron", "FastIPC::DeserializeSharedMemoryData");

  v8::Local<v8::Value> result =
      electron::DeserializeV8ValueWithTypedArrays(
          isolate, message);

  if (result.IsEmpty()) {
    LOG(ERROR) << "Failed to deserialize shared memory data";
    return v8::Undefined(isolate);
  }

  return result;
}

}  // namespace

void EmitFastIPCEvent(const v8::Local<v8::Context>& context,
                      bool internal,
                      const std::string& channel,
                      const ::electron::mojom::TypedArrayCloneableMessage& message) {
  auto* isolate = context->GetIsolate();

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);
  v8::MicrotasksScope script_scope(isolate, context->GetMicrotaskQueue(),
                                   v8::MicrotasksScope::kRunMicrotasks);

  // Deserialize the data from FastIpcMessage
  v8::Local<v8::Value> args = DeserializeSharedMemoryData(isolate, message);

  // Pass arguments matching regular IPC: (internal, channel, ports, args)
  std::vector<v8::Local<v8::Value>> argv = {
      gin::ConvertToV8(isolate, internal),
      gin::ConvertToV8(isolate, channel),
      gin::ConvertToV8(isolate, std::vector<v8::Local<v8::Value>>()),  // empty ports
      args
  };

  InvokeFastIpcCallback(context, "onMessage", argv);
}

}  // namespace electron::fast_ipc_native
