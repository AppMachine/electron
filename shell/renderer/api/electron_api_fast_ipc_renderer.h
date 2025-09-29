// Copyright (c) 2024 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_API_ELECTRON_API_FAST_IPC_H_
#define ELECTRON_SHELL_RENDERER_API_ELECTRON_API_FAST_IPC_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/weak_ptr.h"
#include "shell/common/api/api.mojom.h"
#include "shell/common/api/api_fast_ipc.mojom.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "v8/include/v8.h"

namespace blink {
class WebServiceWorkerContextProxy;
}

namespace content {
class RenderFrame;
}

namespace electron::api {

class FastIpcBase : public gin::Wrappable<FastIpcBase>,
                          public electron::mojom::ElectronFastIpcRenderer {
 public:
  FastIpcBase(const FastIpcBase&) = delete;
  FastIpcBase& operator=(const FastIpcBase&) = delete;

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // JS API methods
  void Send(v8::Isolate* isolate,
           bool internal,
           const std::string& channel,
           v8::Local<v8::Value> args);

  v8::Local<v8::Promise> Invoke(v8::Isolate* isolate,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> args);

  v8::Local<v8::Value> SendSync(v8::Isolate* isolate,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> args);

  void PostMessage(v8::Isolate* isolate,
                  const std::string& channel,
                  v8::Local<v8::Value> message);

  void SendToHost(v8::Isolate* isolate,
                 const std::string& channel,
                 v8::Local<v8::Value> args);

  void On(const std::string& channel, v8::Local<v8::Function> handler);
  void Once(const std::string& channel, v8::Local<v8::Function> handler);
  void RemoveListener(const std::string& channel,
                     v8::Local<v8::Function> handler);
  void RemoveAllListeners(const std::string& channel);

  // mojom::ElectronFastIpcRenderer
  void ReceiveFastIpcMessage(bool internal,
                            const std::string& channel,
                            electron::mojom::TypedArrayCloneableMessagePtr message,
                            int32_t sender_id) override;

 protected:
  explicit FastIpcBase(v8::Isolate* isolate);
  ~FastIpcBase() override;

  virtual int32_t GetSenderId() = 0;

 private:
  struct InvokeCallback {
    InvokeCallback();
    ~InvokeCallback();

    v8::Global<v8::Promise::Resolver> resolver;
    v8::Global<v8::Context> context;
    raw_ptr<v8::Isolate> isolate;
  };

  void OnInvokeResponse(int32_t request_id,
                       base::ReadOnlySharedMemoryRegion response_data,
                       uint32_t response_size);

  void EmitEvent(v8::Isolate* isolate,
                const std::string& channel,
                const std::vector<v8::Local<v8::Value>>& args,
                int32_t sender_id);

  mojo::AssociatedRemote<electron::mojom::ElectronApiFastIPC> fast_ipc_;
  mojo::AssociatedReceiver<electron::mojom::ElectronFastIpcRenderer> receiver_{this};

  // Event listeners
  std::map<std::string, std::vector<v8::Global<v8::Function>>> listeners_;
  std::map<std::string, std::vector<v8::Global<v8::Function>>> once_listeners_;

  // Pending invoke callbacks
  std::map<int32_t, std::unique_ptr<InvokeCallback>> pending_invokes_;
  int32_t next_request_id_ = 1;

  raw_ptr<v8::Isolate> isolate_;

  base::WeakPtrFactory<FastIpcBase> weak_factory_{this};
};

// Renderer frame implementation
class FastIpcRenderFrame : public gin::Wrappable<FastIpcRenderFrame>,
                                   public electron::mojom::ElectronFastIpcRenderer {
 public:
  explicit FastIpcRenderFrame(v8::Isolate* isolate,
                                    content::RenderFrame* render_frame);
  ~FastIpcRenderFrame() override;

  static gin::Handle<FastIpcRenderFrame> Create(
      v8::Isolate* isolate,
      content::RenderFrame* render_frame);

  // Static method to install FastIpc API on window object
  static void Install(v8::Isolate* isolate,
                     v8::Local<v8::Context> context,
                     content::RenderFrame* render_frame);

  // Static method to get current instance for a render frame
  static FastIpcRenderFrame* GetCurrent(content::RenderFrame* render_frame);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // JS API methods (same as FastIpcBase)
  void Send(v8::Isolate* isolate,
           bool internal,
           const std::string& channel,
           v8::Local<v8::Value> args);

  v8::Local<v8::Promise> Invoke(v8::Isolate* isolate,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> args);

  v8::Local<v8::Value> SendSync(v8::Isolate* isolate,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> args);

  void PostMessage(v8::Isolate* isolate,
                  const std::string& channel,
                  v8::Local<v8::Value> message);

  void SendToHost(v8::Isolate* isolate,
                 const std::string& channel,
                 v8::Local<v8::Value> args);

  void On(const std::string& channel, v8::Local<v8::Function> handler);
  void Once(const std::string& channel, v8::Local<v8::Function> handler);
  void RemoveListener(const std::string& channel,
                     v8::Local<v8::Function> handler);
  void RemoveAllListeners(const std::string& channel);

  // mojom::ElectronFastIpcRenderer
  void ReceiveFastIpcMessage(bool internal,
                            const std::string& channel,
                            electron::mojom::TypedArrayCloneableMessagePtr message,
                            int32_t sender_id) override;

  // Version that accepts an isolate with proper context
  void FastIpcMessageWithContext(v8::Isolate* isolate,
                                       bool internal,
                                       const std::string& channel,
                                       base::ReadOnlySharedMemoryRegion shared_memory_data,
                                       uint32_t data_size,
                                       int32_t sender_id);

  // Get weak pointer for safe cross-class references
  base::WeakPtr<FastIpcRenderFrame> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  struct InvokeCallback {
    InvokeCallback();
    ~InvokeCallback();

    v8::Global<v8::Promise::Resolver> resolver;
    v8::Global<v8::Context> context;
    raw_ptr<v8::Isolate> isolate;
  };

  void OnInvokeResponse(int32_t request_id,
                       base::ReadOnlySharedMemoryRegion response_data,
                       uint32_t response_size);

  void EmitEvent(v8::Isolate* isolate,
                const std::string& channel,
                const std::vector<v8::Local<v8::Value>>& args,
                int32_t sender_id);

  int32_t GetSenderId();

  mojo::AssociatedRemote<electron::mojom::ElectronApiFastIPC> fast_ipc_;
  mojo::Receiver<electron::mojom::ElectronFastIpcRenderer> receiver_{this};

  // Event listeners
  std::map<std::string, std::vector<v8::Global<v8::Function>>> listeners_;
  std::map<std::string, std::vector<v8::Global<v8::Function>>> once_listeners_;

  // Pending invoke callbacks
  std::map<int32_t, std::unique_ptr<InvokeCallback>> pending_invokes_;
  int32_t next_request_id_ = 1;

  raw_ptr<v8::Isolate> isolate_;
  raw_ptr<content::RenderFrame> render_frame_;

  // Static map to track instances per render frame
  static std::map<content::RenderFrame*, FastIpcRenderFrame*> instances_;

  base::WeakPtrFactory<FastIpcRenderFrame> weak_factory_{this};
};

// Service worker implementation
class FastIpcServiceWorker : public gin::Wrappable<FastIpcServiceWorker>,
                                     public electron::mojom::ElectronFastIpcRenderer {
 public:
  explicit FastIpcServiceWorker(
      v8::Isolate* isolate,
      blink::WebServiceWorkerContextProxy* proxy);
  ~FastIpcServiceWorker() override;

  static gin::Handle<FastIpcServiceWorker> Create(
      v8::Isolate* isolate,
      blink::WebServiceWorkerContextProxy* proxy);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // JS API methods (same as FastIpcBase)
  void Send(v8::Isolate* isolate,
           bool internal,
           const std::string& channel,
           v8::Local<v8::Value> args);

  v8::Local<v8::Promise> Invoke(v8::Isolate* isolate,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> args);

  v8::Local<v8::Value> SendSync(v8::Isolate* isolate,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> args);

  void PostMessage(v8::Isolate* isolate,
                  const std::string& channel,
                  v8::Local<v8::Value> message);

  void SendToHost(v8::Isolate* isolate,
                 const std::string& channel,
                 v8::Local<v8::Value> args);

  void On(const std::string& channel, v8::Local<v8::Function> handler);
  void Once(const std::string& channel, v8::Local<v8::Function> handler);
  void RemoveListener(const std::string& channel,
                     v8::Local<v8::Function> handler);
  void RemoveAllListeners(const std::string& channel);

  // mojom::ElectronFastIpcRenderer
  void ReceiveFastIpcMessage(bool internal,
                            const std::string& channel,
                            electron::mojom::TypedArrayCloneableMessagePtr message,
                            int32_t sender_id) override;

 private:
  struct InvokeCallback {
    InvokeCallback();
    ~InvokeCallback();

    v8::Global<v8::Promise::Resolver> resolver;
    v8::Global<v8::Context> context;
    raw_ptr<v8::Isolate> isolate;
  };

  void OnInvokeResponse(int32_t request_id,
                       base::ReadOnlySharedMemoryRegion response_data,
                       uint32_t response_size);

  void EmitEvent(v8::Isolate* isolate,
                const std::string& channel,
                const std::vector<v8::Local<v8::Value>>& args,
                int32_t sender_id);

  int32_t GetSenderId();

  mojo::AssociatedRemote<electron::mojom::ElectronApiFastIPC> fast_ipc_;
  mojo::AssociatedReceiver<electron::mojom::ElectronFastIpcRenderer> receiver_{this};

  // Event listeners
  std::map<std::string, std::vector<v8::Global<v8::Function>>> listeners_;
  std::map<std::string, std::vector<v8::Global<v8::Function>>> once_listeners_;

  // Pending invoke callbacks
  std::map<int32_t, std::unique_ptr<InvokeCallback>> pending_invokes_;
  int32_t next_request_id_ = 1;

  raw_ptr<v8::Isolate> isolate_;
  raw_ptr<blink::WebServiceWorkerContextProxy> proxy_;

  base::WeakPtrFactory<FastIpcServiceWorker> weak_factory_{this};
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_RENDERER_API_ELECTRON_API_FAST_IPC_H_