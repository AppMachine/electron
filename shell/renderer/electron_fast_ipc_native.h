// Copyright (c) 2024 The Electron Authors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_ELECTRON_FAST_IPC_NATIVE_H_
#define ELECTRON_SHELL_RENDERER_ELECTRON_FAST_IPC_NATIVE_H_

#include <string>
#include <vector>

#include "v8/include/v8.h"

namespace electron::mojom {
class TypedArrayCloneableMessage;
}

namespace electron::fast_ipc_native {

// Emit a FastIPC event by retrieving the callback from the global context
// and invoking it with the deserialized message data.
// This follows the same pattern as electron_ipc_native but handles
// shared memory regions for performance.
void EmitFastIPCEvent(const v8::Local<v8::Context>& context,
                      bool internal,
                      const std::string& channel,
                      const ::electron::mojom::TypedArrayCloneableMessage& message);

}  // namespace electron::fast_ipc_native

#endif  // ELECTRON_SHELL_RENDERER_ELECTRON_FAST_IPC_NATIVE_H_