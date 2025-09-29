import { fastIpcRendererInternal } from '@electron/internal/renderer/fast-ipc-renderer-internal';

type FastIPCHandler = (event: Electron.IpcRendererEvent, ...args: any[]) => any

export const handle = function <T extends FastIPCHandler> (channel: string, handler: T) {
  fastIpcRendererInternal.on(channel, async (event, requestId, ...args) => {
    const replyChannel = `${channel}_RESPONSE_${requestId}`;
    try {
      event.sender.send(replyChannel, null, await handler(event, ...args));
    } catch (error) {
      event.sender.send(replyChannel, error);
    }
  });
};

export function invokeSync<T> (command: string, ...args: any[]): T {
  const [error, result] = fastIpcRendererInternal.sendSync(command, ...args);

  if (error) {
    throw error;
  } else {
    return result;
  }
}
