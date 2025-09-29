// Copyright (c) 2024 The Electron Authors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string>
#include <cstring>

#include "base/strings/stringprintf.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/worker_thread.h"
#include "gin/dictionary.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "gin/wrappable.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "shell/common/api/api_fast_ipc.mojom.h"
#include "shell/common/api/api_typed_array_cloneable_message.mojom.h"
#include "shell/common/api/api_transferable_typed_array_message.mojom.h"
#include "shell/common/typed_array_v8_serializer.h"
#include "shell/common/transferable_typed_array_v8_serializer.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/function_template_extensions.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_util.h"
#include "shell/renderer/preload_realm_context.h"
#include "shell/renderer/service_worker_data.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/web/modules/service_worker/web_service_worker_context_proxy.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_message_port_converter.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"  // nogncheck

using blink::WebLocalFrame;
using content::RenderFrame;

namespace {

const char kFastIPCMethodCalledAfterContextReleasedError[] =
    "FastIPC method called after context was released";

RenderFrame* GetCurrentRenderFrame() {
  WebLocalFrame* frame = WebLocalFrame::FrameForCurrentContext();
  if (!frame)
    return nullptr;

  return RenderFrame::FromWebFrame(frame);
}


template <typename T>
class FastIPCBase : public gin::Wrappable<T> {
 public:
  static gin::WrapperInfo kWrapperInfo;

  static gin::Handle<T> Create(v8::Isolate* isolate) {
    return gin::CreateHandle(isolate, new T(isolate));
  }

  base::WeakPtr<FastIPCBase> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void SendMessage(v8::Isolate* isolate,
                   gin_helper::ErrorThrower thrower,
                   bool internal,
                   const std::string& channel,
                   v8::Local<v8::Value> arguments) {
    if (!electron_fast_ipc_remote_) {
      thrower.ThrowError(kFastIPCMethodCalledAfterContextReleasedError);
      return;
    }

    // Get sender ID (use render frame ID or 0)
    int32_t sender_id = render_frame_ ? render_frame_->GetRoutingID() : 0;

    // Use async serialization with TypedArrayV8Serializer
    v8::Local<v8::Value> transfer_list = v8::Array::New(isolate, 0);
    electron::SerializeV8ValueWithTypedArraysAsync(
        isolate, arguments, transfer_list,
        base::BindOnce(
            [](base::WeakPtr<FastIPCBase> self,
               bool internal,
               std::string channel,
               int32_t sender_id,
               electron::mojom::TypedArrayCloneableMessagePtr typed_msg) {
              if (!self) {
                // FastIPC instance destroyed during serialization
                return;
              }

              if (!self->electron_fast_ipc_remote_) {
                // Remote disconnected during serialization
                return;
              }

              if (!typed_msg) {
                // Serialization failed - log error but don't throw since we're in callback
                LOG(ERROR) << "FastIPC serialization failed";
                return;
              }

              if (typed_msg->base_message.encoded_message.size() == 0) {
                LOG(ERROR) << "Empty buffer after serialization";
                return;
              }

              // Send via FastIPC with zero-copy message
              self->electron_fast_ipc_remote_->SendFastIpcMessage(
                  internal, channel, std::move(typed_msg),
                  sender_id,
                  base::DoNothing());
            },
            this->GetWeakPtr(), internal, channel, sender_id));
  }

