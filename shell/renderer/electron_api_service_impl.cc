// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/shell/renderer/electron_api_service_impl.h"

#include <string_view>
#include <utility>
#include <vector>

#include "base/trace_event/trace_event.h"
#include "gin/data_object_builder.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "shell/common/api/api_transferable_typed_array_message.mojom.h"
#include "shell/common/electron_constants.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/heap_snapshot.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "shell/common/thread_restrictions.h"
#include "shell/common/transferable_typed_array_v8_serializer.h"
#include "shell/renderer/electron_ipc_native.h"
#include "shell/renderer/electron_fast_ipc_native.h"
#include "shell/renderer/electron_render_frame_observer.h"
#include "shell/renderer/renderer_client_base.h"
#include "shell/renderer/api/electron_api_fast_ipc_renderer.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom-shared.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "shell/renderer/api/electron_message_port.h"

namespace electron {

// Inner class to handle FastIpc interface binding
class ElectronApiServiceImpl::FastIpcInterface
    : public mojom::ElectronFastIpcRenderer {
 public:
  FastIpcInterface(content::RenderFrame* render_frame,
                         RendererClientBase* renderer_client)
      : render_frame_(render_frame),
        renderer_client_(renderer_client) {
    // We'll create/retrieve the FastIpcRenderFrame instance on demand
    // to avoid holding stale pointers
  }

  ~FastIpcInterface() override = default;

  void Bind(mojo::PendingReceiver<mojom::ElectronFastIpcRenderer> receiver) {
    if (receiver_.is_bound()) {
      receiver_.reset();
    }
    receiver_.Bind(std::move(receiver));
    receiver_.set_disconnect_handler(base::BindOnce(
        &FastIpcInterface::OnConnectionError, base::Unretained(this)));
  }

  void OnConnectionError() {
    if (receiver_.is_bound()) {
      receiver_.reset();
    }
  }

  // mojom::ElectronFastIpcRenderer implementation
  void ReceiveFastIpcMessage(bool internal,
                            const std::string& channel,
                            electron::mojom::TypedArrayCloneableMessagePtr message,
                            int32_t sender_id) override {
    // Check if receiver is still bound before processing
    if (!receiver_.is_bound()) {
      return;
    }

    // Get the frame and validate it
    blink::WebLocalFrame* frame = render_frame_->GetWebFrame();
    if (!frame) {
      return;
    }

    // Get fresh isolate and create proper V8 context
    v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();

    // Check if isolate is in a valid state for execution
    if (isolate->IsExecutionTerminating()) {
      return;
    }

    v8::HandleScope handle_scope(isolate);

    // Get context from renderer client if available
    v8::Local<v8::Context> context;
    if (renderer_client_) {
      context = renderer_client_->GetContext(frame, isolate);
    } else {
      context = frame->MainWorldScriptContext();
    }

    if (context.IsEmpty()) {
      return;
    }

    v8::Context::Scope context_scope(context);

    // Use the native helper to emit the event to the global callback
    // This follows the same pattern as regular IPC, storing the callback in global context
    electron::fast_ipc_native::EmitFastIPCEvent(context, internal, channel, *message);
  }

 private:
  raw_ptr<content::RenderFrame> render_frame_;
  raw_ptr<RendererClientBase> renderer_client_;
  mojo::Receiver<mojom::ElectronFastIpcRenderer> receiver_{this};
};

ElectronApiServiceImpl::~ElectronApiServiceImpl() = default;

ElectronApiServiceImpl::ElectronApiServiceImpl(
    content::RenderFrame* render_frame,
    RendererClientBase* renderer_client)
    : content::RenderFrameObserver(render_frame),
      renderer_client_(renderer_client) {
  registry_.AddInterface<mojom::ElectronRenderer>(base::BindRepeating(
      &ElectronApiServiceImpl::BindTo, base::Unretained(this)));

  // Register Direct Transfer interface
  registry_.AddInterface<mojom::ElectronFastIpcRenderer>(base::BindRepeating(
      &ElectronApiServiceImpl::BindFastIpc, base::Unretained(this)));
}

void ElectronApiServiceImpl::BindTo(
    mojo::PendingReceiver<mojom::ElectronRenderer> receiver) {
  if (document_created_) {
    if (receiver_.is_bound())
      receiver_.reset();

    receiver_.Bind(std::move(receiver));
    receiver_.set_disconnect_handler(base::BindOnce(
        &ElectronApiServiceImpl::OnConnectionError, GetWeakPtr()));
  } else {
    pending_receiver_ = std::move(receiver);
  }
}

void ElectronApiServiceImpl::BindFastIpc(
    mojo::PendingReceiver<mojom::ElectronFastIpcRenderer> receiver) {
  // Create the FastIpc interface if not already created
  if (!fast_ipc_interface_) {
    fast_ipc_interface_ = std::make_unique<FastIpcInterface>(render_frame(), renderer_client_);
  }

  // Bind the receiver
  fast_ipc_interface_->Bind(std::move(receiver));
}

void ElectronApiServiceImpl::OnInterfaceRequestForFrame(
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle* interface_pipe) {
  registry_.TryBindInterface(interface_name, interface_pipe);
}

void ElectronApiServiceImpl::DidCreateDocumentElement() {
  document_created_ = true;

  if (pending_receiver_) {
    if (receiver_.is_bound())
      receiver_.reset();

    receiver_.Bind(std::move(pending_receiver_));
    receiver_.set_disconnect_handler(base::BindOnce(
        &ElectronApiServiceImpl::OnConnectionError, GetWeakPtr()));
  }
}

void ElectronApiServiceImpl::OnDestruct() {
  delete this;
}

void ElectronApiServiceImpl::OnConnectionError() {
  if (receiver_.is_bound())
    receiver_.reset();
}

void ElectronApiServiceImpl::Message(bool internal,
                                     const std::string& channel,
                                     blink::CloneableMessage arguments) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame)
    return;

  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::Context> context = renderer_client_->GetContext(frame, isolate);
  v8::Context::Scope context_scope(context);

  v8::Local<v8::Value> args = gin::ConvertToV8(isolate, arguments);

  ipc_native::EmitIPCEvent(context, internal, channel, {}, args);
}

