const { app, BrowserWindow } = require('electron');

let win = null;

app.commandLine.appendSwitch('ignore-gpu-blacklist');

function createWindow() {
    win = new BrowserWindow({
        width: 560,
        height: 520,
        resizable: false,
        webPreferences: {
            contextIsolation: false,
            nodeIntegration: true
        }
    });
    
    win.loadFile('index.html');
}

app.whenReady().then(createWindow);

// 关闭窗口时完全退出（所有平台）
app.on('window-all-closed', () => app.quit());
