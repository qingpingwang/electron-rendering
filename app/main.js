const { app, ipcMain, Menu } = require('electron');

let wm = null;
let _quitting = false;

// ─────────────────────────────────────────────
// WebContents 工具函数
// ─────────────────────────────────────────────

/** WebContents 就绪后执行 fn（若仍在加载则等 did-finish-load） */
function runWhenReady(wc, fn) {
    if (wc.isLoading()) wc.once('did-finish-load', fn);
    else fn();
}

/** 向目标 WebContents 发送 __onProjectOpened 通知 */
function notifyProjectOpened(wc, uuid, options) {
    runWhenReady(wc, () =>
        wc.executeJavaScript(
            `window.__onProjectOpened && window.__onProjectOpened(${JSON.stringify(uuid)}, ${JSON.stringify(options)})`
        )
    );
}

// ─────────────────────────────────────────────
// IPC 路由
// ─────────────────────────────────────────────

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

    ipcMain.on('open-project', (_event, payload) => {
        const { type = 'editor', uuid, configPath, isNew = false, name } = payload || {};

        if (type === 'resource') {
            openResourceProject(wm, { uuid, name, isNew });
            return;
        }

        // type === 'editor'
        if (!wm.mainWindow) {
            wm.createMainWindow();

            wm.mainWindow.on('closed', () => {
                wm.closeEditor();
                if (!_quitting) wm.showHome();
            });
        }

        wm.closeHome();

        const editorWC = wm.getEditorWebContents();
        const chatWC   = wm.getChatWebContents();

        runWhenReady(editorWC, () => editorWC.send('load-project', { uuid, configPath }));
        notifyProjectOpened(chatWC, uuid, { isNewThread: isNew, mode: 'editor' });
    });
});

/** 渲染资源工程：仅 chat 窗口，没有编辑器视图，不绑定任何项目文件夹 */
function openResourceProject(wm, { uuid, name, isNew }) {
    wm.closeHome();
    const win = wm.showResourceWindow();
    const wc = wm.getResourceChatWebContents();

    // 记录当前打开的项目，用于窗口关闭时清理
    wm._currentResourceUuid = uuid;

    notifyProjectOpened(wc, uuid, { isNewThread: !!isNew, mode: 'resource', name });

    // 首次创建窗口时绑定关闭清理（只绑一次）
    if (!wm._resourceWindowCleanupBound) {
        wm._resourceWindowCleanupBound = true;
        win.on('closed', () => {
            wm._resourceWindowCleanupBound = false;
            const curUuid = wm._currentResourceUuid;
            wm._currentResourceUuid = null;
            if (!curUuid) return;

            // 通过 checkpointer 接口判断是否有对话记录，无则删除空 thread
            const agentMod = require('./agent');
            agentMod.getHistory(curUuid, 'resource').then(({ messages }) => {
                if (!messages || messages.length === 0) {
                    agentMod.deleteThread(curUuid, 'resource').catch(() => {});
                    if (wm.homeWindow && !wm.homeWindow.isDestroyed()) {
                        wm.homeWindow.webContents.send('refresh-history');
                    }
                }
            }).catch(e => {
                console.warn('[Resource] Cleanup check failed:', e.message);
            });
        });
    }
}

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
