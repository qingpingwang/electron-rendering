const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { ipcRenderer } = require('electron');
const toast = require('../renderer/utils/toast');
const db = require('../db');

const ROOT_DIR = path.join(__dirname, '..');

db.init();

const PRESETS = [
    { name: '横屏', width: 1920, height: 1080, iconClass: 'landscape' },
    { name: '竖屏', width: 1080, height: 1920, iconClass: 'portrait' },
    { name: '方形', width: 1080, height: 1080, iconClass: 'square' },
];

// ---- Rendering ----

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
    const section = document.getElementById('home-history-section');
    const list = document.getElementById('home-history-list');

    const items = db.projects.list();

    list.innerHTML = '';

    if (!items || items.length === 0) {
        section.style.display = 'none';
        return;
    }

    section.style.display = '';
    items.forEach(item => {
        const row = document.createElement('div');
        row.className = 'home-history-item';

        row.innerHTML = `
            <span class="home-history-label">${escapeHtml(item.name)}</span>
            <span class="home-history-duration">${formatDuration(item.duration)}</span>
            <span class="home-history-date">${formatDate(item.updatedAt)}</span>
        `;
        row.addEventListener('click', () => openProject(item));
        list.appendChild(row);
    });
}

// ---- Actions ----

function openProject(item) {
    const absPath = path.resolve(ROOT_DIR, item.configPath);
    if (!fs.existsSync(absPath)) {
        db.projects.remove(item.uuid);
        renderHistory();
        toast.show(`「${item.name}」工程文件不存在，已从历史记录中移除`, 'error');
        return;
    }
    ipcRenderer.send('open-project', {
        uuid: item.uuid,
        configPath: item.configPath,
    });
}

function createProject(width, height) {
    const uuid = crypto.randomUUID();
    const configPath = `test/project_${uuid}.json`;
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

    ipcRenderer.send('open-project', { uuid, configPath });
}

// ---- Helpers ----

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str || '';
    return div.innerHTML;
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

// ---- Init ----

ipcRenderer.on('refresh-history', () => {
    renderHistory();
});

renderPresets();
renderHistory();

document.getElementById('home-create').addEventListener('click', () => {
    const w = parseInt(document.getElementById('home-w').value, 10);
    const h = parseInt(document.getElementById('home-h').value, 10);
    if (!w || !h || w < 1 || h < 1) return;
    createProject(w, h);
});
