// Copyright (c) 2024 Electron contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/typed_array_v8_serializer.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include "base/task/thread_pool.h"
#include "gin/converter.h"
#include "mojo/public/cpp/base/big_buffer.h"
#include "mojo/public/cpp/system/buffer.h"
#include "shell/common/api/electron_api_native_image.h"
#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/v8_util.h"
#include "skia/public/mojom/bitmap.mojom.h"
#include "third_party/blink/public/common/messaging/cloneable_message.h"
#include "third_party/blink/public/common/messaging/web_message_port.h"
#include "ui/gfx/image/image_skia.h"

namespace electron {

namespace {

constexpr uint8_t kNativeImageTag = 'i';

}  // namespace

// TypedArrayInfo implementation
TypedArrayV8Serializer::TypedArrayInfo::TypedArrayInfo() = default;
TypedArrayV8Serializer::TypedArrayInfo::TypedArrayInfo(const TypedArrayInfo&) = default;
TypedArrayV8Serializer::TypedArrayInfo::TypedArrayInfo(TypedArrayInfo&&) = default;
TypedArrayV8Serializer::TypedArrayInfo::~TypedArrayInfo() = default;
TypedArrayV8Serializer::TypedArrayInfo& TypedArrayV8Serializer::TypedArrayInfo::operator=(const TypedArrayInfo&) = default;
TypedArrayV8Serializer::TypedArrayInfo& TypedArrayV8Serializer::TypedArrayInfo::operator=(TypedArrayInfo&&) = default;

TypedArrayV8Serializer::TypedArrayV8Serializer(v8::Isolate* isolate)
    : isolate_(isolate), serializer_(std::make_unique<v8::ValueSerializer>(isolate, this)) {}

TypedArrayV8Serializer::~TypedArrayV8Serializer() = default;

bool TypedArrayV8Serializer::Serialize(
    v8::Local<v8::Value> value,
    electron::mojom::TypedArrayCloneableMessage* out) {
  std::cout << "[TypedArrayV8Serializer] Starting serialization" << std::endl;

  gin_helper::MicrotasksScope microtasks_scope{
      isolate_->GetCurrentContext(), true,
      v8::MicrotasksScope::kDoNotRunMicrotasks};

  serializer_->SetTreatArrayBufferViewsAsHostObjects(true);

  WriteBlinkEnvelope(19);
  serializer_->WriteHeader();

  bool wrote_value;
  if (!serializer_->WriteValue(isolate_->GetCurrentContext(), value)
           .To(&wrote_value)) {
    isolate_->ThrowException(v8::Exception::Error(
        gin::StringToV8(isolate_, "An object could not be cloned.")));
    return false;
  }
  DCHECK(wrote_value);

  const auto [data_bytes, data_len] = serializer_->Release();
  DCHECK_EQ(std::data(data_), data_bytes);
  DCHECK_GE(std::size(data_), data_len);
  data_.resize(data_len);

  // Initialize all required CloneableMessage fields
  out->base_message.owned_encoded_message = std::move(data_);
  out->base_message.encoded_message = out->base_message.owned_encoded_message;
  // Initialize empty vectors for non-copyable mojo types
  out->base_message.blobs.clear();  // Ensure empty vector of blobs
  out->base_message.sender_origin = std::nullopt;  // No origin restriction
  out->base_message.stack_trace_id = 0;
  out->base_message.sender_agent_cluster_id =
      blink::WebMessagePort::GetEmbedderAgentClusterID();
  out->base_message.locked_to_sender_agent_cluster = false;
  // file_system_access_tokens is optional, leave as is

  std::cout << "[TypedArrayV8Serializer] Initialized CloneableMessage, size: "
            << out->base_message.encoded_message.size() << std::endl;

  ProcessTypedArraysSync(out);

  std::cout << "[TypedArrayV8Serializer] Serialization complete" << std::endl;
  return true;
}

void TypedArrayV8Serializer::SerializeAsync(
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>
        callback) {
  gin_helper::MicrotasksScope microtasks_scope{
      isolate_->GetCurrentContext(), true,
      v8::MicrotasksScope::kDoNotRunMicrotasks};

  auto message = electron::mojom::TypedArrayCloneableMessage::New();

  serializer_->SetTreatArrayBufferViewsAsHostObjects(true);

  // Handle transfer list
  if (!transfer_list.IsEmpty() && transfer_list->IsArray()) {
    v8::Local<v8::Array> transfer_array = transfer_list.As<v8::Array>();
    uint32_t length = transfer_array->Length();
    for (uint32_t i = 0; i < length; i++) {
      v8::Local<v8::Value> element;
      if (transfer_array->Get(isolate_->GetCurrentContext(), i).ToLocal(&element)) {
        if (element->IsArrayBuffer()) {
          serializer_->TransferArrayBuffer(i, element.As<v8::ArrayBuffer>());
        }
      }
    }
  }

  WriteBlinkEnvelope(19);
  serializer_->WriteHeader();

  bool wrote_value;
  if (!serializer_->WriteValue(isolate_->GetCurrentContext(), value)
           .To(&wrote_value)) {
    isolate_->ThrowException(v8::Exception::Error(
        gin::StringToV8(isolate_, "An object could not be cloned.")));
    std::move(callback).Run(std::move(message));
    return;
  }
  DCHECK(wrote_value);

  const auto [data_bytes, data_len] = serializer_->Release();
  DCHECK_EQ(std::data(data_), data_bytes);
  DCHECK_GE(std::size(data_), data_len);
  data_.resize(data_len);

  message->base_message.owned_encoded_message = std::move(data_);
  message->base_message.encoded_message = message->base_message.owned_encoded_message;
  message->base_message.sender_agent_cluster_id =
      blink::WebMessagePort::GetEmbedderAgentClusterID();

  ProcessTypedArraysAsync(
      base::BindOnce(
          [](electron::mojom::TypedArrayCloneableMessagePtr message,
             base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)> callback,
             electron::mojom::TypedArrayCloneableMessagePtr processed_message) {
            message->typed_arrays_data = std::move(processed_message->typed_arrays_data);
            message->typed_arrays_shared_memory = std::move(processed_message->typed_arrays_shared_memory);
            message->typed_arrays_size = processed_message->typed_arrays_size;
            std::move(callback).Run(std::move(message));
          },
          std::move(message), std::move(callback)));
}

