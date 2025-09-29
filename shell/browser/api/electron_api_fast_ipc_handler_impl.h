// Copyright (c) 2024 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_FAST_IPC_HANDLER_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_FAST_IPC_HANDLER_H_

#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "gin/handle.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "shell/common/api/api_fast_ipc.mojom.h"
#include "shell/common/api/api_transferable_typed_array_message.mojom.h"
#include "v8/include/v8-forward.h"

namespace content {
class RenderFrameHost;
}

namespace gin_helper {
namespace internal {
class Event;
}
}

namespace electron {

namespace api {
class Session;
}

class ElectronApiFastIpcHandler 
    : public mojom::ElectronApiFastIPC,
      public content::WebContentsObserver {
 public:
  static void Create(
      content::RenderFrameHost* frame_host,
      mojo::PendingAssociatedReceiver<mojom::ElectronApiFastIPC> receiver);

  ~ElectronApiFastIpcHandler() override;

  // mojom::ElectronApiFastIPC implementation
  void SendFastIpcMessage(
      bool internal,
      const std::string& channel,
      electron::mojom::TypedArrayCloneableMessagePtr message,
      int32_t sender_id,
      SendFastIpcMessageCallback callback) override;

  void FastIpcInvoke(
      bool internal,
      const std::string& channel,
      electron::mojom::TypedArrayCloneableMessagePtr message,
      int32_t sender_id,
      FastIpcInvokeCallback callback) override;

  void FastIpcMessageSync(
      bool internal,
      const std::string& channel,
      electron::mojom::TypedArrayCloneableMessagePtr message,
      int32_t sender_id,
      FastIpcMessageSyncCallback callback) override;

  void ReceiveFastIpcPostMessage(
      const std::string& channel,
      electron::mojom::TransferableTypedArrayMessagePtr message,
      int32_t sender_id) override;

  void FastIpcMessageHost(
      const std::string& channel,
      electron::mojom::TypedArrayCloneableMessagePtr message,
      int32_t sender_id,
      FastIpcMessageHostCallback callback) override;

 private:
  ElectronApiFastIpcHandler(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<mojom::ElectronApiFastIPC> receiver);

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  void OnConnectionError();

  content::RenderFrameHost* GetRenderFrameHost();
  api::Session* GetSession();
  
  gin::Handle<gin_helper::internal::Event> MakeFastIpcEvent(
      v8::Isolate* isolate,
      api::Session* session,
      bool internal,
      FastIpcInvokeCallback callback = FastIpcInvokeCallback());

  content::GlobalRenderFrameHostId render_frame_host_id_;
  mojo::AssociatedReceiver<mojom::ElectronApiFastIPC> receiver_;

  // Stats
  static uint32_t total_transfers_;
  static double total_latency_ms_;
  static uint64_t total_bytes_transferred_;

  base::WeakPtrFactory<ElectronApiFastIpcHandler> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_FAST_IPC_HANDLER_H_