import { getFastIPCRenderer } from '@electron/internal/renderer/fast-ipc-renderer-bindings';

import { EventEmitter } from 'events';

const fastIpc = getFastIPCRenderer();
const internal = false;

class FastIpcRenderer extends EventEmitter implements Electron.IpcRenderer {
  send (channel: string, ...args: any[]) {
    return fastIpc.send(internal, channel, args);
  }

  sendSync (channel: string, ...args: any[]) {
    return fastIpc.sendSync(internal, channel, args);
  }

  sendToHost (channel: string, ...args: any[]) {
    return fastIpc.sendToHost(channel, args);
  }

  async invoke (channel: string, ...args: any[]) {
    const { error, result } = await fastIpc.invoke(internal, channel, args);
    if (error) {
      throw new Error(`Error invoking remote method '${channel}': ${error}`);
    }
    return result;
  }

  postMessage (channel: string, message: any, transferables: any) {
    return fastIpc.postMessage(channel, message, transferables);
  }
}

export default new FastIpcRenderer();
