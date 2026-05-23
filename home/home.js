const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { ipcRenderer } = require('electron');
const { dialog, BrowserWindow } = require('@electron/remote');
const toast = require('../renderer/utils/toast');
const db = require('../db');

const ROOT_DIR = path.join(__dirname, '..');

db.init();

const PRESETS = [
    { name: '横屏', width: 1920, height: 1080, iconClass: 'landscape' },
    { name: '竖屏', width: 1080, height: 1920, iconClass: 'portrait' },
    { name: '方形', width: 1080, height: 1080, iconClass: 'square' },
];

// =====================================================================
// 视频工程
// =====================================================================

function renderPresets() {
    const container = document.getElementById('home-presets');
    container.innerHTML = '';

    PRESETS.forEach(p => {
        const el = document.createElement('div');
        el.className = 'home-preset';
        el.innerHTML = `
            <div class="home-preset-icon ${p.iconClass}"></div>
            <div class="home-preset-name">${p.name}</div>
            <div class="home-preset-size">${p.width}x${p.height}</div>
        `;
        el.addEventListener('click', () => createProject(p.width, p.height));
        container.appendChild(el);
    });
}

function renderHistory() {
    const items = db.projects.list();
    renderHistoryList('home-history-section', 'home-history-list', items, item => {
        const row = document.createElement('div');
        row.className = 'home-history-item';
        row.innerHTML = `
            <span class="home-history-label">${escapeHtml(item.name)}</span>
            <span class="home-history-duration">${formatDuration(item.duration)}</span>
            <span class="home-history-date">${formatDate(item.updatedAt)}</span>
        `;
        row.addEventListener('click', () => openProject(item));
        return row;
    });
}

function openProject(item) {
    const absPath = path.resolve(ROOT_DIR, item.configPath);
    if (!fs.existsSync(absPath)) {
        db.projects.remove(item.uuid);
        renderHistory();
        toast.show(`「${item.name}」工程文件不存在，已从历史记录中移除`, 'error');
        return;
    }
    ipcRenderer.send('open-project', {
        type: 'editor',
        uuid: item.uuid,
        configPath: item.configPath,
    });
}

function createProject(width, height) {
    const uuid = crypto.randomUUID();
    const configDir = path.resolve(ROOT_DIR, 'test_project');
    if (!fs.existsSync(configDir)) fs.mkdirSync(configDir, { recursive: true });

    const configPath = `test_project/${uuid}.json`;
    const absPath = path.resolve(ROOT_DIR, configPath);

    const orientLabel = width > height ? '横屏' : (height > width ? '竖屏' : '方形');
    const name = `${orientLabel} ${width}x${height}`;

    const config = {
        id: uuid,
        duration: 5000,
        fps: 30,
        canvas_config: {
            width,
            height,
            ratio: `${width}:${height}`,
        },
        tracks: [],
        materials: {},
    };

    fs.writeFileSync(absPath, JSON.stringify(config, null, 4), 'utf-8');

    db.projects.create({
        uuid,
        configPath,
        name,
        duration: config.duration,
    });

    ipcRenderer.send('open-project', { type: 'editor', uuid, configPath, isNew: true });
}

async function openProjectFromFile() {
    try {
        const win = BrowserWindow.getFocusedWindow();
        const result = await dialog.showOpenDialog(win || undefined, {
            title: '选择项目 JSON 文件',
            properties: ['openFile'],
            filters: [
                { name: 'JSON 文件', extensions: ['json'] },
                { name: '所有文件', extensions: ['*'] },
            ],
        });
        if (result.canceled || !result.filePaths?.length) return;

        const absPath = path.resolve(result.filePaths[0]);
        let config;
        try {
            const raw = fs.readFileSync(absPath, 'utf-8');
            config = JSON.parse(raw);
        } catch (e) {
            toast.show(`JSON 解析失败: ${e.message}`, 'error');
            return;
        }

        if (!config || typeof config !== 'object') {
            toast.show('配置文件格式无效', 'error');
            return;
        }

        const existing = db.projects.list().find((p) => path.resolve(ROOT_DIR, p.configPath) === absPath);
        if (existing) {
            openProject(existing);
            return;
        }

        const uuid = crypto.randomUUID();
        const name = path.basename(absPath, path.extname(absPath)) || '导入项目';
        const duration = Number(config.duration) > 0 ? Number(config.duration) : 5000;

        db.projects.create({
            uuid,
            configPath: absPath,
            name,
            duration,
        });

        ipcRenderer.send('open-project', { type: 'editor', uuid, configPath: absPath, isNew: true });
    } catch (e) {
        toast.show(`打开失败: ${e.message}`, 'error');
    }
}

