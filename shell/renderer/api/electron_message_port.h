// Copyright (c) 2024 Electron contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_API_ELECTRON_MESSAGE_PORT_H_
#define ELECTRON_SHELL_RENDERER_API_ELECTRON_MESSAGE_PORT_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/message.h"
#include "shell/common/api/api_transferable_typed_array_message.mojom.h"
#include "shell/common/gin_helper/cleaned_up_at_exit.h"
#include "third_party/blink/public/common/messaging/message_port_descriptor.h"

namespace gin {
class Arguments;
}

namespace mojo {
class Connector;
}

namespace electron {

// Renderer-side MessagePort that uses our TransferableTypedArrayMessage format
// This mirrors the main process MessagePort but runs in the renderer
class RendererMessagePort final : public gin::Wrappable<RendererMessagePort>,
                                   public gin_helper::CleanedUpAtExit,
                                   private mojo::MessageReceiver {
 public:
  ~RendererMessagePort() override;
  static gin::Handle<RendererMessagePort> Create(v8::Isolate* isolate,
                                                   v8::Local<v8::Context> context);

  void PostMessage(gin::Arguments* args);
  void Start();
  void Close();

  void Entangle(blink::MessagePortDescriptor port);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // gin_helper::CleanedUpAtExit
  void WillBeDestroyed() override;

 private:
  RendererMessagePort(v8::Isolate* isolate, v8::Local<v8::Context> context);

  // mojo::MessageReceiver
  bool Accept(mojo::Message* mojo_message) override;

  void ProcessMessage(
      std::shared_ptr<electron::mojom::TransferableTypedArrayMessagePtr> message);

  bool IsEntangled() const;
  bool IsNeutered() const;
  bool HasPendingActivity() const;

  void Pin();
  void Unpin();

  std::unique_ptr<mojo::Connector> connector_;
  bool started_ = false;
  bool closed_ = false;

  // Keep the JavaScript wrapper alive while the port is active
  v8::Global<v8::Value> pinned_;

  // The internal port owned by this class
  blink::MessagePortDescriptor port_;

  // Store the V8 context (similar to how Blink's MessagePort stores ExecutionContext)
  v8::Global<v8::Context> context_;

  base::WeakPtrFactory<RendererMessagePort> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_API_ELECTRON_MESSAGE_PORT_H_