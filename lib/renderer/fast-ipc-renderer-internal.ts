import { getFastIPCRenderer } from '@electron/internal/renderer/fast-ipc-renderer-bindings';

import { EventEmitter } from 'events';

const fastIpc = getFastIPCRenderer();
const internal = true;

class FastIpcRendererInternal extends EventEmitter implements ElectronInternal.FastIpcRendererInternal {
  send (channel: string, ...args: any[]) {
    return fastIpc.send(internal, channel, args);
  }

  sendSync (channel: string, ...args: any[]) {
    return fastIpc.sendSync(internal, channel, args);
  }

  async invoke<T> (channel: string, ...args: any[]) {
    const { error, result } = await fastIpc.invoke(internal, channel, args) as { error: string, result: T };
    if (error) {
      throw new Error(`Error invoking remote method '${channel}': ${error}`);
    }
    return result;
  }
}

export const fastIpcRendererInternal = new FastIpcRendererInternal();
