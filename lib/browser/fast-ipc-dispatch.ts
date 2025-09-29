import { fastIpcMainInternal } from '@electron/internal/browser/fast-ipc-main-internal';

import { fastIpcMain } from 'electron/main';

const v8Util = process._linkedBinding('electron_common_v8_util');
const webFrameMainBinding = process._linkedBinding('electron_browser_web_frame_main');

const addReplyToEvent = (event: Electron.FastIpcMainEvent) => {
  const { processId, frameId } = event;
  event.reply = (channel: string, ...args: any[]) => {
    event.sender.sendFastIpcToFrame([processId, frameId], channel, ...args);
  };
};

const addReturnValueToEvent = (event: Electron.FastIpcMainEvent) => {
  Object.defineProperty(event, 'returnValue', {
    set: (value) => event._replyChannel.sendReply(value),
    get: () => {}
  });
};

/**
 * Cached FastIPC emitters sorted by dispatch priority.
 * Caching is used to avoid frequent array allocations.
 */
const cachedFastIpcEmitters: (ElectronInternal.FastIpcMainInternal | undefined)[] = [
  undefined, // WebFrameMain fastIpc
  undefined, // WebContents fastIpc
  fastIpcMain
];

// Get list of relevant FastIPC emitters for dispatch.
const getFastIpcEmittersForFrameEvent = (event: Electron.FastIpcMainEvent | Electron.IpcMainInvokeEvent): (ElectronInternal.FastIpcMainInternal | undefined)[] => {
  // Lookup by FrameTreeNode ID to ensure IPCs received after a frame swap are
  // always received. This occurs when a RenderFrame sends an IPC while it's
  // unloading and its internal state is pending deletion.
  const { frameTreeNodeId } = event;
  const webFrameByFtn = frameTreeNodeId ? webFrameMainBinding._fromFtnIdIfExists(frameTreeNodeId) : undefined;
  cachedFastIpcEmitters[0] = webFrameByFtn?.fastIpc;
  cachedFastIpcEmitters[1] = event.sender.fastIpc;
  return cachedFastIpcEmitters;
};

/**
 * Listens for FastIPC dispatch events on `api`.
 */
export function addFastIpcDispatchListeners (api: NodeJS.EventEmitter) {
  // Handle fast-ipc message (one-way, no response)
  api.on('-fast-ipc-message' as any, function (event: any, channel: string, args: any[]) {
    const internal = v8Util.getHiddenValue<boolean>(event, 'internal');

    if (internal) {
      fastIpcMainInternal.emit(channel, event, ...args);
    } else {
      addReplyToEvent(event);
      event.sender.emit('fast-ipc-message', event, channel, ...args);
      for (const fastIpcEmitter of getFastIpcEmittersForFrameEvent(event)) {
        fastIpcEmitter?.emit(channel, event, ...args);
      }
    }
  } as any);

  // Handle fast-ipc invoke (async with response)
  api.on('-fast-ipc-invoke' as any, async function (event: any, channel: string, args: any[]) {
    const internal = v8Util.getHiddenValue<boolean>(event, 'internal');

    const replyWithResult = (result: any) => event._replyChannel.sendReply({ result });
    const replyWithError = (error: Error) => {
      console.error(`Error occurred in handler for '${channel}':`, error);
      event._replyChannel.sendReply({ error: error.toString() });
    };

    const targets: (ElectronInternal.FastIpcMainInternal | undefined)[] = [];

    if (internal) {
      targets.push(fastIpcMainInternal);
    } else {
      targets.push(...getFastIpcEmittersForFrameEvent(event));
    }

    const target = targets.find(target => (target as any)?._invokeHandlers.has(channel));
    if (target) {
      const handler = (target as any)._invokeHandlers.get(channel);
      try {
        replyWithResult(await Promise.resolve(handler(event, ...args)));
      } catch (err) {
        replyWithError(err as Error);
      }
    } else {
      replyWithError(new Error(`No handler registered for '${channel}'`));
    }
  } as any);

  // Handle fast-ipc sync message
  api.on('-fast-ipc-message-sync' as any, function (event: any, channel: string, args: any[]) {
    const internal = v8Util.getHiddenValue<boolean>(event, 'internal');
    addReturnValueToEvent(event);

    if (internal) {
      fastIpcMainInternal.emit(channel, event, ...args);
    } else {
      addReplyToEvent(event);
      const webContents = event.sender;
      const fastIpcEmitters = getFastIpcEmittersForFrameEvent(event);
      if (
        webContents.listenerCount('fast-ipc-message-sync') === 0 &&
        fastIpcEmitters.every(emitter => !emitter || emitter.listenerCount(channel) === 0)
      ) {
        console.warn(`WebContents #${webContents.id} called fastIpcRenderer.sendSync() with '${channel}' channel without listeners.`);
      }
      webContents.emit('fast-ipc-message-sync', event, channel, ...args);
      for (const fastIpcEmitter of fastIpcEmitters) {
        fastIpcEmitter?.emit(channel, event, ...args);
      }
    }
  } as any);

  // Handle fast-ipc message to host
  api.on('-fast-ipc-message-host', function (event: any, channel: string, args: any[]) {
    event.sender.emit('-fast-ipc-message-host', event, channel, args);
  });
}