void* TypedArrayV8Serializer::ReallocateBufferMemory(void* old_buffer,
                                                     size_t size,
                                                     size_t* actual_size) {
  DCHECK_EQ(old_buffer, data_.data());
  data_.resize(size);
  *actual_size = data_.capacity();
  return data_.data();
}

void TypedArrayV8Serializer::FreeBufferMemory(void* buffer) {
  DCHECK_EQ(buffer, data_.data());
  data_ = {};
}

v8::Maybe<bool> TypedArrayV8Serializer::WriteHostObject(
    v8::Isolate* isolate,
    v8::Local<v8::Object> object) {
  // NativeImage support
  api::NativeImage* native_image;
  if (gin::ConvertFromV8(isolate, object, &native_image)) {
    WriteTag(kNativeImageTag);
    gfx::ImageSkia image = native_image->image().AsImageSkia();
    std::vector<gfx::ImageSkiaRep> image_reps = image.image_reps();
    serializer_->WriteUint32(image_reps.size());
    for (const auto& rep : image_reps) {
      serializer_->WriteDouble(rep.scale());
      const SkBitmap& bitmap = rep.GetBitmap();
      std::vector<uint8_t> bytes =
          ::skia::mojom::InlineBitmap::Serialize(&bitmap);
      serializer_->WriteUint32(bytes.size());
      serializer_->WriteRawBytes(bytes.data(), bytes.size());
    }
    return v8::Just(true);
  }

  // ArrayBuffer and TypedArray support
  if (object->IsArrayBuffer() || object->IsArrayBufferView()) {
    TypedArrayInfo info;
    info.type = GetTypedArrayType(object);

    if (object->IsArrayBuffer()) {
      v8::Local<v8::ArrayBuffer> buffer = object.As<v8::ArrayBuffer>();
      info.backing_store = buffer->GetBackingStore();
      info.byte_offset = 0;
      info.byte_length = buffer->ByteLength();
      info.buffer = buffer;
    } else if (object->IsTypedArray()) {
      v8::Local<v8::TypedArray> typed_array = object.As<v8::TypedArray>();
      info.buffer = typed_array->Buffer();
      info.backing_store = info.buffer->GetBackingStore();
      info.byte_offset = typed_array->ByteOffset();
      info.byte_length = typed_array->ByteLength();
    } else if (object->IsDataView()) {
      v8::Local<v8::DataView> data_view = object.As<v8::DataView>();
      info.buffer = data_view->Buffer();
      info.backing_store = info.buffer->GetBackingStore();
      info.byte_offset = data_view->ByteOffset();
      info.byte_length = data_view->ByteLength();
    }

    WriteTag(kTypedArrayTag);
    serializer_->WriteUint32(static_cast<uint32_t>(info.type));
    serializer_->WriteUint32(static_cast<uint32_t>(info.byte_length));

    info.data_offset = 0;
    typed_arrays_.push_back(std::move(info));

    return v8::Just(true);
  }

  return v8::ValueSerializer::Delegate::WriteHostObject(isolate, object);
}

