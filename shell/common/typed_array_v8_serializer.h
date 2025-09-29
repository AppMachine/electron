// Copyright (c) 2024 Electron contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_TYPED_ARRAY_V8_SERIALIZER_H_
#define ELECTRON_SHELL_COMMON_TYPED_ARRAY_V8_SERIALIZER_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "electron/shell/common/api/api_typed_array_cloneable_message.mojom.h"
#include "v8/include/v8.h"

namespace blink {
struct CloneableMessage;
}  // namespace blink

namespace electron {

class TypedArrayV8Serializer : public v8::ValueSerializer::Delegate {
 public:
  enum TypedArrayType {
    kArrayBuffer = 0,
    kInt8Array = 1,
    kUint8Array = 2,
    kUint8ClampedArray = 3,
    kInt16Array = 4,
    kUint16Array = 5,
    kInt32Array = 6,
    kUint32Array = 7,
    kFloat32Array = 8,
    kFloat64Array = 9,
    kBigInt64Array = 10,
    kBigUint64Array = 11,
    kDataView = 12,
  };

  explicit TypedArrayV8Serializer(v8::Isolate* isolate);
  ~TypedArrayV8Serializer() override;

  bool Serialize(v8::Local<v8::Value> value,
                 electron::mojom::TypedArrayCloneableMessage* out);

  void SerializeAsync(
      v8::Local<v8::Value> value,
      v8::Local<v8::Value> transfer_list,
      base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>
          callback);

  // v8::ValueSerializer::Delegate
  void* ReallocateBufferMemory(void* old_buffer,
                               size_t size,
                               size_t* actual_size) override;
  void FreeBufferMemory(void* buffer) override;
  v8::Maybe<bool> WriteHostObject(v8::Isolate* isolate,
                                  v8::Local<v8::Object> object) override;
  void ThrowDataCloneError(v8::Local<v8::String> message) override;

 protected:
  raw_ptr<v8::Isolate> isolate_;

 private:
  struct TypedArrayInfo {
    TypedArrayInfo();
    TypedArrayInfo(const TypedArrayInfo&);
    TypedArrayInfo(TypedArrayInfo&&);
    ~TypedArrayInfo();
    TypedArrayInfo& operator=(const TypedArrayInfo&);
    TypedArrayInfo& operator=(TypedArrayInfo&&);

    TypedArrayType type;
    size_t byte_offset;
    size_t byte_length;
    size_t data_offset;
    v8::Local<v8::ArrayBuffer> buffer;
    std::shared_ptr<v8::BackingStore> backing_store;
  };

  void WriteTag(uint8_t tag);
  void WriteBlinkEnvelope(uint32_t blink_version);
  TypedArrayType GetTypedArrayType(v8::Local<v8::Object> object);
  void ProcessTypedArraysSync(electron::mojom::TypedArrayCloneableMessage* out);
  void ProcessTypedArraysAsync(
      base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>
          callback);
  size_t CalculateTotalTypedArraysSize() const;

  std::vector<uint8_t> data_;
  std::unique_ptr<v8::ValueSerializer> serializer_;
  std::vector<TypedArrayInfo> typed_arrays_;

  static constexpr size_t kSharedMemoryThreshold = 4 * 1024;
  static constexpr uint8_t kTypedArrayTag = 't';
  static constexpr uint8_t kVersionTag = 0xFF;
};

class TypedArrayV8Deserializer : public v8::ValueDeserializer::Delegate {
 public:
  TypedArrayV8Deserializer(v8::Isolate* isolate,
                           const electron::mojom::TypedArrayCloneableMessage& message);
  ~TypedArrayV8Deserializer() override;

  v8::Local<v8::Value> Deserialize();

  void DeserializeAsync(
      base::OnceCallback<void(v8::Local<v8::Value>)> callback);

  // v8::ValueDeserializer::Delegate
  v8::MaybeLocal<v8::Object> ReadHostObject(v8::Isolate* isolate) override;

 protected:
  raw_ptr<v8::Isolate> isolate_;

 private:
  bool ReadTag(uint8_t* tag);
  bool ReadBlinkEnvelope(uint32_t* blink_version);
  v8::Local<v8::Object> CreateTypedArray(
      v8::Isolate* isolate,
      TypedArrayV8Serializer::TypedArrayType type,
      size_t byte_offset,
      size_t byte_length,
      const uint8_t* data);
  std::unique_ptr<v8::ValueDeserializer> deserializer_;
  raw_ref<const electron::mojom::TypedArrayCloneableMessage> message_;
  size_t current_typed_array_offset_;
  const uint8_t* typed_arrays_data_;
  mojo::ScopedSharedBufferMapping typed_arrays_mapping_;

  static constexpr uint8_t kTypedArrayTag = 't';
  static constexpr uint8_t kVersionTag = 0xFF;
  static constexpr uint8_t kTrailerOffsetTag = 0xFE;

  using TypedArrayType = TypedArrayV8Serializer::TypedArrayType;
};

// Public API functions
bool SerializeV8ValueWithTypedArrays(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    electron::mojom::TypedArrayCloneableMessage* out);

v8::Local<v8::Value> DeserializeV8ValueWithTypedArrays(
    v8::Isolate* isolate,
    const electron::mojom::TypedArrayCloneableMessage& in);

void SerializeV8ValueWithTypedArraysAsync(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>
        callback);

void DeserializeV8ValueWithTypedArraysAsync(
    v8::Isolate* isolate,
    const electron::mojom::TypedArrayCloneableMessage& in,
    base::OnceCallback<void(v8::Local<v8::Value>)> callback);

}  // namespace electron

#endif  // ELECTRON_SHELL_COMMON_TYPED_ARRAY_V8_SERIALIZER_H_