void ElectronApiServiceImpl::ReceivePostMessage(
    const std::string& channel,
    electron::mojom::TransferableTypedArrayMessagePtr message) {
  std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] Received message on channel: " << channel << std::endl;
  std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] Message has " << message->ports.size() << " ports" << std::endl;

  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] No WebLocalFrame - dropping message" << std::endl;
    return;
  }

  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::Context> context = renderer_client_->GetContext(frame, isolate);
  v8::Context::Scope context_scope(context);

  // Use our new deserializer with shared memory support
  v8::Local<v8::Value> message_value = DeserializeV8ValueWithTransfer(isolate, *message);

  std::vector<v8::Local<v8::Value>> ports;
  for (auto& port : message->ports) {
    // Use our custom renderer MessagePort instead of WebMessagePortConverter
    // Pass the context just like WebMessagePortConverter does
    std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] Creating RendererMessagePort" << std::endl;
    auto renderer_port = RendererMessagePort::Create(isolate, context);
    renderer_port->Entangle(std::move(port));
    ports.emplace_back(renderer_port->GetWrapper(isolate).ToLocalChecked());
    std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] RendererMessagePort created and entangled" << std::endl;
  }

  std::vector<v8::Local<v8::Value>> args = {message_value};

  std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] About to emit IPC event on channel: " << channel
            << " with " << ports.size() << " ports" << std::endl;

  ipc_native::EmitIPCEvent(context, false, channel, ports,
                           gin::ConvertToV8(isolate, args));

  std::cout << "[ElectronApiServiceImpl::ReceivePostMessage] IPC event emitted" << std::endl;
}

void ElectronApiServiceImpl::TakeHeapSnapshot(
    mojo::ScopedHandle file,
    TakeHeapSnapshotCallback callback) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame)
    return;

  ScopedAllowBlockingForElectron allow_blocking;

  base::ScopedPlatformFile platform_file;
  if (mojo::UnwrapPlatformFile(std::move(file), &platform_file) !=
      MOJO_RESULT_OK) {
    LOG(ERROR) << "Unable to get the file handle from mojo.";
    std::move(callback).Run(false);
    return;
  }
  base::File base_file(std::move(platform_file));

  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  bool success = electron::TakeHeapSnapshot(isolate, &base_file);

  std::move(callback).Run(success);
}

}  // namespace electron
