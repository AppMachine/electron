// Copyright (c) 2024 Electron contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/api/electron_message_port.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "base/task/single_thread_task_runner.h"
#include "gin/arguments.h"
#include "gin/data_object_builder.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/event_emitter_caller.h"
#include "shell/common/transferable_typed_array_v8_serializer.h"
#include "shell/common/api/api_transferable_typed_array_message.mojom.h"
#include "third_party/blink/public/common/messaging/cloneable_message_mojom_traits.h"
#include "third_party/blink/public/common/messaging/message_port_channel.h"
#include "third_party/blink/public/common/messaging/task_attribution_id_mojom_traits.h"
#include "third_party/blink/public/mojom/blob/blob.mojom.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace electron {

gin::WrapperInfo RendererMessagePort::kWrapperInfo = {gin::kEmbedderNativeGin};

RendererMessagePort::RendererMessagePort(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context)
    : context_(isolate, context) {}

RendererMessagePort::~RendererMessagePort() {
  if (!IsNeutered()) {
    // Properly disentangle before teardown. The MessagePortDescriptor will blow up if it
    // hasn't had its underlying handle returned to it before teardown.
    port_.GiveDisentangledHandle(connector_->PassMessagePipe());
  }
  connector_.reset();
}

// static
gin::Handle<RendererMessagePort> RendererMessagePort::Create(v8::Isolate* isolate,
                                                              v8::Local<v8::Context> context) {
  return gin::CreateHandle(isolate, new RendererMessagePort(isolate, context));
}

void RendererMessagePort::PostMessage(gin::Arguments* args) {
  if (!IsEntangled())
    return;

  gin_helper::ErrorThrower thrower(args->isolate());

  // Get message value and transfer list
  v8::Local<v8::Value> message_value;
  v8::Local<v8::Value> transfer_list;

  if (!args->GetNext(&message_value)) {
    message_value = v8::Undefined(args->isolate());
  }

  if (!args->GetNext(&transfer_list)) {
    transfer_list = v8::Undefined(args->isolate());
  }

  std::cout << "[RendererMessagePort::PostMessage] Starting async serialization" << std::endl;

  // Keep a weak pointer for async operation
  auto weak_this = weak_factory_.GetWeakPtr();

  // Use async serializer with shared memory optimization
  electron::SerializeV8ValueWithTransferAsync(
      args->isolate(), message_value, transfer_list,
      base::BindOnce(
          [](base::WeakPtr<RendererMessagePort> weak_port,
             electron::mojom::TransferableTypedArrayMessagePtr message) {
            if (!weak_port || !weak_port->IsEntangled()) {
              std::cout << "[RendererMessagePort::PostMessage] Port no longer valid" << std::endl;
              return;
            }

            std::cout << "[RendererMessagePort::PostMessage] Sending message" << std::endl;
            mojo::Message mojo_message =
                electron::mojom::TransferableTypedArrayMessage::WrapAsMessage(
                    std::move(message));
            weak_port->connector_->Accept(&mojo_message);
          },
          weak_this));
}

void RendererMessagePort::Start() {
  if (!IsEntangled())
    return;

  if (started_)
    return;

  started_ = true;
  Pin();  // Pin to prevent GC while messages might arrive
  std::cout << "[RendererMessagePort::Start] Port started and pinned, resuming message processing" << std::endl;
  connector_->ResumeIncomingMethodCallProcessing();
}

void RendererMessagePort::Close() {
  if (closed_)
    return;

  std::cout << "[RendererMessagePort::Close] Close() called - port is being closed" << std::endl;

  closed_ = true;
  Unpin();  // Allow GC now that port is closed

  // Only disentangle/reset if the port is still valid (not neutered)
  // This mirrors what blink::MessagePort does in its close() method
  if (!IsNeutered()) {
    // Disentangle the port properly before cleanup
    port_.GiveDisentangledHandle(connector_->PassMessagePipe());
    connector_.reset();

    // Create a new dangling message pipe to keep the port valid but disconnected
    // This ensures the port can be properly cleaned up later
    blink::MessagePortDescriptorPair pipe;
    port_ = pipe.TakePort0();
    // Don't re-entangle with connector, leave it disconnected
  } else {
    // Just reset the connector if it's already neutered
    connector_.reset();
  }

  // Emit close event if we have a valid isolate
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  if (isolate) {
    v8::HandleScope scope(isolate);
    v8::Local<v8::Object> self;
    if (GetWrapper(isolate).ToLocal(&self)) {
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!context.IsEmpty()) {
        v8::Context::Scope context_scope(context);

        // Try to use dispatchEvent for Web API compatibility
        v8::Local<v8::Value> dispatch_event_fn;
        if (self->Get(context, gin::StringToV8(isolate, "dispatchEvent"))
                 .ToLocal(&dispatch_event_fn) &&
            dispatch_event_fn->IsFunction()) {
          // Create Event('close')
          v8::Local<v8::Value> event_ctor;
          if (context->Global()
                   ->Get(context, gin::StringToV8(isolate, "Event"))
                   .ToLocal(&event_ctor) &&
              event_ctor->IsFunction()) {
            v8::Local<v8::Value> argv[] = { gin::StringToV8(isolate, "close") };
            v8::Local<v8::Value> close_event;
            if (event_ctor.As<v8::Function>()
                     ->NewInstance(context, 1, argv)
                     .ToLocal(&close_event)) {
              v8::Local<v8::Value> dispatch_argv[] = { close_event };
              v8::Local<v8::Value> result;
              (void)dispatch_event_fn.As<v8::Function>()->Call(context, self, 1, dispatch_argv).ToLocal(&result);
            }
          }
        } else {
          // Fall back to emit for Node context
          gin_helper::EmitEvent(isolate, self, "close");
        }
      }
    }
  }
}

