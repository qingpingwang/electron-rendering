const { app, BrowserWindow, BrowserView, ipcMain } = require('electron');
const path = require('path');

const TAB_BAR_HEIGHT = 36;

class WindowManager {
    constructor() {
        this.homeWindow = null;
        this.mainWindow = null;
        this.editorView = null;
        this.chatView = null;
        this.logView = null;
        this.logs = [];
        this.chatWindow = null;
        this.chatDetached = false;
        this.activeTab = 'editor';
        this._lastChatBounds = null;
        this._ipcSetup = false;
        this._suppressHomeQuit = false;

        // 渲染资源工程窗口（仅 chat，无 editor / tab-bar）
        this.resourceWindow = null;
    }

    // ---- Home Window ----

    createHomeWindow() {
        this.homeWindow = new BrowserWindow({
            width: 720,
            height: 560,
            minWidth: 480,
            minHeight: 400,
            show: false,
            backgroundColor: '#1a1a2e',
            titleBarStyle: 'hiddenInset',
            trafficLightPosition: { x: 12, y: 10 },
            webPreferences: {
                nodeIntegration: true,
                contextIsolation: false,
            },
        });

        this.homeWindow.loadFile(path.join(__dirname, '..', 'home', 'index.html'));
        try {
            require('@electron/remote/main').enable(this.homeWindow.webContents);
        } catch (_) {}
        this.homeWindow.on('closed', () => {
            this.homeWindow = null;
            // 关闭主页窗口时直接退出应用；但切换到主编辑器时不触发。
            if (!this._suppressHomeQuit && !this.mainWindow && !this.chatWindow) {
                app.quit();
            }
            this._suppressHomeQuit = false;
        });
        this.homeWindow.webContents.on('before-input-event', (_event, input) => {
            if (input.type === 'keyDown' && input.key === 'F12') {
                const wc = this.homeWindow?.webContents;
                if (!wc) return;
                if (wc.isDevToolsOpened()) wc.closeDevTools();
                else wc.openDevTools({ mode: 'detach' });
            }
        });
        this.homeWindow.once('ready-to-show', () => this.homeWindow.show());
        return this.homeWindow;
    }

    closeHome() {
        if (this.homeWindow && !this.homeWindow.isDestroyed()) {
            this._suppressHomeQuit = true;
            this.homeWindow.destroy();
        }
        this.homeWindow = null;
    }

    showHome() {
        if (this.homeWindow && !this.homeWindow.isDestroyed()) {
            this.homeWindow.webContents.send('refresh-history');
            this.homeWindow.show();
            return;
        }
        this.createHomeWindow();
    }

    // ---- Editor + Chat Window ----

    createMainWindow() {
        this.mainWindow = new BrowserWindow({
            width: 1200,
            height: 800,
            minWidth: 780,
            minHeight: 480,
            show: false,
            backgroundColor: '#1a1a2e',
            titleBarStyle: 'hiddenInset',
            trafficLightPosition: { x: 12, y: 10 },
            webPreferences: {
                nodeIntegration: true,
                contextIsolation: false,
            },
        });

        this.mainWindow.loadFile(path.join(__dirname, 'tab-bar.html'));

        this._createEditorView();
        this._createChatView();
        this._createLogView();

        if (!this._ipcSetup) {
            this._setupIPC();
            this._ipcSetup = true;
        }

        this.mainWindow.on('resize', () => this._layoutViews());
        this.mainWindow.webContents.on('did-finish-load', () => {
            this._layoutViews();
            this._notifyTabBar();
        });

        this.mainWindow.once('ready-to-show', () => this.mainWindow.show());

        this.switchTab('editor');

        return this.mainWindow;
    }

    closeEditor() {
        if (this.chatWindow && !this.chatWindow.isDestroyed()) {
            this.chatWindow.removeAllListeners();
            this.chatWindow.destroy();
        }
        this.chatWindow = null;
        this.chatDetached = false;

        if (this.mainWindow && !this.mainWindow.isDestroyed()) {
            this.mainWindow.removeAllListeners();
            this.mainWindow.destroy();
        }
        if (this.logView && !this.logView.webContents.isDestroyed()) { this.logView.webContents.close(); }
        this.logView = null;
        this.logs = [];
        this.mainWindow = null;
        this.editorView = null;
        this.chatView = null;
    }

    // ---- Editor BrowserView ----

    _createEditorView() {
        this.editorView = new BrowserView({
            webPreferences: {
                nodeIntegration: true,
                contextIsolation: false,
                enableRemoteModule: true,
            },
        });
        this.mainWindow.addBrowserView(this.editorView);
        this.editorView.webContents.loadFile(path.join(__dirname, '..', 'index.html'));

        try {
            require('@electron/remote/main').enable(this.editorView.webContents);
        } catch (_) {}
    }

