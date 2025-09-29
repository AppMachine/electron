import { FastIpcMainImpl } from '@electron/internal/browser/fast-ipc-main-impl';

export const fastIpcMainInternal = new FastIpcMainImpl() as ElectronInternal.FastIpcMainInternal;