// Copyright (c) 2025 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_FAST_IPC_DISPATCHER_TEMPLATE_H_
#define ELECTRON_SHELL_BROWSER_API_FAST_IPC_DISPATCHER_TEMPLATE_H_

#include <string>

#include "base/trace_event/trace_event.h"
#include "gin/handle.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_helper/event.h"
#include "v8/include/v8.h"

namespace electron {

// Handles dispatching Direct Transfer messages to JS.
// See fast-ipc-dispatch.ts for JS listeners.
// This follows the same pattern as IpcDispatcher.
template <typename T>
class FastIpcDispatcher {
 public:
  void FastIpcMessage(gin::Handle<gin_helper::internal::Event>& event,
                            const std::string& channel,
                            v8::Local<v8::Value> args) {
    TRACE_EVENT1("electron", "FastIpcDispatcher::Message", "channel", channel);
    emitter()->EmitWithoutEvent("-fast-ipc-message", event, channel, args);
  }

  void FastIpcInvoke(gin::Handle<gin_helper::internal::Event>& event,
                           const std::string& channel,
                           v8::Local<v8::Value> args) {
    TRACE_EVENT1("electron", "FastIpcDispatcher::Invoke", "channel", channel);
    emitter()->EmitWithoutEvent("-fast-ipc-invoke", event, channel, args);
  }

  void FastIpcMessageSync(gin::Handle<gin_helper::internal::Event>& event,
                                const std::string& channel,
                                v8::Local<v8::Value> args) {
    TRACE_EVENT1("electron", "FastIpcDispatcher::MessageSync", "channel", channel);
    emitter()->EmitWithoutEvent("-fast-ipc-message-sync", event, channel, args);
  }

  void FastIpcMessageHost(gin::Handle<gin_helper::internal::Event>& event,
                                const std::string& channel,
                                v8::Local<v8::Value> args) {
    TRACE_EVENT1("electron", "FastIpcDispatcher::MessageHost", "channel", channel);
    emitter()->EmitWithoutEvent("-fast-ipc-message-host", event, channel, args);
  }

 private:
  inline T* emitter() {
    // T must inherit from gin_helper::EventEmitterMixin<T>
    return static_cast<T*>(this);
  }
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_FAST_IPC_DISPATCHER_TEMPLATE_H_