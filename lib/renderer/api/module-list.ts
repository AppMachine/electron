// Renderer side modules, please sort alphabetically.
export const rendererModuleList: ElectronInternal.ModuleEntry[] = [
  { name: 'clipboard', loader: () => require('./clipboard') },
  { name: 'contextBridge', loader: () => require('./context-bridge') },
  { name: 'crashReporter', loader: () => require('./crash-reporter') },
  { name: 'fastIpcRenderer', loader: () => require('./fast-ipc-renderer') },
  // { name: 'ipcRenderer', loader: () => require('./ipc-renderer') },
  { name: 'ipcRenderer', loader: () => require('./fast-ipc-renderer') },
  { name: 'webFrame', loader: () => require('./web-frame') },
  { name: 'webUtils', loader: () => require('./web-utils') }
];