    // ---- Chat BrowserView ----

    _createChatView() {
        this.chatView = new BrowserView({
            webPreferences: {
                nodeIntegration: true,
                contextIsolation: false,
                enableRemoteModule: true,
            },
        });
        this.mainWindow.addBrowserView(this.chatView);
        this.chatView.webContents.loadFile(path.join(__dirname, '..', 'chat', 'index.html'));

        try {
            require('@electron/remote/main').enable(this.chatView.webContents);
        } catch (_) {}
    }

    _createLogView() {
        this.logView = new BrowserView({ webPreferences: { nodeIntegration: true, contextIsolation: false } });
        this.mainWindow.addBrowserView(this.logView);
        this.logView.webContents.loadFile(path.join(__dirname, 'logs.html'));
    }

    // ---- Layout ----

    _layoutViews() {
        if (!this.mainWindow || this.mainWindow.isDestroyed()) return;
        const [w, h] = this.mainWindow.getContentSize();

        const bounds = { x: 0, y: TAB_BAR_HEIGHT, width: w, height: Math.max(0, h - TAB_BAR_HEIGHT) };
        this.editorView.setBounds(bounds);
        this.logView.setBounds(bounds);
        if (!this.chatDetached) {
            this.chatView.setBounds(bounds);
        }
    }

    // ---- Tab / Detach IPC ----

    _setupIPC() {
        ipcMain.on('editor-log', (event, entry) => {
            if (event.sender !== this.editorView?.webContents) { return; }
            const row = { time: entry.time, type: entry.type, message: String(entry.message) };
            this.logs.push(row);
            if (this.logs.length > 1000) { this.logs.shift(); }
            if (this.logView && !this.logView.webContents.isDestroyed()) {
                this.logView.webContents.send('logs-append', row);
            }
        });
        ipcMain.on('logs-subscribe', event => {
            if (event.sender === this.logView?.webContents) { event.reply('logs-snapshot', this.logs); }
        });
        ipcMain.on('logs-clear', event => {
            if (event.sender !== this.logView?.webContents) { return; }
            this.logs = [];
            event.reply('logs-snapshot', this.logs);
        });
        ipcMain.on('tab-switch', (_event, tabId) => {
            this.switchTab(tabId);
        });

        ipcMain.on('tab-detach-chat', () => {
            this.detachChat();
        });

        const devToolsHandler = (_event, input) => {
            if (input.type === 'keyDown' && input.key === 'F12') {
                this._toggleActiveDevTools();
            }
        };

        this.mainWindow.webContents.on('before-input-event', devToolsHandler);
        for (const view of [this.editorView, this.chatView, this.logView]) {
            if (!view) continue;
            view.webContents.on('before-input-event', devToolsHandler);
        }
    }

    _toggleActiveDevTools() {
        const wc = this.activeTab === 'logs' ? this.logView?.webContents : this.activeTab === 'chat'
            ? this.chatView?.webContents
            : this.editorView?.webContents;
        if (!wc) return;

        if (wc.isDevToolsOpened()) {
            wc.closeDevTools();
        } else {
            wc.openDevTools({ mode: 'detach' });
        }
    }

    switchTab(tabId) {
        const view = { editor: this.editorView, chat: this.chatView, logs: this.logView }[tabId];
        if (!view || (tabId === 'chat' && this.chatDetached)) { return; }
        this.activeTab = tabId;
        this.mainWindow.setTopBrowserView(view);
        view.webContents.focus();

        this._notifyTabBar();
    }

    detachChat() {
        if (this.chatDetached) {
            this.chatWindow?.focus();
            return;
        }

        const mainBounds = this.mainWindow.getBounds();
        const bounds = this._lastChatBounds || {
            width: mainBounds.width,
            height: mainBounds.height,
            x: mainBounds.x + 30,
            y: mainBounds.y + 30,
        };

        this.mainWindow.removeBrowserView(this.chatView);
        this.chatDetached = true;
        this.activeTab = 'editor';
        this.mainWindow.setTopBrowserView(this.editorView);

        this._layoutViews();
        this._notifyTabBar();

        this.chatWindow = new BrowserWindow({
            width: bounds.width,
            height: bounds.height,
            x: bounds.x,
            y: bounds.y,
            minWidth: 360,
            minHeight: 400,
            show: false,
            backgroundColor: '#1a1a2e',
            title: 'AI 聊天',
            webPreferences: { nodeIntegration: true, contextIsolation: false },
        });

        this.chatWindow.addBrowserView(this.chatView);
        const [cw, ch] = this.chatWindow.getContentSize();
        this.chatView.setBounds({ x: 0, y: 0, width: cw, height: ch });
        this.chatWindow.show();

        this.chatWindow.on('resize', () => {
            if (this.chatWindow && !this.chatWindow.isDestroyed()) {
                const [rw, rh] = this.chatWindow.getContentSize();
                this.chatView.setBounds({ x: 0, y: 0, width: rw, height: rh });
            }
        });

        this.chatWindow.on('close', (e) => {
            e.preventDefault();
            this.reattachChat();
        });
    }

