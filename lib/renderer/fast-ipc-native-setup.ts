import { fastIpcRendererInternal } from '@electron/internal/renderer/fast-ipc-renderer-internal';

let fastIpcRenderer: any;
try {
  fastIpcRenderer = require('@electron/internal/renderer/api/fast-ipc-renderer').default;
} catch (e) {
  // fastIpcRenderer might not be available in sandboxed mode
  console.warn('fastIpcRenderer not available in this context');
}

const v8Util = process._linkedBinding('electron_common_v8_util');

// ElectronApiServiceImpl will look for the "fastIpcNative" hidden object when
// invoking the 'onMessage' callback for FastIPC messages.
v8Util.setHiddenValue(globalThis, 'fastIpcNative', {
  onMessage (internal: boolean, channel: string, ports: MessagePort[], args: any[]) {
    const sender = internal ? fastIpcRendererInternal : fastIpcRenderer;
    if (sender) {
      sender.emit(channel, { sender, ports }, ...args);
    }
  }
});