  v8::Local<v8::Value> SendSync(v8::Isolate* isolate,
                                 gin_helper::ErrorThrower thrower,
                                 bool internal,
                                 const std::string& channel,
                                 v8::Local<v8::Value> arguments) {
    if (!electron_fast_ipc_remote_) {
      thrower.ThrowError(kFastIPCMethodCalledAfterContextReleasedError);
      return v8::Undefined(isolate);
    }

    // Use SerializeSync with TypedArrayV8Serializer
    auto typed_msg = electron::mojom::TypedArrayCloneableMessage::New();
    bool success = electron::SerializeV8ValueWithTypedArrays(isolate, arguments, typed_msg.get());

    if (!success) {
      thrower.ThrowError("Failed to serialize for FastIPC");
      return v8::Undefined(isolate);
    }

    // Get sender ID (use render frame ID or 0)
    int32_t sender_id = render_frame_ ? render_frame_->GetRoutingID() : 0;

    // Send sync via FastIPC and get response
    electron::mojom::TypedArrayCloneableMessagePtr response;
    electron_fast_ipc_remote_->FastIpcMessageSync(
        internal, channel, std::move(typed_msg),
        sender_id,
        &response);

    // Deserialize response
    if (response && response->base_message.encoded_message.size() > 0) {
      // Use synchronous deserialization with TypedArrayV8Serializer
      v8::Local<v8::Value> result_value = electron::DeserializeV8ValueWithTypedArrays(
          isolate, *response);
      if (!result_value.IsEmpty() && result_value->IsArray()) {
        v8::Local<v8::Array> arr = result_value.As<v8::Array>();
        if (arr->Length() > 0) {
          return arr->Get(isolate->GetCurrentContext(), 0).ToLocalChecked();
        }
      }
    }

    return v8::Undefined(isolate);
  }

  v8::Local<v8::Promise> Invoke(v8::Isolate* isolate,
                                gin_helper::ErrorThrower thrower,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> arguments) {
    if (!electron_fast_ipc_remote_) {
      thrower.ThrowError(kFastIPCMethodCalledAfterContextReleasedError);
      return v8::Local<v8::Promise>();
    }

    gin_helper::Promise<v8::Local<v8::Value>> p(isolate);
    auto handle = p.GetHandle();

    // Get sender ID (use render frame ID or 0)
    int32_t sender_id = render_frame_ ? render_frame_->GetRoutingID() : 0;

    // Arguments from JS is always an array (due to ...args)
    // We want to serialize the contents of that array, not the array itself
    // This matches how regular IPC works with CloneableMessage

    // Use async serialization with TypedArrayV8Serializer
    v8::Local<v8::Value> transfer_list = v8::Array::New(isolate, 0);
    electron::SerializeV8ValueWithTypedArraysAsync(
        isolate, arguments, transfer_list,
        base::BindOnce(
            [](base::WeakPtr<FastIPCBase> self,
               gin_helper::Promise<v8::Local<v8::Value>> p,
               bool internal,
               std::string channel,
               int32_t sender_id,
               electron::mojom::TypedArrayCloneableMessagePtr typed_msg) {
              if (!self) {
                p.RejectWithErrorMessage("FastIPC instance destroyed during serialization");
                return;
              }

              if (!self->electron_fast_ipc_remote_) {
                p.RejectWithErrorMessage("FastIPC remote disconnected during serialization");
                return;
              }

              if (!typed_msg) {
                p.RejectWithErrorMessage("Serialization failed");
                return;
              }

              if (typed_msg->base_message.encoded_message.size() == 0) {
                p.RejectWithErrorMessage("Empty buffer after serialization");
                return;
              }

              // Send via Mojo from main thread (after background serialization)
              self->electron_fast_ipc_remote_->FastIpcInvoke(
                  internal, channel, std::move(typed_msg),
                  sender_id,
                  base::BindOnce(
                      [](gin_helper::Promise<v8::Local<v8::Value>> p,
                         electron::mojom::TypedArrayCloneableMessagePtr response) {
                        // Ensure we have proper V8 context and scope
                        v8::Isolate* isolate = v8::Isolate::GetCurrent();
                        v8::HandleScope handle_scope(isolate);
                        v8::Local<v8::Context> context = isolate->GetCurrentContext();
                        if (context.IsEmpty()) {
                          // Try to get a context from the promise's creation context
                          context = p.GetContext();
                          if (context.IsEmpty()) {
                            p.RejectWithErrorMessage("No V8 context available");
                            return;
                          }
                        }
                        v8::Context::Scope context_scope(context);

                        // Use async deserialization with TypedArrayV8Serializer
                        if (response && response->base_message.encoded_message.size() > 0) {
                          electron::DeserializeV8ValueWithTypedArraysAsync(
                              isolate, *response,
                              base::BindOnce([](gin_helper::Promise<v8::Local<v8::Value>> p,
                                              v8::Local<v8::Value> result) {
                                if (!result.IsEmpty()) {
                                  p.Resolve(result);
                                } else {
                                  p.RejectWithErrorMessage("Failed to deserialize response");
                                }
                              }, std::move(p)));
                        } else {
                          p.RejectWithErrorMessage("Empty response received");
                        }
                      },
                      std::move(p)));
            },
            this->GetWeakPtr(), std::move(p), internal, channel, sender_id));

    return handle;
  }