void TypedArrayV8Serializer::ThrowDataCloneError(v8::Local<v8::String> message) {
  isolate_->ThrowException(v8::Exception::Error(message));
}

void TypedArrayV8Serializer::WriteTag(uint8_t tag) {
  serializer_->WriteRawBytes(&tag, 1U);
}

void TypedArrayV8Serializer::WriteBlinkEnvelope(uint32_t blink_version) {
  WriteTag(kVersionTag);
  serializer_->WriteUint32(blink_version);
}

TypedArrayV8Serializer::TypedArrayType TypedArrayV8Serializer::GetTypedArrayType(
    v8::Local<v8::Object> object) {
  if (object->IsArrayBuffer()) {
    return kArrayBuffer;
  } else if (object->IsInt8Array()) {
    return kInt8Array;
  } else if (object->IsUint8Array()) {
    return kUint8Array;
  } else if (object->IsUint8ClampedArray()) {
    return kUint8ClampedArray;
  } else if (object->IsInt16Array()) {
    return kInt16Array;
  } else if (object->IsUint16Array()) {
    return kUint16Array;
  } else if (object->IsInt32Array()) {
    return kInt32Array;
  } else if (object->IsUint32Array()) {
    return kUint32Array;
  } else if (object->IsFloat32Array()) {
    return kFloat32Array;
  } else if (object->IsFloat64Array()) {
    return kFloat64Array;
  } else if (object->IsBigInt64Array()) {
    return kBigInt64Array;
  } else if (object->IsBigUint64Array()) {
    return kBigUint64Array;
  } else if (object->IsDataView()) {
    return kDataView;
  }

  return kArrayBuffer;
}

size_t TypedArrayV8Serializer::CalculateTotalTypedArraysSize() const {
  size_t total = 0;
  for (const auto& info : typed_arrays_) {
    total += info.byte_length;
  }
  return total;
}

void TypedArrayV8Serializer::ProcessTypedArraysSync(
    electron::mojom::TypedArrayCloneableMessage* out) {
  size_t total_size = CalculateTotalTypedArraysSize();
  out->typed_arrays_size = total_size;

  if (total_size == 0) {
    return;
  }

  if (total_size <= kSharedMemoryThreshold) {
    // Use inline storage for small data
    std::vector<uint8_t> combined_data(total_size);
    size_t current_offset = 0;

    for (const auto& info : typed_arrays_) {
      if (info.byte_length > 0) {
        const uint8_t* src = static_cast<const uint8_t*>(info.backing_store->Data())
                            + info.byte_offset;
        memcpy(combined_data.data() + current_offset, src, info.byte_length);
        current_offset += info.byte_length;
      }
    }

    out->typed_arrays_data = mojo_base::BigBuffer(
        base::span<const uint8_t>(combined_data.data(), combined_data.size()));
  } else {
    // Use shared memory for large data
    auto shared_buffer = mojo::SharedBufferHandle::Create(total_size);
    if (shared_buffer.is_valid()) {
      auto mapping = shared_buffer->Map(total_size);
      if (mapping) {
        uint8_t* dest = static_cast<uint8_t*>(mapping.get());
        size_t current_offset = 0;

        for (const auto& info : typed_arrays_) {
          if (info.byte_length > 0) {
            const uint8_t* src = static_cast<const uint8_t*>(info.backing_store->Data())
                                + info.byte_offset;
            memcpy(dest + current_offset, src, info.byte_length);
            current_offset += info.byte_length;
          }
        }

        out->typed_arrays_shared_memory = std::move(shared_buffer);
      } else {
        // Fallback to inline if mapping fails
        std::vector<uint8_t> combined_data(total_size);
        size_t current_offset = 0;

        for (const auto& info : typed_arrays_) {
          if (info.byte_length > 0) {
            const uint8_t* src = static_cast<const uint8_t*>(info.backing_store->Data())
                                + info.byte_offset;
            memcpy(combined_data.data() + current_offset, src, info.byte_length);
            current_offset += info.byte_length;
          }
        }

        out->typed_arrays_data = mojo_base::BigBuffer(
            base::span<const uint8_t>(combined_data.data(), combined_data.size()));
      }
    }
  }
}