void RendererMessagePort::Entangle(blink::MessagePortDescriptor port) {
  DCHECK(port.IsValid());
  DCHECK(!connector_);

  std::cout << "[RendererMessagePort::Entangle] Starting entangle" << std::endl;

  port_ = std::move(port);
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  if (!isolate) {
    std::cout << "[RendererMessagePort::Entangle] ERROR: No isolate available!" << std::endl;
    return;
  }
  v8::HandleScope scope(isolate);

  connector_ = std::make_unique<mojo::Connector>(
      port_.TakeHandleToEntangleWithEmbedder(),
      mojo::Connector::SINGLE_THREADED_SEND,
      base::SingleThreadTaskRunner::GetCurrentDefault());
  connector_->PauseIncomingMethodCallProcessing();
  connector_->set_incoming_receiver(this);
  connector_->set_connection_error_handler(
      base::BindOnce(&RendererMessagePort::Close, weak_factory_.GetWeakPtr()));

  // Pin if we have pending activity (like main process MessagePort does)
  if (HasPendingActivity()) {
    std::cout << "[RendererMessagePort::Entangle] Port has pending activity, pinning" << std::endl;
    Pin();
  } else {
    std::cout << "[RendererMessagePort::Entangle] Port has no pending activity (started=" << started_ << ")" << std::endl;
  }

  std::cout << "[RendererMessagePort::Entangle] Entangle complete" << std::endl;
}

bool RendererMessagePort::Accept(mojo::Message* mojo_message) {
  std::cout << "[RendererMessagePort::Accept] Received message, started=" << started_
            << ", closed=" << closed_ << std::endl;

  // First deserialize the mojo message (doesn't need V8)
  electron::mojom::TransferableTypedArrayMessagePtr message;
  if (!electron::mojom::TransferableTypedArrayMessage::DeserializeFromMessage(
          std::move(*mojo_message), &message)) {
    std::cout << "[RendererMessagePort::Accept] Failed to deserialize" << std::endl;
    return false;
  }

  // Store message and post task to process when context is available
  auto message_ptr = std::make_shared<electron::mojom::TransferableTypedArrayMessagePtr>(
      std::move(message));

  auto weak_this = weak_factory_.GetWeakPtr();

  // Post task to ensure we're on the right thread and can get context
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&RendererMessagePort::ProcessMessage,
                     weak_this, message_ptr));

  return true;
}

