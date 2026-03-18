const { BrowserWindow, BrowserView, ipcMain } = require('electron');
const path = require('path');

const TAB_BAR_HEIGHT = 36;

class WindowManager {
    constructor() {
        this.homeWindow = null;
        this.mainWindow = null;
        this.editorView = null;
        this.chatView = null;
        this.chatWindow = null;
        this.chatDetached = false;
        this.activeTab = 'editor';
        this._lastChatBounds = null;
        this._ipcSetup = false;
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
            this.homeWindow.removeAllListeners();
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
            minWidth: 640,
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
            },
        });
        this.mainWindow.addBrowserView(this.chatView);
        this.chatView.webContents.loadFile(path.join(__dirname, '..', 'chat', 'index.html'));
    }

    // ---- Layout ----

    _layoutViews() {
        if (!this.mainWindow || this.mainWindow.isDestroyed()) return;
        const [w, h] = this.mainWindow.getContentSize();

        if (this.chatDetached) {
            this.editorView.setBounds({ x: 0, y: 0, width: w, height: h });
        } else {
            const contentY = TAB_BAR_HEIGHT;
            const contentH = Math.max(0, h - TAB_BAR_HEIGHT);
            this.editorView.setBounds({ x: 0, y: contentY, width: w, height: contentH });
            this.chatView.setBounds({ x: 0, y: contentY, width: w, height: contentH });
        }
    }

    // ---- Tab / Detach IPC ----

    _setupIPC() {
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
        for (const view of [this.editorView, this.chatView]) {
            if (!view) continue;
            view.webContents.on('before-input-event', devToolsHandler);
        }
    }

    _toggleActiveDevTools() {
        const wc = this.activeTab === 'chat'
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
        if (this.chatDetached) return;

        this.activeTab = tabId;

        if (tabId === 'editor') {
            this.mainWindow.setTopBrowserView(this.editorView);
        } else {
            this.mainWindow.setTopBrowserView(this.chatView);
        }

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
        if (this.homeWindow && !this.homeWindow.isDestroyed()) {
            this.homeWindow.destroy();
        }
        this.homeWindow = null;
    }
}

module.exports = WindowManager;