void TypedArrayV8Serializer::ProcessTypedArraysAsync(
    base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>
        callback) {
  auto message = electron::mojom::TypedArrayCloneableMessage::New();
  message->base_message.owned_encoded_message = std::move(data_);
  message->base_message.encoded_message = message->base_message.owned_encoded_message;
  message->base_message.sender_agent_cluster_id =
      blink::WebMessagePort::GetEmbedderAgentClusterID();

  size_t total_size = CalculateTotalTypedArraysSize();
  message->typed_arrays_size = total_size;

  if (total_size == 0) {
    std::move(callback).Run(std::move(message));
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::vector<TypedArrayInfo> typed_arrays, size_t total_size) {
            auto result = electron::mojom::TypedArrayCloneableMessage::New();
            constexpr size_t kSharedMemoryThreshold = 4 * 1024;

            if (total_size <= kSharedMemoryThreshold) {
              std::vector<uint8_t> combined_data(total_size);
              size_t current_offset = 0;

              for (const auto& info : typed_arrays) {
                if (info.byte_length > 0) {
                  const uint8_t* src = static_cast<const uint8_t*>(info.backing_store->Data())
                                      + info.byte_offset;
                  memcpy(combined_data.data() + current_offset, src, info.byte_length);
                  current_offset += info.byte_length;
                }
              }

              result->typed_arrays_data = mojo_base::BigBuffer(
                  base::span<const uint8_t>(combined_data.data(), combined_data.size()));
            } else {
              auto shared_buffer = mojo::SharedBufferHandle::Create(total_size);
              if (shared_buffer.is_valid()) {
                auto mapping = shared_buffer->Map(total_size);
                if (mapping) {
                  uint8_t* dest = static_cast<uint8_t*>(mapping.get());
                  size_t current_offset = 0;

                  for (const auto& info : typed_arrays) {
                    if (info.byte_length > 0) {
                      const uint8_t* src = static_cast<const uint8_t*>(info.backing_store->Data())
                                          + info.byte_offset;
                      memcpy(dest + current_offset, src, info.byte_length);
                      current_offset += info.byte_length;
                    }
                  }

                  result->typed_arrays_shared_memory = std::move(shared_buffer);
                }
              }
            }

            result->typed_arrays_size = total_size;
            return result;
          },
          std::move(typed_arrays_), total_size),
      base::BindOnce(
          [](electron::mojom::TypedArrayCloneableMessagePtr message,
             base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)> callback,
             electron::mojom::TypedArrayCloneableMessagePtr typed_arrays_result) {
            message->typed_arrays_data = std::move(typed_arrays_result->typed_arrays_data);
            message->typed_arrays_shared_memory = std::move(typed_arrays_result->typed_arrays_shared_memory);
            message->typed_arrays_size = typed_arrays_result->typed_arrays_size;
            std::move(callback).Run(std::move(message));
          },
          std::move(message), std::move(callback)));
}

// TypedArrayV8Deserializer

TypedArrayV8Deserializer::TypedArrayV8Deserializer(
    v8::Isolate* isolate,
    const electron::mojom::TypedArrayCloneableMessage& message)
    : isolate_(isolate),
      message_(message),
      current_typed_array_offset_(0),
      typed_arrays_data_(nullptr) {
  const auto& data = message_->base_message.encoded_message;
  deserializer_ = std::make_unique<v8::ValueDeserializer>(
      isolate, data.data(), data.size(), this);

  if (message_->typed_arrays_data) {
    typed_arrays_data_ = message_->typed_arrays_data->data();
  } else if (message_->typed_arrays_shared_memory.is_valid()) {
    typed_arrays_mapping_ = message_->typed_arrays_shared_memory->Map(message_->typed_arrays_size);
    if (typed_arrays_mapping_) {
      typed_arrays_data_ = static_cast<const uint8_t*>(typed_arrays_mapping_.get());
    }
  }
}

