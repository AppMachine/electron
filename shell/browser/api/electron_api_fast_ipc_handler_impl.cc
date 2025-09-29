// Copyright (c) 2024 The Electron Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_fast_ipc_handler_impl.h"

#include "base/logging.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/time/time.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "gin/handle.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/api/api_typed_array_cloneable_message.mojom.h"
#include "shell/common/typed_array_v8_serializer.h"
#include "shell/common/gin_converters/content_converter.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_helper/event.h"
#include "shell/common/gin_helper/fast_ipc_reply_channel.h"
#include "shell/common/node_includes.h"
#include "v8/include/v8.h"

namespace electron {

// Static member initialization
uint32_t ElectronApiFastIpcHandler::total_transfers_ = 0;
double ElectronApiFastIpcHandler::total_latency_ms_ = 0.0;
uint64_t ElectronApiFastIpcHandler::total_bytes_transferred_ = 0;

ElectronApiFastIpcHandler::ElectronApiFastIpcHandler(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingAssociatedReceiver<mojom::ElectronApiFastIPC> receiver)
    : content::WebContentsObserver(
          content::WebContents::FromRenderFrameHost(render_frame_host)),
      render_frame_host_id_(render_frame_host->GetGlobalId()),
      receiver_(this, std::move(receiver)) {
  receiver_.set_disconnect_handler(base::BindOnce(
      &ElectronApiFastIpcHandler::OnConnectionError,
      weak_factory_.GetWeakPtr()));

}

ElectronApiFastIpcHandler::~ElectronApiFastIpcHandler() {
}

// static
void ElectronApiFastIpcHandler::Create(
    content::RenderFrameHost* frame_host,
    mojo::PendingAssociatedReceiver<mojom::ElectronApiFastIPC> receiver) {
  // Self-managed instance - will delete itself when the WebContents is destroyed
  new ElectronApiFastIpcHandler(frame_host, std::move(receiver));
}

void ElectronApiFastIpcHandler::SendFastIpcMessage(
    bool internal,
    const std::string& channel,
    electron::mojom::TypedArrayCloneableMessagePtr message,
    int32_t sender_id,
    SendFastIpcMessageCallback callback) {
  auto* session = GetSession();
  if (!session) {
    std::move(callback).Run();
    return;
  }
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto event = MakeFastIpcEvent(isolate, session, internal);
  if (event.IsEmpty()) {
    std::move(callback).Run();
    return;
  }
  v8::Local<v8::Value> args =
      electron::DeserializeV8ValueWithTypedArrays(
          isolate, *message);
  session->FastIpcMessage(event, channel, args);
  std::move(callback).Run();
}

void ElectronApiFastIpcHandler::FastIpcInvoke(
    bool internal,
    const std::string& channel,
    electron::mojom::TypedArrayCloneableMessagePtr message,
    int32_t sender_id,
    FastIpcInvokeCallback callback) {
  auto* session = GetSession();
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);

  auto event = MakeFastIpcEvent(isolate, session, internal, std::move(callback));
  if (event.IsEmpty())
    return;
  v8::Local<v8::Value> args =
      electron::DeserializeV8ValueWithTypedArrays(
          isolate, *message);
  session->FastIpcInvoke(event, channel, args);
}

void ElectronApiFastIpcHandler::FastIpcMessageSync(
    bool internal,
    const std::string& channel,
    electron::mojom::TypedArrayCloneableMessagePtr message,
    int32_t sender_id,
    FastIpcMessageSyncCallback callback) {
  auto* session = GetSession();
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);

  auto event = MakeFastIpcEvent(isolate, session, internal, std::move(callback));
  if (event.IsEmpty())
    return;
  v8::Local<v8::Value> args =
      electron::DeserializeV8ValueWithTypedArrays(
          isolate, *message);
  session->FastIpcMessageSync(event, channel, args);
}

void ElectronApiFastIpcHandler::ReceiveFastIpcPostMessage(
    const std::string& channel,
    electron::mojom::TransferableTypedArrayMessagePtr message,
    int32_t sender_id) {
  LOG(INFO) << "[FastIpcHandler] PostMessage received on channel: " << channel;
  // Handle transferable messages with shared memory support
  // TODO: Implement message handling with new serializer
}

void ElectronApiFastIpcHandler::FastIpcMessageHost(
    const std::string& channel,
    electron::mojom::TypedArrayCloneableMessagePtr message,
    int32_t sender_id,
    FastIpcMessageHostCallback callback) {
  auto* session = GetSession();
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);

  auto event = MakeFastIpcEvent(isolate, session, false);
  if (event.IsEmpty()) {
    std::move(callback).Run();
    return;
  }

  v8::Local<v8::Value> args =
      electron::DeserializeV8ValueWithTypedArrays(
          isolate, *message);
  session->FastIpcMessageHost(event, channel, args);

  std::move(callback).Run();
}

void ElectronApiFastIpcHandler::WebContentsDestroyed() {
  delete this;
}

void ElectronApiFastIpcHandler::OnConnectionError() {
  delete this;
}

content::RenderFrameHost* ElectronApiFastIpcHandler::GetRenderFrameHost() {
  return content::RenderFrameHost::FromID(render_frame_host_id_);
}

api::Session* ElectronApiFastIpcHandler::GetSession() {
  auto* rfh = GetRenderFrameHost();
  return rfh ? api::Session::FromBrowserContext(rfh->GetBrowserContext())
             : nullptr;
}

gin::Handle<gin_helper::internal::Event>
ElectronApiFastIpcHandler::MakeFastIpcEvent(
    v8::Isolate* isolate,
    api::Session* session,
    bool internal,
    FastIpcInvokeCallback callback) {
  if (!session) {
    if (callback) {
      // We must always invoke the callback if present.
      gin_helper::internal::FastIpcReplyChannel::Create(isolate, std::move(callback))
          ->SendError("Session does not exist");
    }
    return {};
  }

  api::WebContents* api_web_contents = api::WebContents::From(web_contents());
  if (!api_web_contents) {
    if (callback) {
      // We must always invoke the callback if present.
      gin_helper::internal::FastIpcReplyChannel::Create(isolate, std::move(callback))
          ->SendError("WebContents does not exist");
    }
    return {};
  }

  v8::Local<v8::Object> wrapper;
  if (!api_web_contents->GetWrapper(isolate).ToLocal(&wrapper)) {
    if (callback) {
      // We must always invoke the callback if present.
      gin_helper::internal::FastIpcReplyChannel::Create(isolate, std::move(callback))
          ->SendError("WebContents was destroyed");
    }
    return {};
  }

  content::RenderFrameHost* frame = GetRenderFrameHost();
  gin::Handle<gin_helper::internal::Event> event =
      gin_helper::internal::Event::New(isolate);
  gin_helper::Dictionary dict(isolate, event.ToV8().As<v8::Object>());
  dict.Set("type", "frame");
  dict.Set("sender", web_contents());
  if (internal)
    dict.SetHidden("internal", internal);
  if (callback)
    dict.Set("_replyChannel", gin_helper::internal::FastIpcReplyChannel::Create(
                                  isolate, std::move(callback)));
  if (frame) {
    dict.SetGetter("senderFrame", frame);
    dict.Set("frameId", frame->GetRoutingID());
    dict.Set("processId", frame->GetProcess()->GetID().GetUnsafeValue());
    dict.Set("frameTreeNodeId", frame->GetFrameTreeNodeId());
  }
  return event;
}

}  // namespace electron
