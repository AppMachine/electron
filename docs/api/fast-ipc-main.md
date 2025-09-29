---
title: "fastIpcMain"
description: "High-performance asynchronous communication from the main process to renderer processes."
slug: fast-ipc-main
hide_title: false
---

# fastIpcMain

> High-performance asynchronous communication from the main process to renderer processes.

Process: [Main](../glossary.md#main-process)

The `fastIpcMain` module is an [Event Emitter][event-emitter]. It provides a high-performance
alternative to the standard `ipcMain` module for scenarios requiring optimized inter-process
communication. When used in the main process, it handles asynchronous and synchronous messages
sent from a renderer process using `fastIpcRenderer`.

## Performance Considerations

`fastIpcMain` is designed for high-throughput scenarios where message passing performance is
critical. It uses optimized native bindings to reduce serialization overhead compared to the
standard IPC mechanism.

## Sending messages

It is also possible to send messages from the main process to the renderer process, see
[webContents.sendFastIpc][web-contents-send-fast-ipc] for more information.

* When sending a message, the event name is the `channel`.
* To reply to a synchronous message, you need to set `event.returnValue`.
* To send an asynchronous message back to the sender, you can use
  `event.reply(...)`. This helper method will automatically handle messages
  coming from frames that aren't the main frame (e.g. iframes) whereas
  `event.sender.send(...)` will always send to the main frame.

## Methods

The `fastIpcMain` module has the following methods to listen for events:

### `fastIpcMain.on(channel, listener)`

* `channel` string
* `listener` Function
  * `event` [FastIpcMainEvent][fast-ipc-main-event]
  * `...args` any[]

Listens to `channel`, when a new message arrives `listener` would be called with
`listener(event, args...)`.

### `fastIpcMain.off(channel, listener)`

* `channel` string
* `listener` Function
  * `...args` any[]

Removes the specified `listener` from the listener array for the specified `channel`.

### `fastIpcMain.once(channel, listener)`

* `channel` string
* `listener` Function
  * `event` [FastIpcMainEvent][fast-ipc-main-event]
  * `...args` any[]

Adds a one-time `listener` function for the event. This `listener` is invoked
only the next time a message is sent to `channel`, after which it is removed.

### `fastIpcMain.removeListener(channel, listener)`

* `channel` string
* `listener` Function
  * `...args` any[]

Alias for `fastIpcMain.off`.

### `fastIpcMain.removeAllListeners([channel])`

* `channel` string (optional)

Removes all listeners, or those of the specified `channel`.

### `fastIpcMain.handle(channel, listener)`

* `channel` string
* `listener` Function
  * `event` [FastIpcMainInvokeEvent][fast-ipc-main-invoke-event]
  * `...args` any[]

Adds a handler for an `invoke`able IPC. This handler will be called whenever a
renderer calls `fastIpcRenderer.invoke(channel, ...args)`.

If `listener` returns a Promise, the eventual result of the promise will be
returned as a reply to the remote caller. Otherwise, the return value of the
listener will be used as the value of the reply.

```js
// Main process
fastIpcMain.handle('my-invokable-ipc', async (event, ...args) => {
  const result = await somePromise(...args)
  return result
})

// Renderer process
async () => {
  const result = await fastIpcRenderer.invoke('my-invokable-ipc', arg1, arg2)
  // ...
}
```

The `event` that is passed as the first argument to the handler is the same as
that passed to a regular event listener. It includes information about which
WebContents is the source of the invoke request.

Errors thrown through `handle` in the main process are not transparent as they
are serialized and only the `message` property from the original error is
provided to the renderer process. Please refer to
[#24427](https://github.com/electron/electron/issues/24427) for details.

### `fastIpcMain.handleOnce(channel, listener)`

* `channel` string
* `listener` Function
  * `event` [FastIpcMainInvokeEvent][fast-ipc-main-invoke-event]
  * `...args` any[]

Handles a single `invoke`able IPC message, then removes the listener. See
`fastIpcMain.handle(channel, listener)`.

### `fastIpcMain.removeHandler(channel)`

* `channel` string

Removes any handler for `channel`, if present.

## FastIpcMainEvent object

The documentation for the `event` object passed to the `callback` can be found
in the [`fast-ipc-main-event`](structures/fast-ipc-main-event.md) structure docs.

## FastIpcMainInvokeEvent object

The documentation for the `event` object passed to `handle` callbacks can be
found in the [`fast-ipc-main-invoke-event`](structures/fast-ipc-main-invoke-event.md)
structure docs.

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
[web-contents-send-fast-ipc]: web-contents.md#contentssendFastipc
[fast-ipc-main-event]: structures/fast-ipc-main-event.md
[fast-ipc-main-invoke-event]: structures/fast-ipc-main-invoke-event.md