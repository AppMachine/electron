// Copyright (c) 2024 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <map>
#include "base/memory/read_only_shared_memory_region.h"
#include "shell/browser/api/electron_api_fast_ipc_handler_impl.h"
#include "shell/common/api/api_typed_array_cloneable_message.mojom.h"
#include "shell/common/typed_array_v8_serializer.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"

namespace {

// Storage for pending callbacks from the handler
std::map<int32_t, electron::ElectronApiFastIpcHandler::FastIpcInvokeCallback> g_pending_callbacks;

void SendInvokeReply(v8::Isolate* isolate,
                    v8::Local<v8::Value> callback_id_value,
                    v8::Local<v8::Value> response_value) {
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  // Get callback ID
  int32_t callback_id = callback_id_value->Int32Value(context).ToChecked();

  // Find the pending callback
  auto it = g_pending_callbacks.find(callback_id);
  if (it == g_pending_callbacks.end()) {
    return;
  }

  auto callback = std::move(it->second);
  g_pending_callbacks.erase(it);

  // Get the response object
  v8::Local<v8::Object> response = response_value->ToObject(context).ToLocalChecked();

  // Check if it's an error or result
  v8::Local<v8::String> error_key = v8::String::NewFromUtf8Literal(isolate, "error");
  v8::Local<v8::String> result_key = v8::String::NewFromUtf8Literal(isolate, "result");

  if (response->Has(context, error_key).ToChecked()) {
    // Error response - send empty message
    auto empty_message = electron::mojom::TypedArrayCloneableMessage::New();
    std::move(callback).Run(std::move(empty_message));
  } else if (response->Has(context, result_key).ToChecked()) {
    // Success response - serialize the result
    v8::Local<v8::Value> result = response->Get(context, result_key).ToLocalChecked();
    v8::Local<v8::Value> transfer_list = v8::Array::New(isolate, 0);
    electron::SerializeV8ValueWithTypedArraysAsync(
        isolate, result, transfer_list,
        base::BindOnce([](electron::ElectronApiFastIpcHandler::FastIpcInvokeCallback callback,
                          electron::mojom::TypedArrayCloneableMessagePtr typed_msg) {
          if (typed_msg) {
            std::move(callback).Run(std::move(typed_msg));
          } else {
            // Fallback on serialization error
            auto empty_message = electron::mojom::TypedArrayCloneableMessage::New();
            std::move(callback).Run(std::move(empty_message));
          }
        }, std::move(callback)));
  }
}

// Function to store callbacks from the handler
void StoreInvokeCallback(int32_t callback_id,
                         electron::ElectronApiFastIpcHandler::FastIpcInvokeCallback callback) {
  g_pending_callbacks[callback_id] = std::move(callback);
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);

  // Methods for JavaScript to send responses back to C++
  dict.SetMethod("sendInvokeReply", &SendInvokeReply);

  // Placeholder properties for JavaScript to set
  dict.SetMethod("onMessage", [](){});
  dict.SetMethod("onInvoke", [](){});
  dict.SetMethod("onMessageSync", [](){});
}

}  // namespace

// Export the callback storage function for use by the handler
namespace electron {
void StoreFastIpcCallback(int32_t callback_id,
                                 ElectronApiFastIpcHandler::FastIpcInvokeCallback callback) {
  StoreInvokeCallback(callback_id, std::move(callback));
}
}  // namespace electron

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_fast_ipc, Initialize)
