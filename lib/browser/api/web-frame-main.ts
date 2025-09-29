import { IpcMainImpl } from '@electron/internal/browser/ipc-main-impl';
import { FastIpcMainImpl } from '@electron/internal/browser/fast-ipc-main-impl';
import { MessagePortMain } from '@electron/internal/browser/message-port-main';

const { WebFrameMain, fromId } = process._linkedBinding('electron_browser_web_frame_main');

Object.defineProperty(WebFrameMain.prototype, 'ipc', {
  get () {
    const ipc = new IpcMainImpl();
    Object.defineProperty(this, 'ipc', { value: ipc });
    return ipc;
  }
});

Object.defineProperty(WebFrameMain.prototype, 'fastIpc', {
  get () {
    const fastIpc = new FastIpcMainImpl();
    Object.defineProperty(this, 'fastIpc', { value: fastIpc });
    return fastIpc;
  }
});

WebFrameMain.prototype.send = function (channel, ...args) {
  if (typeof channel !== 'string') {
    throw new TypeError('Missing required channel argument');
  }

  try {
    return this._send(false /* internal */, channel, args);
  } catch (e) {
    console.error('Error sending from webFrameMain: ', e);
  }
};

// Direct Transfer version of send
(WebFrameMain.prototype as any).sendFastIpc = function (channel: string, ...args: any[]) {
  if (typeof channel !== 'string') {
    throw new TypeError('Missing required channel argument');
  }

  try {
    // Check if Direct Transfer send is available
    if ((this as any)._sendFastIpc) {
      return (this as any)._sendFastIpc(false /* internal */, channel, args);
    } else {
      // Fallback to regular send if Direct Transfer not implemented
      console.warn('[WebFrameMain] _sendFastIpc not available, using regular send for channel:', channel);
      console.warn('[WebFrameMain] Frame info:', {
        processId: this.processId,
        routingId: this.routingId,
        url: this.url
      });
      return (this as any)._send(false /* internal */, channel, args);
    }
  } catch (e) {
    console.error('Error sending Direct Transfer from webFrameMain: ', e);
  }
};

WebFrameMain.prototype._sendInternal = function (channel, ...args) {
  if (typeof channel !== 'string') {
    throw new TypeError('Missing required channel argument');
  }

  try {
    return this._send(true /* internal */, channel, args);
  } catch (e) {
    console.error('Error sending from webFrameMain: ', e);
  }
};

WebFrameMain.prototype._sendFastIpcMessage = function (internal, channel, args) {
  if (typeof channel !== 'string') {
    throw new TypeError('Missing required channel argument');
  }

  try {
    // Use the same _send method but mark it for direct transfer
    return this._send(internal, channel, args);
  } catch (e) {
    console.error('Error sending direct transfer from webFrameMain: ', e);
  }
};

WebFrameMain.prototype.postMessage = function (...args) {
  if (Array.isArray(args[2])) {
    args[2] = args[2].map(o => o instanceof MessagePortMain ? o._internalPort : o);
  }
  this._postMessage(...args);
};

export default {
  fromId
};
