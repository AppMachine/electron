// Copyright (c) 2024 Electron contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_TRANSFERABLE_TYPED_ARRAY_V8_SERIALIZER_H_
#define ELECTRON_SHELL_COMMON_TRANSFERABLE_TYPED_ARRAY_V8_SERIALIZER_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "electron/shell/common/api/api_transferable_typed_array_message.mojom.h"
#include "electron/shell/common/typed_array_v8_serializer.h"
#include "third_party/blink/public/common/messaging/message_port_channel.h"
#include "v8/include/v8.h"

namespace electron {

// Extends TypedArrayV8Serializer to support MessagePort transfers
class TransferableTypedArrayV8Serializer : public TypedArrayV8Serializer {
 public:
  explicit TransferableTypedArrayV8Serializer(v8::Isolate* isolate);
  ~TransferableTypedArrayV8Serializer() override;

  // Serialize with MessagePort support
  bool Serialize(v8::Local<v8::Value> value,
                 v8::Local<v8::Value> transfer_list,
                 electron::mojom::TransferableTypedArrayMessage* out);

  void SerializeAsync(
      v8::Local<v8::Value> value,
      v8::Local<v8::Value> transfer_list,
      base::OnceCallback<void(electron::mojom::TransferableTypedArrayMessagePtr)>
          callback);

 private:
  // Process MessagePorts from transfer list
  bool ProcessTransferList(v8::Local<v8::Value> transfer_list,
                           electron::mojom::TransferableTypedArrayMessage* out);

  // Helper to extract MessagePorts
  bool ExtractMessagePorts(v8::Local<v8::Array> transfer_array,
                           std::vector<blink::MessagePortChannel>* ports);
};

class TransferableTypedArrayV8Deserializer : public TypedArrayV8Deserializer {
 public:
  TransferableTypedArrayV8Deserializer(
      v8::Isolate* isolate,
      const electron::mojom::TransferableTypedArrayMessage& message);
  ~TransferableTypedArrayV8Deserializer() override;

  v8::Local<v8::Value> Deserialize();

  void DeserializeAsync(
      base::OnceCallback<void(v8::Local<v8::Value>)> callback);

 private:
  // Reconstruct MessagePorts after deserialization
  void AttachMessagePorts(v8::Local<v8::Value> value);

  raw_ref<const electron::mojom::TransferableTypedArrayMessage> transferable_message_;
};

// Public API for TransferableMessage replacement
bool SerializeV8ValueWithTransfer(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    electron::mojom::TransferableTypedArrayMessage* out);

v8::Local<v8::Value> DeserializeV8ValueWithTransfer(
    v8::Isolate* isolate,
    const electron::mojom::TransferableTypedArrayMessage& in);

void SerializeV8ValueWithTransferAsync(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    base::OnceCallback<void(electron::mojom::TransferableTypedArrayMessagePtr)>
        callback);

void DeserializeV8ValueWithTransferAsync(
    v8::Isolate* isolate,
    const electron::mojom::TransferableTypedArrayMessage& in,
    base::OnceCallback<void(v8::Local<v8::Value>)> callback);

}  // namespace electron

#endif  // ELECTRON_SHELL_COMMON_TRANSFERABLE_TYPED_ARRAY_V8_SERIALIZER_H_