// Copyright (c) 2024 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_HELPER_FAST_IPC_REPLY_CHANNEL_H_
#define ELECTRON_SHELL_COMMON_GIN_HELPER_FAST_IPC_REPLY_CHANNEL_H_

#include "base/memory/read_only_shared_memory_region.h"
#include "gin/wrappable.h"
#include "shell/common/api/api_fast_ipc.mojom.h"

namespace gin {
template <typename T>
class Handle;
}  // namespace gin

namespace v8 {
class Isolate;
template <typename T>
class Local;
class Object;
class ObjectTemplate;
}  // namespace v8

namespace gin_helper::internal {

// This object wraps the FastIpcInvokeCallback so that if it gets GC'd by V8, we can
// still call the callback and send an error. Not doing so causes a Mojo DCHECK,
// since Mojo requires callbacks to be called before they are destroyed.
class FastIpcReplyChannel : public gin::Wrappable<FastIpcReplyChannel> {
 public:
  using InvokeCallback = electron::mojom::ElectronApiFastIPC::FastIpcInvokeCallback;
  static gin::Handle<FastIpcReplyChannel> Create(v8::Isolate* isolate,
                                                        InvokeCallback callback);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  void SendError(const std::string& msg);

 private:
  explicit FastIpcReplyChannel(InvokeCallback callback);
  ~FastIpcReplyChannel() override;

  bool SendReply(v8::Isolate* isolate, v8::Local<v8::Value> arg);

  InvokeCallback callback_;
};

}  // namespace gin_helper::internal

#endif  // ELECTRON_SHELL_COMMON_GIN_HELPER_FAST_IPC_REPLY_CHANNEL_H_