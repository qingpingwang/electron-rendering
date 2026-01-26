const { app, BrowserWindow } = require('electron');

let win = null;

app.commandLine.appendSwitch('ignore-gpu-blacklist');

function createWindow() {
    win = new BrowserWindow({
        width: 800,
        height: 600,
        minWidth: 640,
        minHeight: 480,
        webPreferences: {
            contextIsolation: false,
            nodeIntegration: true,
            enableRemoteModule: true
        }
    });
    
    // 启用 @electron/remote
    try {
        require('@electron/remote/main').initialize();
        require('@electron/remote/main').enable(win.webContents);
    } catch (e) {
        console.log('remote module not available:', e.message);
    }
    
    win.loadFile('index.html');
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
    app.quit();
});

app.on('activate', () => {
    if (!win) createWindow();
});
