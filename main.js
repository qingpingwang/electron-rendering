const { app, ipcMain, Menu } = require('electron');


let wm = null;
let _quitting = false;

app.whenReady().then(() => {
    try {
        require('@electron/remote/main').initialize();
    } catch { }

    const template = [
        ...(process.platform === 'darwin' ? [{
            label: app.name,
            submenu: [
                { role: 'about' },
                { type: 'separator' },
                { role: 'hide' },
                { role: 'hideOthers' },
                { role: 'unhide' },
                { type: 'separator' },
                { role: 'quit' },
            ],
        }] : []),
        {
            label: '编辑', submenu: [
                { role: 'undo' }, { role: 'redo' }, { type: 'separator' },
                { role: 'cut' }, { role: 'copy' }, { role: 'paste' },
                { role: 'selectAll' },
            ]
        },
        {
            label: '窗口', submenu: [
                { role: 'minimize' }, { role: 'zoom' }, { role: 'close' },
                ...(process.platform === 'darwin' ? [
                    { type: 'separator' }, { role: 'front' },
                ] : []),
            ]
        },
    ];
    Menu.setApplicationMenu(Menu.buildFromTemplate(template));

    const db = require('./db');
    const WindowManager = require('./window/window_manager');

    db.init();

    wm = new WindowManager();
    wm.showHome();

    // Tool-call relay: chat renderer → editor
    ipcMain.on('tool-call', (_event, data) => {
        const editorWC = wm.getEditorWebContents();
        if (editorWC && !editorWC.isDestroyed()) {
            editorWC.send('tool-call', data);
        }
    });

    // Tool-result relay: editor → chat renderer
    ipcMain.on('tool-result', (_event, result) => {
        const chatWC = wm.getChatWebContents();
        if (chatWC && !chatWC.isDestroyed()) {
            chatWC.send('tool-result', result);
        }
    });

    ipcMain.on('open-project', (_event, { uuid, configPath }) => {
        if (!wm.mainWindow) {
            wm.createMainWindow();

            wm.mainWindow.on('closed', () => {
                wm.closeEditor();
                if (!_quitting) wm.showHome();
            });
        }

        wm.closeHome();

        const editorWC = wm.getEditorWebContents();
        const chatWC = wm.getChatWebContents();

        const sendLoad = () => {
            editorWC.send('load-project', { uuid, configPath });
        };

        const notifyChat = () => {
            chatWC.executeJavaScript(
                `window.__onProjectOpened && window.__onProjectOpened(${JSON.stringify(uuid)})`
            );
        };

        if (editorWC.isLoading()) {
            editorWC.once('did-finish-load', sendLoad);
        } else {
            sendLoad();
        }

        if (chatWC.isLoading()) {
            chatWC.once('did-finish-load', notifyChat);
        } else {
            notifyChat();
        }
    });
});

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
    if (wm && !wm.homeWindow && !wm.mainWindow) {
        wm.showHome();
    }
});

app.on('before-quit', () => {
    _quitting = true;
});

app.on('will-quit', () => {
    try { require('./db').close(); } catch { }
});