TypedArrayV8Deserializer::~TypedArrayV8Deserializer() = default;

v8::Local<v8::Value> TypedArrayV8Deserializer::Deserialize() {
  v8::EscapableHandleScope scope(isolate_);
  auto context = isolate_->GetCurrentContext();

  uint32_t blink_version;
  if (!ReadBlinkEnvelope(&blink_version))
    return v8::Null(isolate_);

  bool read_header;
  if (!deserializer_->ReadHeader(context).To(&read_header))
    return v8::Null(isolate_);
  DCHECK(read_header);

  v8::Local<v8::Value> value;
  if (!deserializer_->ReadValue(context).ToLocal(&value))
    return v8::Null(isolate_);

  return scope.Escape(value);
}

void TypedArrayV8Deserializer::DeserializeAsync(
    base::OnceCallback<void(v8::Local<v8::Value>)> callback) {
  std::move(callback).Run(Deserialize());
}

v8::MaybeLocal<v8::Object> TypedArrayV8Deserializer::ReadHostObject(
    v8::Isolate* isolate) {
  uint8_t tag = 0;
  if (!ReadTag(&tag))
    return v8::MaybeLocal<v8::Object>();

  if (tag == kNativeImageTag) {
    uint32_t num_reps = 0;
    if (!deserializer_->ReadUint32(&num_reps))
      return v8::MaybeLocal<v8::Object>();

    gfx::ImageSkia image_skia;
    for (uint32_t i = 0; i < num_reps; i++) {
      double scale = 0.0;
      if (!deserializer_->ReadDouble(&scale))
        return v8::MaybeLocal<v8::Object>();

      uint32_t bitmap_size_bytes = 0;
      if (!deserializer_->ReadUint32(&bitmap_size_bytes))
        return v8::MaybeLocal<v8::Object>();

      const void* bitmap_data = nullptr;
      if (!deserializer_->ReadRawBytes(bitmap_size_bytes, &bitmap_data))
        return v8::MaybeLocal<v8::Object>();

      SkBitmap bitmap;
      if (!::skia::mojom::InlineBitmap::Deserialize(bitmap_data,
                                                  bitmap_size_bytes, &bitmap))
        return v8::MaybeLocal<v8::Object>();

      image_skia.AddRepresentation(gfx::ImageSkiaRep(bitmap, scale));
    }

    gfx::Image image(image_skia);
    auto* native_image = new api::NativeImage(isolate, image);
    return native_image->GetWrapper(isolate);
  } else if (tag == kTypedArrayTag) {
    uint32_t type;
    uint32_t byte_length;

    if (!deserializer_->ReadUint32(&type))
      return v8::MaybeLocal<v8::Object>();
    if (!deserializer_->ReadUint32(&byte_length))
      return v8::MaybeLocal<v8::Object>();

    const uint8_t* array_data = nullptr;
    if (typed_arrays_data_ && current_typed_array_offset_ + byte_length <= message_->typed_arrays_size) {
      array_data = typed_arrays_data_ + current_typed_array_offset_;
      current_typed_array_offset_ += byte_length;
    }

    return CreateTypedArray(isolate,
                           static_cast<TypedArrayV8Serializer::TypedArrayType>(type),
                           0,
                           byte_length,
                           array_data);
  }

  return v8::MaybeLocal<v8::Object>();
}

bool TypedArrayV8Deserializer::ReadTag(uint8_t* tag) {
  const void* tag_bytes = nullptr;
  if (!deserializer_->ReadRawBytes(1, &tag_bytes))
    return false;
  *tag = *reinterpret_cast<const uint8_t*>(tag_bytes);
  return true;
}