  void PostMessage(v8::Isolate* isolate,
                   gin_helper::ErrorThrower thrower,
                   const std::string& channel,
                   v8::Local<v8::Value> message_value,
                   std::optional<v8::Local<v8::Value>> transfer) {
    if (!electron_fast_ipc_remote_) {
      thrower.ThrowError(kFastIPCMethodCalledAfterContextReleasedError);
      return;
    }

    // Use transfer list or empty array
    v8::Local<v8::Value> transfer_list =
        (transfer && !transfer.value()->IsUndefined())
        ? transfer.value()
        : v8::Array::New(isolate, 0);

    // Get sender ID (use render frame ID or 0)
    int32_t sender_id = render_frame_ ? render_frame_->GetRoutingID() : 0;

    // Use async serialization with transfer support
    electron::SerializeV8ValueWithTransferAsync(
        isolate, message_value, transfer_list,
        base::BindOnce(
            [](base::WeakPtr<FastIPCBase> self,
               std::string channel,
               int32_t sender_id,
               electron::mojom::TransferableTypedArrayMessagePtr message) {
              if (!self) {
                // FastIPC instance destroyed during serialization
                return;
              }

              if (!self->electron_fast_ipc_remote_) {
                // Remote disconnected during serialization
                return;
              }

              self->electron_fast_ipc_remote_->ReceiveFastIpcPostMessage(
                  channel, std::move(message), sender_id);
            },
            this->GetWeakPtr(), channel, sender_id));
  }

  void SendToHost(v8::Isolate* isolate,
                  gin_helper::ErrorThrower thrower,
                  const std::string& channel,
                  v8::Local<v8::Value> arguments) {
    if (!electron_fast_ipc_remote_) {
      thrower.ThrowError(kFastIPCMethodCalledAfterContextReleasedError);
      return;
    }

    // Get sender ID (use render frame ID or 0)
    int32_t sender_id = render_frame_ ? render_frame_->GetRoutingID() : 0;

    // Use async serialization with TypedArrayV8Serializer
    v8::Local<v8::Value> transfer_list = v8::Array::New(isolate, 0);
    electron::SerializeV8ValueWithTypedArraysAsync(
        isolate, arguments, transfer_list,
        base::BindOnce(
            [](base::WeakPtr<FastIPCBase> self,
               std::string channel,
               int32_t sender_id,
               electron::mojom::TypedArrayCloneableMessagePtr typed_msg) {
              if (!self) {
                // FastIPC instance destroyed during serialization
                return;
              }

              if (!self->electron_fast_ipc_remote_) {
                // Remote disconnected during serialization
                return;
              }

              if (!typed_msg) {
                // Serialization failed - log error but don't throw since we're in callback
                LOG(ERROR) << "FastIPC serialization failed";
                return;
              }

              if (typed_msg->base_message.encoded_message.size() == 0) {
                LOG(ERROR) << "Empty buffer after serialization";
                return;
              }

              // Send via Mojo from main thread (after background serialization)
              self->electron_fast_ipc_remote_->FastIpcMessageHost(
                  channel, std::move(typed_msg),
                  sender_id,
                  base::BindOnce([](){
                    // Empty callback
                  }));
            },
            this->GetWeakPtr(), channel, sender_id));
  }

