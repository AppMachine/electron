// Copyright (c) 2024 Electron contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/transferable_typed_array_v8_serializer.h"

#include <iostream>
#include "gin/converter.h"
#include "gin/handle.h"  // Need full gin::Handle definition
#include "shell/browser/api/message_port.h"
#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/v8_util.h"
#include "third_party/blink/public/common/messaging/message_port_descriptor.h"
#include "third_party/blink/public/common/messaging/cloneable_message_mojom_traits.h"
#include "third_party/blink/public/mojom/blob/blob.mojom.h"
#include "third_party/blink/public/mojom/blob/serialized_blob.mojom.h"

namespace electron {

TransferableTypedArrayV8Serializer::TransferableTypedArrayV8Serializer(
    v8::Isolate* isolate)
    : TypedArrayV8Serializer(isolate) {}

TransferableTypedArrayV8Serializer::~TransferableTypedArrayV8Serializer() = default;

bool TransferableTypedArrayV8Serializer::Serialize(
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    electron::mojom::TransferableTypedArrayMessage* out) {
  std::cout << "[TransferableSerializer] Starting serialization" << std::endl;

  gin_helper::MicrotasksScope microtasks_scope{
      isolate_->GetCurrentContext(), true,
      v8::MicrotasksScope::kDoNotRunMicrotasks};

  // First serialize the main value using TypedArrayV8Serializer
  auto temp_message = electron::mojom::TypedArrayCloneableMessage::New();
  if (!TypedArrayV8Serializer::Serialize(value, temp_message.get())) {
    std::cout << "[TransferableSerializer] Base serialization failed" << std::endl;
    return false;
  }

  std::cout << "[TransferableSerializer] Base serialization succeeded, size: "
            << temp_message->base_message.encoded_message.size() << std::endl;

  // Copy the base serialization to output
  out->base_message = std::move(temp_message->base_message);
  out->typed_arrays_data = std::move(temp_message->typed_arrays_data);
  out->typed_arrays_shared_memory = std::move(temp_message->typed_arrays_shared_memory);
  out->typed_arrays_size = temp_message->typed_arrays_size;

  // Process transfer list (MessagePorts and ArrayBuffers)
  if (!transfer_list.IsEmpty() && !transfer_list->IsUndefined()) {
    std::cout << "[TransferableSerializer] Processing transfer list" << std::endl;
    if (!ProcessTransferList(transfer_list, out)) {
      std::cout << "[TransferableSerializer] ProcessTransferList failed" << std::endl;
      return false;
    }
    std::cout << "[TransferableSerializer] Transfer list processed, ports: "
              << out->ports.size() << std::endl;
  } else {
    std::cout << "[TransferableSerializer] No transfer list provided" << std::endl;
  }

  std::cout << "[TransferableSerializer] Serialization complete" << std::endl;
  return true;
}

void TransferableTypedArrayV8Serializer::SerializeAsync(
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    base::OnceCallback<void(electron::mojom::TransferableTypedArrayMessagePtr)>
        callback) {
  auto message = electron::mojom::TransferableTypedArrayMessage::New();

  // Initialize all required array fields to avoid mojo validation errors
  message->ports = std::vector<blink::MessagePortDescriptor>();
  message->stream_channels = std::vector<blink::MessagePortDescriptor>();

  // Use the parent class async serialization for typed arrays
  TypedArrayV8Serializer::SerializeAsync(
      value, transfer_list,
      base::BindOnce(
          [](electron::mojom::TransferableTypedArrayMessagePtr message,
             v8::Local<v8::Value> transfer_list,
             base::OnceCallback<void(electron::mojom::TransferableTypedArrayMessagePtr)> callback,
             electron::mojom::TypedArrayCloneableMessagePtr typed_array_message) {

            // Copy typed array data
            message->base_message = std::move(typed_array_message->base_message);
            message->typed_arrays_data = std::move(typed_array_message->typed_arrays_data);
            message->typed_arrays_shared_memory = std::move(typed_array_message->typed_arrays_shared_memory);
            message->typed_arrays_size = typed_array_message->typed_arrays_size;

            // Process MessagePorts from transfer list
            if (!transfer_list.IsEmpty() && !transfer_list->IsUndefined()) {
              v8::Isolate* isolate = v8::Isolate::GetCurrent();
              TransferableTypedArrayV8Serializer serializer(isolate);
              serializer.ProcessTransferList(transfer_list, message.get());
            }

            std::move(callback).Run(std::move(message));
          },
          std::move(message), transfer_list, std::move(callback)));
}

bool TransferableTypedArrayV8Serializer::ProcessTransferList(
    v8::Local<v8::Value> transfer_list,
    electron::mojom::TransferableTypedArrayMessage* out) {

  if (!transfer_list->IsArray()) {
    return true;  // No transfer list is ok
  }

  v8::Local<v8::Array> transfer_array = transfer_list.As<v8::Array>();

  std::vector<gin::Handle<MessagePort>> wrapped_ports;
  uint32_t length = transfer_array->Length();

  // Extract MessagePorts from transfer list
  for (uint32_t i = 0; i < length; i++) {
    v8::Local<v8::Value> element;
    if (!transfer_array->Get(isolate_->GetCurrentContext(), i).ToLocal(&element))
      continue;

    // Check if it's a MessagePort
    gin::Handle<MessagePort> port;
    if (gin::ConvertFromV8(isolate_, element, &port)) {
      wrapped_ports.push_back(port);
    }
    // ArrayBuffers are already handled by parent class
  }

  // Disentangle MessagePorts
  if (!wrapped_ports.empty()) {
    bool threw_exception = false;
    std::vector<blink::MessagePortChannel> channels = MessagePort::DisentanglePorts(
        isolate_, wrapped_ports, &threw_exception);
    if (threw_exception) {
      return false;
    }

    // Extract MessagePortDescriptors from channels
    for (auto& channel : channels) {
      out->ports.push_back(channel.ReleaseHandle());
    }
  }

  return true;
}

bool TransferableTypedArrayV8Serializer::ExtractMessagePorts(
    v8::Local<v8::Array> transfer_array,
    std::vector<blink::MessagePortChannel>* ports) {
  std::vector<gin::Handle<MessagePort>> wrapped_ports;

  uint32_t length = transfer_array->Length();
  for (uint32_t i = 0; i < length; i++) {
    v8::Local<v8::Value> element;
    if (!transfer_array->Get(isolate_->GetCurrentContext(), i).ToLocal(&element))
      continue;

    gin::Handle<MessagePort> port;
    if (gin::ConvertFromV8(isolate_, element, &port)) {
      wrapped_ports.push_back(port);
    }
  }

  if (!wrapped_ports.empty()) {
    bool threw_exception = false;
    *ports = MessagePort::DisentanglePorts(isolate_, wrapped_ports, &threw_exception);
    return !threw_exception;
  }

  return true;
}

// Deserializer implementation
TransferableTypedArrayV8Deserializer::TransferableTypedArrayV8Deserializer(
    v8::Isolate* isolate,
    const electron::mojom::TransferableTypedArrayMessage& message)
    : TypedArrayV8Deserializer(isolate,
        *reinterpret_cast<const electron::mojom::TypedArrayCloneableMessage*>(&message)),
      transferable_message_(message) {}

TransferableTypedArrayV8Deserializer::~TransferableTypedArrayV8Deserializer() = default;

v8::Local<v8::Value> TransferableTypedArrayV8Deserializer::Deserialize() {
  v8::Local<v8::Value> result = TypedArrayV8Deserializer::Deserialize();

  // Attach any transferred MessagePorts
  if (!transferable_message_->ports.empty()) {
    AttachMessagePorts(result);
  }

  return result;
}

void TransferableTypedArrayV8Deserializer::DeserializeAsync(
    base::OnceCallback<void(v8::Local<v8::Value>)> callback) {
  // Simply forward to the base class - MessagePorts are handled separately
  // by the caller (e.g., in electron_api_service_impl.cc)
  TypedArrayV8Deserializer::DeserializeAsync(std::move(callback));
}

void TransferableTypedArrayV8Deserializer::AttachMessagePorts(
    v8::Local<v8::Value> value) {
  // MessagePorts are handled by the caller who has non-const access to the message
  // This is typically done in electron_api_service_impl.cc or similar
}

// Public API implementation
bool SerializeV8ValueWithTransfer(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    electron::mojom::TransferableTypedArrayMessage* out) {
  return TransferableTypedArrayV8Serializer(isolate).Serialize(value, transfer_list, out);
}

v8::Local<v8::Value> DeserializeV8ValueWithTransfer(
    v8::Isolate* isolate,
    const electron::mojom::TransferableTypedArrayMessage& in) {
  return TransferableTypedArrayV8Deserializer(isolate, in).Deserialize();
}

void SerializeV8ValueWithTransferAsync(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    base::OnceCallback<void(electron::mojom::TransferableTypedArrayMessagePtr)>
        callback) {
  TransferableTypedArrayV8Serializer(isolate).SerializeAsync(
      value, transfer_list, std::move(callback));
}

void DeserializeV8ValueWithTransferAsync(
    v8::Isolate* isolate,
    const electron::mojom::TransferableTypedArrayMessage& in,
    base::OnceCallback<void(v8::Local<v8::Value>)> callback) {
  TransferableTypedArrayV8Deserializer(isolate, in).DeserializeAsync(
      std::move(callback));
}

}  // namespace electron