bool TypedArrayV8Deserializer::ReadBlinkEnvelope(uint32_t* blink_version) {
  uint8_t tag = 0;
  if (!ReadTag(&tag) || tag != kVersionTag)
    return false;
  if (!deserializer_->ReadUint32(blink_version))
    return false;

  static constexpr uint32_t kMinWireFormatVersionWithTrailer = 21;
  if (*blink_version >= kMinWireFormatVersionWithTrailer) {
    uint8_t trailer_offset_tag = 0;
    if (!ReadTag(&trailer_offset_tag) ||
        trailer_offset_tag != kTrailerOffsetTag)
      return false;

    const void* trailer_offset_and_size_bytes = nullptr;
    static constexpr size_t kTrailerOffsetDataSize =
        sizeof(uint64_t) + sizeof(uint32_t);
    if (!deserializer_->ReadRawBytes(kTrailerOffsetDataSize,
                                     &trailer_offset_and_size_bytes))
      return false;
  }

  return true;
}

v8::Local<v8::Object> TypedArrayV8Deserializer::CreateTypedArray(
    v8::Isolate* isolate,
    TypedArrayV8Serializer::TypedArrayType type,
    size_t byte_offset,
    size_t byte_length,
    const uint8_t* data) {
  v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, byte_length);

  if (byte_length > 0 && data) {
    auto backing_store = buffer->GetBackingStore();
    if (backing_store && backing_store->Data()) {
      memcpy(backing_store->Data(), data, byte_length);
    } else {
      LOG(ERROR) << "Failed to get backing store for ArrayBuffer";
      return v8::Object::New(isolate);
    }
  }

  using TAType = TypedArrayV8Serializer::TypedArrayType;
  // For typed arrays, the 3rd parameter is the element count, not byte length
  // Since we created a buffer with exact size, offset is always 0
  switch (type) {
    case TAType::kArrayBuffer:
      return buffer;
    case TAType::kInt8Array:
      return v8::Int8Array::New(buffer, 0, byte_length);
    case TAType::kUint8Array:
      return v8::Uint8Array::New(buffer, 0, byte_length);
    case TAType::kUint8ClampedArray:
      return v8::Uint8ClampedArray::New(buffer, 0, byte_length);
    case TAType::kInt16Array:
      return v8::Int16Array::New(buffer, 0, byte_length / 2);
    case TAType::kUint16Array:
      return v8::Uint16Array::New(buffer, 0, byte_length / 2);
    case TAType::kInt32Array:
      return v8::Int32Array::New(buffer, 0, byte_length / 4);
    case TAType::kUint32Array:
      return v8::Uint32Array::New(buffer, 0, byte_length / 4);
    case TAType::kFloat32Array:
      return v8::Float32Array::New(buffer, 0, byte_length / 4);
    case TAType::kFloat64Array:
      return v8::Float64Array::New(buffer, 0, byte_length / 8);
    case TAType::kBigInt64Array:
      return v8::BigInt64Array::New(buffer, 0, byte_length / 8);
    case TAType::kBigUint64Array:
      return v8::BigUint64Array::New(buffer, 0, byte_length / 8);
    case TAType::kDataView:
      return v8::DataView::New(buffer, 0, byte_length);
    default:
      return v8::Object::New(isolate);
  }
}

// Public API

bool SerializeV8ValueWithTypedArrays(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    electron::mojom::TypedArrayCloneableMessage* out) {
  return TypedArrayV8Serializer(isolate).Serialize(value, out);
}

v8::Local<v8::Value> DeserializeV8ValueWithTypedArrays(
    v8::Isolate* isolate,
    const electron::mojom::TypedArrayCloneableMessage& in) {
  return TypedArrayV8Deserializer(isolate, in).Deserialize();
}

void SerializeV8ValueWithTypedArraysAsync(
    v8::Isolate* isolate,
    v8::Local<v8::Value> value,
    v8::Local<v8::Value> transfer_list,
    base::OnceCallback<void(electron::mojom::TypedArrayCloneableMessagePtr)>
        callback) {
  TypedArrayV8Serializer(isolate).SerializeAsync(value, transfer_list, std::move(callback));
}

void DeserializeV8ValueWithTypedArraysAsync(
    v8::Isolate* isolate,
    const electron::mojom::TypedArrayCloneableMessage& in,
    base::OnceCallback<void(v8::Local<v8::Value>)> callback) {
  TypedArrayV8Deserializer(isolate, in).DeserializeAsync(std::move(callback));
}

}  // namespace electron