  // gin::Wrappable
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override {
    return gin::Wrappable<T>::GetObjectTemplateBuilder(isolate)
        .SetMethod("send", &T::SendMessage)
        .SetMethod("sendSync", &T::SendSync)
        .SetMethod("sendToHost", &T::SendToHost)
        .SetMethod("invoke", &T::Invoke)
        .SetMethod("postMessage", &T::PostMessage);
  }

 protected:
  explicit FastIPCBase(v8::Isolate* isolate) : weak_factory_(this) {}

  ~FastIPCBase() override = default;

  raw_ptr<content::RenderFrame> render_frame_ = nullptr;
  mojo::AssociatedRemote<electron::mojom::ElectronApiFastIPC> electron_fast_ipc_remote_;
  base::WeakPtrFactory<FastIPCBase> weak_factory_;
};

class FastIPCRenderFrame : public FastIPCBase<FastIPCRenderFrame>,
                          public content::RenderFrameObserver {
 public:
  explicit FastIPCRenderFrame(v8::Isolate* isolate)
      : FastIPCBase<FastIPCRenderFrame>(isolate),
        content::RenderFrameObserver(GetCurrentRenderFrame()) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    blink::ExecutionContext* execution_context =
        blink::ExecutionContext::From(context);
    if (execution_context->IsWindow()) {
      RenderFrame* render_frame = GetCurrentRenderFrame();
      DCHECK(render_frame);
      render_frame->GetRemoteAssociatedInterfaces()->GetInterface(
          &electron_fast_ipc_remote_);
    }
    weak_context_ =
        v8::Global<v8::Context>(isolate, isolate->GetCurrentContext());
    weak_context_.SetWeak();
  }

  // RenderFrameObserver
  void WillReleaseScriptContext(v8::Local<v8::Context> context,
                                int32_t world_id) override {
    if (weak_context_.IsEmpty() ||
        weak_context_ == context->GetIsolate()->GetCurrentContext()) {
      electron_fast_ipc_remote_.reset();
    }
  }

  void OnDestruct() override {
    // Handled in WillReleaseScriptContext
  }

  const char* GetTypeName() override { return "FastIPCRenderFrame"; }

 private:
  v8::Global<v8::Context> weak_context_;
};

class FastIPCServiceWorker : public FastIPCBase<FastIPCServiceWorker>,
                            public content::WorkerThread::Observer {
 public:
  explicit FastIPCServiceWorker(v8::Isolate* isolate)
      : FastIPCBase<FastIPCServiceWorker>(isolate) {
    content::WorkerThread::AddObserver(this);

    auto* service_worker_data =
        electron::preload_realm::GetServiceWorkerData(
            isolate->GetCurrentContext());
    DCHECK(service_worker_data);
    service_worker_data->proxy()->GetRemoteAssociatedInterface(
        electron_fast_ipc_remote_.BindNewEndpointAndPassReceiver());
  }

  void WillStopCurrentWorkerThread() override {
    electron_fast_ipc_remote_.reset();
  }

  const char* GetTypeName() override { return "FastIPCServiceWorker"; }
};

template <>
gin::WrapperInfo FastIPCBase<FastIPCRenderFrame>::kWrapperInfo = {
    gin::kEmbedderNativeGin};

template <>
gin::WrapperInfo FastIPCBase<FastIPCServiceWorker>::kWrapperInfo = {
    gin::kEmbedderNativeGin};

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin_helper::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("createForRenderFrame", &FastIPCRenderFrame::Create);
  dict.SetMethod("createForServiceWorker", &FastIPCServiceWorker::Create);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_renderer_fast_ipc, Initialize)
