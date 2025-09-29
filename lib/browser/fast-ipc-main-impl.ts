import { IpcMainInvokeEvent } from 'electron/main';

import { EventEmitter } from 'events';

export class FastIpcMainImpl extends EventEmitter implements ElectronInternal.FastIpcMainInternal {
  _invokeHandlers: Map<string, (e: IpcMainInvokeEvent, ...args: any[]) => void> = new Map();

  constructor () {
    super();

    // Do not throw exception when channel name is "error".
    this.on('error', () => {});
  }

  handle: ElectronInternal.FastIpcMainInternal['handle'] = (method, fn) => {
    if (this._invokeHandlers.has(method)) {
      throw new Error(`Attempted to register a second handler for '${method}'`);
    }
    if (typeof fn !== 'function') {
      throw new TypeError(`Expected handler to be a function, but found type '${typeof fn}'`);
    }
    this._invokeHandlers.set(method, fn);
  };

  handleOnce: ElectronInternal.FastIpcMainInternal['handle'] = (method, fn) => {
    this.handle(method, (e, ...args) => {
      this.removeHandler(method);
      return fn(e, ...args);
    });
  };

  removeHandler (method: string) {
    this._invokeHandlers.delete(method);
  }
}
