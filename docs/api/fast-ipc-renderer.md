---
title: "fastIpcRenderer"
description: "High-performance asynchronous communication from a renderer process to the main process."
slug: fast-ipc-renderer
hide_title: false
---

# fastIpcRenderer

> High-performance asynchronous communication from a renderer process to the main process.

Process: [Renderer](../glossary.md#renderer-process)

The `fastIpcRenderer` module is an [EventEmitter][event-emitter]. It provides a high-performance
alternative to the standard `ipcRenderer` module for scenarios requiring optimized inter-process
communication. It provides a few methods so you can send synchronous and asynchronous messages
from the render process (web page) to the main process. You can also receive replies from the
main process.

See [FastIPC guide](../tutorial/fast-ipc.md) for usage examples.

## Performance Considerations

`fastIpcRenderer` is designed for high-throughput scenarios where message passing performance is
critical. It uses optimized native bindings to reduce serialization overhead compared to the
standard IPC mechanism.

## Methods

The `fastIpcRenderer` module has the following methods to send messages:

### `fastIpcRenderer.on(channel, listener)`

* `channel` string
* `listener` Function
  * `event` [IpcRendererEvent][ipc-renderer-event]
  * `...args` any[]

Listens to `channel`, when a new message arrives `listener` would be called with
`listener(event, args...)`.

### `fastIpcRenderer.off(channel, listener)`

* `channel` string
* `listener` Function
  * `...args` any[]

Alias for `fastIpcRenderer.removeListener`.

### `fastIpcRenderer.once(channel, listener)`

* `channel` string
* `listener` Function
  * `event` [IpcRendererEvent][ipc-renderer-event]
  * `...args` any[]

Adds a one-time `listener` function for the event. This `listener` is invoked
only the next time a message is sent to `channel`, after which it is removed.

### `fastIpcRenderer.addListener(channel, listener)`

* `channel` string
* `listener` Function
  * `...args` any[]

Alias for `fastIpcRenderer.on`.

### `fastIpcRenderer.removeListener(channel, listener)`

* `channel` string
* `listener` Function
  * `...args` any[]

Removes the specified `listener` from the listener array for the specified
`channel`.

### `fastIpcRenderer.removeAllListeners([channel])`

* `channel` string (optional)

Removes all listeners, or those of the specified `channel`.

### `fastIpcRenderer.send(channel, ...args)`

* `channel` string
* `...args` any[]

Send an asynchronous message to the main process via `channel`, along with
arguments. Arguments will be serialized with the [Structured Clone
Algorithm][SCA], just like [`window.postMessage`][], so prototype chains will not be
included. Sending Functions, Promises, Symbols, WeakMaps, or WeakSets will
throw an exception.

> [!NOTE]
> Sending non-standard JavaScript types such as DOM objects or
> special Electron objects will throw an exception.

Since the main process does not have support for DOM objects such as
`ImageBitmap`, `File`, `DOMMatrix` and so on, such objects cannot be sent over
Electron's IPC to the main process, as the main process would have no way to decode
them. Attempting to send such objects over IPC will result in an error.

The main process handles it by listening for `channel` with the
[`fastIpcMain`](fast-ipc-main.md) module.

If you need to transfer a [`MessagePort`][] to the main process, use
[`fastIpcRenderer.postMessage`](#fastipcrendererpostmessagechannel-message-transfer).

If you want to receive a single response from the main process, like the result
of a method call, consider using [`fastIpcRenderer.invoke`](#fastipcrendererinvokechannel-args).

### `fastIpcRenderer.invoke(channel, ...args)`

* `channel` string
* `...args` any[]

Returns `Promise<any>` - Resolves with the response from the main process.

Send a message to the main process via `channel` and expect a result
asynchronously. Arguments will be serialized with the [Structured Clone
Algorithm][SCA], just like [`window.postMessage`][], so prototype chains will not be
included. Sending Functions, Promises, Symbols, WeakMaps, or WeakSets will
throw an exception.

The main process should listen for `channel` with
[`fastIpcMain.handle()`](fast-ipc-main.md#fastipcmainhandlechannel-listener).

For example:

```js
// Renderer process
fastIpcRenderer.invoke('some-name', someArgument).then((result) => {
  // ...
})

// Main process
fastIpcMain.handle('some-name', async (event, someArgument) => {
  const result = await doSomeWork(someArgument)
  return result
})
```

If you need to transfer a [`MessagePort`][] to the main process, use
[`fastIpcRenderer.postMessage`](#fastipcrendererpostmessagechannel-message-transfer).

If you do not need a response to the message, consider using [`fastIpcRenderer.send`](#fastiprenderersendchannel-args).

> [!NOTE]
> Sending non-standard JavaScript types such as DOM objects or
> special Electron objects will throw an exception.

Since the main process does not have support for DOM objects such as
`ImageBitmap`, `File`, `DOMMatrix` and so on, such objects cannot be sent over
Electron's IPC to the main process, as the main process would have no way to decode
them. Attempting to send such objects over IPC will result in an error.

> [!NOTE]
> If the handler in the main process throws an error,
> the promise returned by `invoke` will reject. However, the `Error` object in
> the renderer process will not be the same as the one thrown in the main
> process.

### `fastIpcRenderer.sendSync(channel, ...args)`

* `channel` string
* `...args` any[]

Returns `any` - The value sent back by the [`fastIpcMain`](fast-ipc-main.md) handler.

Send a message to the main process via `channel` and expect a result
synchronously. Arguments will be serialized with the [Structured Clone
Algorithm][SCA], just like [`window.postMessage`][], so prototype chains will not be
included. Sending Functions, Promises, Symbols, WeakMaps, or WeakSets will
throw an exception.

> [!NOTE]
> Sending non-standard JavaScript types such as DOM objects or
> special Electron objects will throw an exception.

Since the main process does not have support for DOM objects such as
`ImageBitmap`, `File`, `DOMMatrix` and so on, such objects cannot be sent over
Electron's IPC to the main process, as the main process would have no way to decode
them. Attempting to send such objects over IPC will result in an error.

The main process handles it by listening for `channel` with [`fastIpcMain`](fast-ipc-main.md)
module, and replies by setting `event.returnValue`.

> [!WARNING]
> Sending a synchronous message will block the whole renderer process until the
> reply is received, so use this method only as a last resort. It's much better to
> use the asynchronous version, [`invoke()`](fast-ipc-renderer.md#fastipcrendererinvokechannel-args).

### `fastIpcRenderer.postMessage(channel, message, [transfer])`

* `channel` string
* `message` any
* `transfer` MessagePort[] (optional)

Send a message to the main process, optionally transferring ownership of zero
or more [`MessagePort`][] objects.

The transferred `MessagePort` objects will be available in the main process as
[`MessagePortMain`](message-port-main.md) objects by accessing the `ports`
property of the emitted event.

For example:

```js
// Renderer process
const { port1, port2 } = new MessageChannel()
fastIpcRenderer.postMessage('port', { message: 'hello' }, [port1])

// Main process
fastIpcMain.on('port', (e, msg) => {
  const [port] = e.ports
  // ...
})
```

For more information on using `MessagePort` and `MessageChannel`, see the [MDN
documentation](https://developer.mozilla.org/en-US/docs/Web/API/MessageChannel).

### `fastIpcRenderer.sendToHost(channel, ...args)`

* `channel` string
* `...args` any[]

Like `fastIpcRenderer.send` but the event will be sent to the `<webview>` element in
the host page instead of the main process.

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
[SCA]: https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Structured_clone_algorithm
[`window.postMessage`]: https://developer.mozilla.org/en-US/docs/Web/API/Window/postMessage
[`MessagePort`]: https://developer.mozilla.org/en-US/docs/Web/API/MessagePort
[ipc-renderer-event]: structures/ipc-renderer-event.md