    reattachChat() {
        if (!this.chatDetached) return;

        if (this.chatWindow && !this.chatWindow.isDestroyed()) {
            this._lastChatBounds = this.chatWindow.getBounds();
            this.chatWindow.removeBrowserView(this.chatView);
            this.chatWindow.removeAllListeners('close');
            this.chatWindow.removeAllListeners('resize');
            this.chatWindow.destroy();
        }
        this.chatWindow = null;
        this.chatDetached = false;

        this.mainWindow.addBrowserView(this.chatView);
        this.activeTab = 'editor';
        this.mainWindow.setTopBrowserView(this.editorView);

        this._layoutViews();
        this.mainWindow.setTopBrowserView(this.editorView);
        this._notifyTabBar();
    }

    _notifyTabBar() {
        if (!this.mainWindow || this.mainWindow.isDestroyed()) return;
        this.mainWindow.webContents.send('tab-state', {
            activeTab: this.activeTab,
            chatDetached: this.chatDetached,
        });
    }

    // ---- Accessors ----

    getEditorWebContents() {
        return this.editorView?.webContents;
    }

    getChatWebContents() {
        return this.chatView?.webContents;
    }

    // ---- Resource Project Window (chat-only) ----

    showResourceWindow() {
        if (this.resourceWindow && !this.resourceWindow.isDestroyed()) {
            this.resourceWindow.show();
            this.resourceWindow.focus();
            return this.resourceWindow;
        }

        this.resourceWindow = new BrowserWindow({
            width: 1400,
            height: 840,
            minWidth: 900,
            minHeight: 560,
            show: false,
            backgroundColor: '#0f0f1a',
            title: '渲染资源助手',
            titleBarStyle: 'hiddenInset',
            trafficLightPosition: { x: 12, y: 10 },
            webPreferences: {
                nodeIntegration: true,
                contextIsolation: false,
                enableRemoteModule: true,
            },
        });
        this.resourceWindow.loadFile(path.join(__dirname, '..', 'resource_ui', 'index.html'));
        try {
            require('@electron/remote/main').enable(this.resourceWindow.webContents);
        } catch (_) {}


        this.resourceWindow.webContents.on('before-input-event', (_event, input) => {
            if (input.type === 'keyDown' && input.key === 'F12') {
                const wc = this.resourceWindow?.webContents;
                if (!wc) return;
                if (wc.isDevToolsOpened()) wc.closeDevTools();
                else wc.openDevTools({ mode: 'detach' });
            }
        });

        this.resourceWindow.once('ready-to-show', () => this.resourceWindow.show());
        this.resourceWindow.on('closed', () => {
            this.resourceWindow = null;
            if (!this.mainWindow && !this.homeWindow && !this.chatWindow) {
                this.showHome();
            }
        });
        return this.resourceWindow;
    }

    closeResource() {
        if (this.resourceWindow && !this.resourceWindow.isDestroyed()) {
            this.resourceWindow.removeAllListeners();
            this.resourceWindow.destroy();
        }
        this.resourceWindow = null;
    }

    getResourceChatWebContents() {
        return this.resourceWindow?.webContents;
    }

    destroy() {
        if (this.chatWindow && !this.chatWindow.isDestroyed()) {
            this.chatWindow.removeAllListeners();
            this.chatWindow.destroy();
        }
        this.chatWindow = null;
        if (this.mainWindow && !this.mainWindow.isDestroyed()) {
            this.mainWindow.destroy();
        }
        this.mainWindow = null;
        if (this.resourceWindow && !this.resourceWindow.isDestroyed()) {
            this.resourceWindow.removeAllListeners();
            this.resourceWindow.destroy();
        }
        this.resourceWindow = null;
        if (this.homeWindow && !this.homeWindow.isDestroyed()) {
            this.homeWindow.destroy();
        }
        this.homeWindow = null;
    }
}

module.exports = WindowManager;
