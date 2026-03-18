const { app, ipcMain, Menu } = require('electron');


let wm = null;
let media = null;
let agentReady = false;
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
    const MediaManager = require('./media/media_manager');
    const { initAgent, updateWebContents, onProjectOpened } = require('./agent');

    db.init();

    wm = new WindowManager();
    media = new MediaManager();
    media.registerIPC();

    wm.showHome();

    ipcMain.on('open-project', (_event, { uuid, configPath }) => {
        if (!wm.mainWindow) {
            wm.createMainWindow();

            wm.mainWindow.on('closed', () => {
                wm.closeEditor();
                if (!_quitting) wm.showHome();
            });
        }

        wm.closeHome();
        media.setProject(uuid);

        const editorWC = wm.getEditorWebContents();
        const chatWC = wm.getChatWebContents();

        if (!agentReady && editorWC && chatWC) {
            initAgent(editorWC, chatWC);
            agentReady = true;
        } else if (agentReady && editorWC && chatWC) {
            updateWebContents(editorWC, chatWC);
        }

        const sendLoad = () => {
            editorWC.send('load-project', { uuid, configPath });
            onProjectOpened(uuid);
        };

        if (editorWC.isLoading()) {
            editorWC.once('did-finish-load', sendLoad);
        } else {
            sendLoad();
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