// =====================================================================
// 渲染资源工程  —— 聊天室不绑定任何项目文件夹，通过 checkpointer 接口管理会话
// =====================================================================

async function renderResHistory() {
    const list = document.getElementById('res-history-list');
    list.innerHTML = '<div style="padding:8px;color:#888;font-size:12px;">加载中…</div>';
    document.getElementById('res-history-section').style.display = '';

    const agent = require('../agent');
    let threads = [];
    try {
        threads = await agent.listResourceThreads();
    } catch (e) {
        console.warn('[Home] listResourceThreads failed:', e.message);
    }

    renderHistoryList('res-history-section', 'res-history-list', threads, item => {
        const row = document.createElement('div');
        row.className = 'home-history-item';
        const label = truncate(item.title) || '（未开始对话）';
        row.innerHTML = `
            <span class="home-history-label">${escapeHtml(label)}</span>
            <span class="home-history-duration">${escapeHtml(item.uuid.substring(0, 8))}</span>
            <span class="home-history-date">${formatDate(item.updatedAt)}</span>
        `;
        row.addEventListener('click', () => openResourceProject(item));
        return row;
    });
}

function openResourceProject(item) {
    ipcRenderer.send('open-project', {
        type: 'resource',
        uuid: item.uuid,
        name: item.title || item.uuid,
    });
}

function createResourceProject() {
    const uuid = crypto.randomUUID();
    // 聊天室不创建任何文件夹，会话由 LangGraph checkpoint 持久化
    ipcRenderer.send('open-project', {
        type:  'resource',
        uuid,
        name:  '',
        isNew: true,
    });
}

// =====================================================================
// Tabs
// =====================================================================

function setupTabs() {
    const tabs = document.querySelectorAll('.home-tab');
    const panels = document.querySelectorAll('.home-tab-panel');
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const id = tab.dataset.tab;
            tabs.forEach(t => t.classList.toggle('active', t === tab));
            panels.forEach(p => p.classList.toggle('active', p.dataset.tabPanel === id));
        });
    });
}

// =====================================================================
// Helpers
// =====================================================================

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str || '';
    return div.innerHTML;
}

/** 超出 max 字符时截断并加省略号 */
function truncate(str, max = 52) {
    if (!str) return '';
    return str.length > max ? str.substring(0, max) + '…' : str;
}

function formatDuration(ms) {
    if (!ms || ms <= 0) return '0s';
    const totalSec = Math.round(ms / 1000);
    const m = Math.floor(totalSec / 60);
    const s = totalSec % 60;
    return m > 0 ? `${m}m${s}s` : `${s}s`;
}

function formatDate(isoStr) {
    try {
        const d = new Date(isoStr);
        const pad = n => String(n).padStart(2, '0');
        return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
    } catch {
        return '';
    }
}

/**
 * 通用历史列表渲染：填充 section/list 对应 DOM。
 * buildRow(item) → HTMLElement
 */
function renderHistoryList(sectionId, listId, items, buildRow) {
    const section = document.getElementById(sectionId);
    const list    = document.getElementById(listId);
    list.innerHTML = '';
    if (!items || items.length === 0) {
        section.style.display = 'none';
        return;
    }
    section.style.display = '';
    items.forEach(item => list.appendChild(buildRow(item)));
}

// =====================================================================
// Init
// =====================================================================

ipcRenderer.on('refresh-history', () => {
    renderHistory();
    renderResHistory();
});

setupTabs();
renderPresets();
renderHistory();
renderResHistory();

document.getElementById('home-create').addEventListener('click', () => {
    const w = parseInt(document.getElementById('home-w').value, 10);
    const h = parseInt(document.getElementById('home-h').value, 10);
    if (!w || !h || w < 1 || h < 1) return;
    createProject(w, h);
});

document.getElementById('home-open-file').addEventListener('click', () => {
    openProjectFromFile();
});

document.getElementById('res-create').addEventListener('click', () => {
    createResourceProject();
});
