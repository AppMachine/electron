let fastIpc: NodeJS.FastIpcRendererImpl | undefined;

/**
 * Get FastIPCRenderer implementation for the current process.
 */
export function getFastIPCRenderer () {
  if (fastIpc) return fastIpc;
  const fastIpcBinding = process._linkedBinding('electron_renderer_fast_ipc');
  switch (process.type) {
    case 'renderer':
      return (fastIpc = fastIpcBinding.createForRenderFrame());
    case 'service-worker':
      return (fastIpc = fastIpcBinding.createForServiceWorker());
    default:
      throw new Error(`Cannot create FastIPCRenderer for '${process.type}' process`);
  }
}
