// Copyright (c) 2024 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_helper/fast_ipc_reply_channel.h"

#include <cstring>

#include "base/debug/stack_trace.h"
#include "base/time/time.h"
#include "mojo/public/cpp/base/big_buffer.h"
#include "gin/data_object_builder.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/api/api_typed_array_cloneable_message.mojom.h"
#include "shell/common/typed_array_v8_serializer.h"
#include "shell/common/gin_converters/value_converter.h"

namespace gin_helper::internal {

// static
using InvokeCallback = base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>;
gin::Handle<FastIpcReplyChannel> FastIpcReplyChannel::Create(
    v8::Isolate* isolate,
    InvokeCallback callback) {
  return gin::CreateHandle(isolate, new FastIpcReplyChannel(std::move(callback)));
}

gin::ObjectTemplateBuilder FastIpcReplyChannel::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<FastIpcReplyChannel>::GetObjectTemplateBuilder(isolate)
      .SetMethod("sendReply", &FastIpcReplyChannel::SendReply);
}

const char* FastIpcReplyChannel::GetTypeName() {
  return "FastIpcReplyChannel";
}

FastIpcReplyChannel::FastIpcReplyChannel(InvokeCallback callback)
    : callback_(std::move(callback)) {}

FastIpcReplyChannel::~FastIpcReplyChannel() {
  if (callback_)
    SendError("reply was never sent");
}

void FastIpcReplyChannel::SendError(const std::string& msg) {

  if (!callback_) {
    return;
  }

  // For errors from destructor, send an empty message
  auto empty_message = electron::mojom::TypedArrayCloneableMessage::New();
  std::move(callback_).Run(std::move(empty_message));
}

bool FastIpcReplyChannel::SendReply(v8::Isolate* isolate, v8::Local<v8::Value> arg) {
  if (!callback_)
    return false;

  v8::Local<v8::Value> transfer_list = v8::Array::New(isolate, 0);
  electron::SerializeV8ValueWithTypedArraysAsync(
      isolate, arg, transfer_list,
      base::BindOnce(
          [](InvokeCallback callback,
             electron::mojom::TypedArrayCloneableMessagePtr typed_msg) {
            if (!typed_msg) {
              auto empty_message = electron::mojom::TypedArrayCloneableMessage::New();
              std::move(callback).Run(std::move(empty_message));
              return;
            }
            std::move(callback).Run(std::move(typed_msg));
          },
          std::move(callback_)));

  return true;
}

gin::WrapperInfo FastIpcReplyChannel::kWrapperInfo = {gin::kEmbedderNativeGin};

}  // namespace gin_helper::internal
