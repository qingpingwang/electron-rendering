const fs = require('fs');
const path = require('path');
const { ipcRenderer } = require('electron');
const player = require('./state');
const { play, seek } = require('./player/controls');
const { loadFromConfig } = require('./player/loader');
const { updateUI } = require('./player/renderer');
const { log } = require('./utils/logger');
const { setupDivider } = require('./utils/divider');
const Inspector = require('./panel/inspector');
const { initToolHandler } = require('./agent/tool_handler');
const db = require('../db');
db.init();

const ROOT_DIR = path.join(__dirname, '..');

let _currentUUID = null;
let _currentConfigPath = null;

function init() {
    const canvas = document.getElementById('canvas');
    player.initCanvas(canvas);

    log('初始化...', 'info');

    try {
        player.addon = require(path.join(ROOT_DIR, 'build', 'Release', 'video_player'));
        player.root = player.addon.createRoot();
        player.root.init();

        log(`GPU: ${player.root.gpuInfo}`, 'ok');
        log(`Addon 加载成功 | GPU: ${player.root.gpuInfo}`, 'ok');

    } catch (e) {
        log(`Addon 加载失败: ${e.message}`, 'err');
        console.error(e);
        return;
    }

    player.initTimeline(document.getElementById('timeline'));

    const inspector = new Inspector(document.getElementById('inspector-content'));
    inspector.onChange = () => {
        if (player.timeline && player.timeline.onRefresh) {
            player.timeline.onRefresh();
        }
    };

    player.video.onRender = () => {
        updateUI();
        if (player.timeline) player.timeline.setCurrentTime(player.video.currentTime);
    };

    player.timeline.onSeek = (timeMs) => seek(timeMs / player.video.duration);
    player.timeline.onTrackMute = (groupId, muted) => player.audio.muteGroup(groupId, muted);
    player.timeline.onRefresh = () => {
        if (!player.audio.playing) player.video.render(player.video.currentTime, true, false);
    };
    player.timeline.onSelectLayer = (info) => inspector.update(info);

    document.getElementById('btn-play').onclick = play;

    initRightPanel();
    initDividers();

    document.onkeydown = e => {
        if (e.code === 'Space') { e.preventDefault(); play(); }
        else if (e.code === 'Escape') stop();
    };

    initToolHandler();
    _setupProjectIPC();

    updateUI();
    log('就绪', 'ok');
}

function _setupProjectIPC() {
    ipcRenderer.on('load-project', async (_event, { uuid, configPath }) => {
        _currentUUID = uuid;
        _currentConfigPath = configPath;

        const absPath = path.resolve(ROOT_DIR, configPath);
        try {
            const jsonStr = fs.readFileSync(absPath, 'utf-8');
            const config = JSON.parse(jsonStr);
            await loadFromConfig(config);
            log(`项目已加载: ${configPath}`, 'ok');
        } catch (e) {
            log(`项目加载失败: ${e.message}`, 'err');
            console.error(e);
        }
    });

    ipcRenderer.on('save-project', (_event) => {
        _saveCurrentProject();
        ipcRenderer.send('project-saved');
    });
}

function _saveCurrentProject() {
    if (!_currentUUID) return;

    const fields = { updatedAt: new Date().toISOString() };
    if (player.root && player.root.loaded && player.root.durationMs) {
        fields.duration = player.root.durationMs;
    }
    try { db.projects.update(_currentUUID, fields); } catch {}

    if (_currentConfigPath && player.root && player.root.loaded) {
        try {
            const configStr = player.root.exportConfig?.();
            if (configStr) {
                const absPath = path.resolve(ROOT_DIR, _currentConfigPath);
                fs.writeFileSync(absPath, configStr, 'utf-8');
            }
        } catch { /* exportConfig may not exist yet */ }
    }
}

function initRightPanel() {
    const section = document.getElementById('right-section');
    const fold = document.getElementById('panel-fold');
    const expand = document.getElementById('panel-expand');
    const divV = document.getElementById('divider-v');
    const logWrap = document.getElementById('log-wrap');

    function togglePanel() {
        const collapsed = section.classList.toggle('collapsed');
        divV.style.display = collapsed ? 'none' : '';
        expand.classList.toggle('visible', collapsed);
    }

    fold.onclick = togglePanel;
    expand.onclick = togglePanel;
    document.getElementById('log-bar').onclick = () => logWrap.classList.toggle('expanded');
}

function initDividers() {
    const playerSection = document.getElementById('player-section');
    const timeline = document.getElementById('timeline');
    const rightSection = document.getElementById('right-section');
    const divH = document.getElementById('divider-h');
    const divV = document.getElementById('divider-v');

    setupDivider(divH, {
        axis: 'y',
        onDrag(delta) {
            const total = playerSection.parentElement.offsetHeight - divH.offsetHeight;
            const newH = Math.max(120, Math.min(total - 60, playerSection.offsetHeight + delta));
            playerSection.style.flex = 'none';
            playerSection.style.height = `${newH}px`;
            timeline.style.height = `${total - newH}px`;
        }
    });

    setupDivider(divV, {
        axis: 'x',
        onDrag(delta) {
            rightSection.style.width = `${Math.max(180, rightSection.offsetWidth - delta)}px`;
        }
    });
}

window.onload = init;
window.onbeforeunload = () => {
    _saveCurrentProject();
    player.audio.dispose();
    player.root?.cleanup();
};