void RendererMessagePort::ProcessMessage(
    std::shared_ptr<electron::mojom::TransferableTypedArrayMessagePtr> message) {
  if (closed_)
    return;

  // Get the isolate
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  if (!isolate || isolate->IsExecutionTerminating()) {
    std::cout << "[RendererMessagePort::ProcessMessage] No isolate or execution terminating" << std::endl;
    return;  // Drop message if no isolate
  }

  v8::HandleScope scope(isolate);

  // Use our stored context (like Blink uses stored ExecutionContext)
  v8::Local<v8::Context> context = context_.Get(isolate);
  if (context.IsEmpty()) {
    std::cout << "[RendererMessagePort::ProcessMessage] Stored context was garbage collected" << std::endl;
    return;  // Context was garbage collected
  }

  std::cout << "[RendererMessagePort::ProcessMessage] Have context, processing message" << std::endl;
  v8::Context::Scope context_scope(context);

  // Convert ports (pass our stored context to child ports)
  std::vector<gin::Handle<RendererMessagePort>> ports;
  for (auto& port_desc : (*message)->ports) {
    auto port = RendererMessagePort::Create(isolate, context);
    port->Entangle(std::move(port_desc));
    ports.push_back(port);
  }

  // Keep a weak reference for async deserialization
  auto weak_this = weak_factory_.GetWeakPtr();

  // Use async deserializer with our stored context
  std::cout << "[RendererMessagePort::ProcessMessage] Starting async deserialization" << std::endl;

  // Now do the async deserialization with valid context
  DeserializeV8ValueWithTransferAsync(
      isolate, **message,
      base::BindOnce(
          [](base::WeakPtr<RendererMessagePort> weak_port,
             std::vector<gin::Handle<RendererMessagePort>> ports,
             v8::Local<v8::Value> message_value) {
            if (!weak_port)
              return;

            v8::Isolate* isolate = v8::Isolate::GetCurrent();
            if (!isolate)
              return;

            v8::HandleScope scope(isolate);

            // Use the stored context from the port
            v8::Local<v8::Context> local_context = weak_port->context_.Get(isolate);
            if (local_context.IsEmpty())
              return;

            v8::Context::Scope context_scope(local_context);

            std::cout << "[RendererMessagePort::ProcessMessage] Async deserialization complete" << std::endl;

            v8::Local<v8::Object> self;
            if (!weak_port->GetWrapper(isolate).ToLocal(&self)) {
              std::cout << "[RendererMessagePort::ProcessMessage] ERROR: Failed to get wrapper" << std::endl;
              return;
            }
            std::cout << "[RendererMessagePort::ProcessMessage] Got wrapper successfully" << std::endl;

            auto event = gin::DataObjectBuilder(isolate)
                             .Set("data", message_value)
                             .Set("ports", ports)
                             .Build();

            std::cout << "[RendererMessagePort::ProcessMessage] About to emit message event, started="
                      << weak_port->started_ << ", closed=" << weak_port->closed_ << std::endl;

            // Add microtask scope for V8 function calls from async callback
            v8::MicrotasksScope microtasks_scope(isolate,
                                                  local_context->GetMicrotaskQueue(),
                                                  v8::MicrotasksScope::kRunMicrotasks);

            // Check if there's an onmessage handler set
            v8::Local<v8::Value> onmessage_handler;
            if (self->Get(local_context, gin::StringToV8(isolate, "onmessage"))
                     .ToLocal(&onmessage_handler) &&
                onmessage_handler->IsFunction()) {
              std::cout << "[RendererMessagePort::ProcessMessage] Found onmessage handler, calling it" << std::endl;

              // Create a MessageEvent-like object for the handler
              v8::Local<v8::Object> message_event = gin::DataObjectBuilder(isolate)
                  .Set("data", message_value)
                  .Set("ports", ports)
                  .Build();

              // Call the onmessage handler
              v8::Local<v8::Value> argv[] = { message_event };
              v8::Local<v8::Value> result;
              if (!onmessage_handler.As<v8::Function>()
                       ->Call(local_context, self, 1, argv)
                       .ToLocal(&result)) {
                std::cout << "[RendererMessagePort::ProcessMessage] ERROR: onmessage handler call failed" << std::endl;
              } else {
                std::cout << "[RendererMessagePort::ProcessMessage] onmessage handler called successfully" << std::endl;
              }
            } else {
              std::cout << "[RendererMessagePort::ProcessMessage] No onmessage handler found" << std::endl;

              // Try emit as fallback for Node.js style event handling
              gin_helper::EmitEvent(isolate, self, "message", event);
            }
          },
          weak_this, std::move(ports)));
}

bool RendererMessagePort::IsEntangled() const {
  return !closed_ && !IsNeutered();
}

bool RendererMessagePort::IsNeutered() const {
  return !connector_ || !connector_->is_valid();
}

bool RendererMessagePort::HasPendingActivity() const {
  // The port has pending activity if it's started and entangled
  // (messages might arrive)
  return started_ && IsEntangled();
}

void RendererMessagePort::Pin() {
  if (!pinned_.IsEmpty())
    return;

  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  if (!isolate)
    return;

  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> self;
  if (GetWrapper(isolate).ToLocal(&self)) {
    pinned_.Reset(isolate, self);
    std::cout << "[RendererMessagePort::Pin] Pinned JS wrapper to prevent GC" << std::endl;
  }
}

void RendererMessagePort::Unpin() {
  if (!pinned_.IsEmpty()) {
    std::cout << "[RendererMessagePort::Unpin] Unpinned JS wrapper, allowing GC" << std::endl;
    pinned_.Reset();
  }
}

gin::ObjectTemplateBuilder RendererMessagePort::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<RendererMessagePort>::GetObjectTemplateBuilder(isolate)
      .SetMethod("postMessage", &RendererMessagePort::PostMessage)
      .SetMethod("start", &RendererMessagePort::Start)
      .SetMethod("close", &RendererMessagePort::Close);
}

const char* RendererMessagePort::GetTypeName() {
  return "RendererMessagePort";
}

void RendererMessagePort::WillBeDestroyed() {
  ClearWeak();
}

}  // namespace electron