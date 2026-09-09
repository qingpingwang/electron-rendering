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
const CanvasTransform = require('./editor/canvas_transform');
const MediaLibrary = require('./editor/media_library');
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
        player.addon = require(path.join(ROOT_DIR, '..', 'build', 'Release', 'video_player'));
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

    const undo = [], redo = [];
    const transform = new CanvasTransform(canvas, (layer, before, after) => {
        if (JSON.stringify(before) !== JSON.stringify(after)) {
            undo.push({ layer, before, after }); redo.length = 0;
        }
        inspector.update(inspector.getCurrentInfo());
    });
    player.canvasTransform = transform;
    document.addEventListener('pointerdown', e => {
        if (e.button !== 0 || transform.drag) { return; }
        // 编辑控件、素材操作和变换手柄需要保留当前图层。
        const keepSelection = '#canvas, .transform-overlay, .tl-segment, .tl-label, #right-section, .pcr-app, .media-card, .resource-tabs, .resource-sidebar, .controls, .tl-zoom-bar, .divider-h, .divider-v, button, input, select, textarea, [contenteditable="true"]';
        if (e.target.closest(keepSelection)) { return; }
        player.timeline._deselectSegment();
    });
    inspector.onChange = () => {
        undo.length = 0; redo.length = 0;
        player.timeline.refresh();
        player.timeline.onRefresh?.();
    };
    player.mediaLibrary = new MediaLibrary();
    player.beforeProjectLoad = () => {
        transform.select(null); inspector.clear(); undo.length = 0; redo.length = 0;
    };
    const history = forward => {
        transform.cancel();
        const item = (forward ? redo : undo).pop();
        if (!item) { return; }
        Object.assign(item.layer, forward ? item.after : item.before);
        (forward ? undo : redo).push(item);
        player.video.render(player.video.currentTime, true, false);
        inspector.update(inspector.getCurrentInfo());
    };
    const saveProject = async () => {
        if (!player.root?.loaded) { return; }
        if (!_currentConfigPath) {
            const { dialog } = require('@electron/remote');
            const result = await dialog.showSaveDialog({ title: '保存项目', defaultPath: 'project.json', filters: [{ name: 'NLE 项目', extensions: ['json'] }] });
            if (result.canceled) { return; }
            _currentConfigPath = result.filePath;
        }
        try { _saveCurrentProject(); log('项目已保存', 'ok'); }
        catch (e) { log(`保存失败：${e.message}`, 'err'); }
    };

    player.video.onRender = () => {
        updateUI();
        transform.refresh();
        if (player.timeline) player.timeline.setCurrentTime(player.video.currentTime);
    };

    player.timeline.onSeek = (timeMs) => seek(timeMs / player.video.duration);
    player.timeline.onTrackMute = (groupId, muted) => player.audio.muteGroup(groupId, muted);
    player.timeline.onRefresh = () => {
        if (!player.audio.playing) player.video.render(player.video.currentTime, true, false);
    };
    player.timeline.onSelectLayer = info => { inspector.update(info); transform.select(info); };
    player.timeline.onMoveSegment = async (trackId, id, time, targetTrackId = trackId) => {
        try {
            const config = JSON.parse(player.root.exportConfig());
            const track = config.tracks.find(t => t.id === trackId);
            const target = config.tracks.find(t => t.id === targetTrackId);
            if (!track || !target || track.type !== target.type) { throw new Error('只能移入相同类型的轨道'); }
            const segment = track.segments.find(s => s.id === id);
            if (!segment) { throw new Error('片段不存在'); }
            const duration = segment.target_timerange.duration;
            const others = target.segments.filter(s => s.id !== id).sort((a, b) => a.target_timerange.start - b.target_timerange.start);
            let start = Math.max(0, Math.round(time * config.fps / 1000) * 1000 / config.fps);
            start = Math.round(start);
            // 同一轨道的片段不覆盖；落点有冲突时顺延到可用空位。
            for (const other of others) {
                const range = other.target_timerange;
                if (start < range.start + range.duration && start + duration > range.start) {
                    start = range.start + range.duration;
                }
            }
            segment.target_timerange.start = start;
            track.segments = track.segments.filter(s => s.id !== id);
            target.segments.push(segment);
            target.segments.sort((a, b) => a.target_timerange.start - b.target_timerange.start);
            config.tracks = config.tracks.filter(t => t.segments.length > 0);
            config.duration = Math.max(0, ...config.tracks.flatMap(t => t.segments.map(s => s.target_timerange.start + s.target_timerange.duration)));
            await player.mediaLibrary.apply(config, id, segment.target_timerange.start);
        } catch (e) { log(`移动失败：${e.message}`, 'err'); player.timeline.refresh(); }
    };

    /** 工具 / 脚本直接改 layer 后同步时间轴与检查器 */
    player.notifyUiAfterLayerChange = () => {
        updateUI();
        if (player.timeline) player.timeline.refresh();
        const info = inspector.getCurrentInfo();
        if (info) inspector.update(info);
    };

    document.getElementById('btn-play').onclick = play;

    initRightPanel();
    initDividers();

    document.onkeydown = e => {
        if ((e.metaKey || e.ctrlKey) && e.code === 'KeyS') { e.preventDefault(); saveProject().catch(err => log(`保存失败：${err.message}`, 'err')); return; }
        if (e.target.closest('input, textarea, [contenteditable="true"]')) { return; }
        if ((e.metaKey || e.ctrlKey) && e.code === 'KeyZ') { e.preventDefault(); history(e.shiftKey); }
        else if (e.code === 'Space') { e.preventDefault(); play(); }
        else if (e.code === 'Escape') { if (transform.drag) { transform.cancel(); } else { player.timeline._deselectSegment(); } }
    };

    initToolHandler();
    _setupProjectIPC();

    updateUI();
    log('就绪', 'ok');
}

function _setupProjectIPC() {
    ipcRenderer.on('load-project', async (_event, { uuid, configPath }) => {
        player.mediaLibrary.items.clear();
        _currentUUID = uuid;
        _currentConfigPath = configPath;

        const absPath = path.resolve(ROOT_DIR, configPath);
        try {
            const jsonStr = fs.readFileSync(absPath, 'utf-8');
            const config = JSON.parse(jsonStr);
            // 协议所在目录作为 base_path，供 C++ 解析/拼接资源路径
            const protocolPath = path.dirname(absPath);
            await loadFromConfig(config, protocolPath);
            log(`项目已加载: ${configPath}`, 'ok');
        } catch (e) {
            log(`项目加载失败: ${e.message}`, 'err');
            console.error(e);
        }
    });

}

function _saveCurrentProject() {
    const fields = { updatedAt: new Date().toISOString() };
    if (player.root && player.root.loaded && player.root.durationMs) {
        fields.duration = player.root.durationMs;
    }
    if (_currentUUID) { db.projects.update(_currentUUID, fields); }

    if (_currentConfigPath && player.root && player.root.loaded) {
        const configStr = player.root.exportConfig();
        if (configStr) {
            const absPath = path.resolve(ROOT_DIR, _currentConfigPath);
            fs.writeFileSync(absPath, configStr, 'utf-8');
        }
    }
}

function initRightPanel() {
    const section = document.getElementById('right-section');
    const fold = document.getElementById('panel-fold');
    const expand = document.getElementById('panel-expand');
    const divV = document.getElementById('divider-v');

    function togglePanel() {
        const collapsed = section.classList.toggle('collapsed');
        document.body.classList.toggle('inspector-collapsed', collapsed);
        expand.classList.toggle('visible', collapsed);
    }

    fold.onclick = togglePanel;
    expand.onclick = togglePanel;
}

function initDividers() {
    const playerSection = document.getElementById('player-section');
    const timeline = document.getElementById('timeline');
    const rightSection = document.getElementById('right-section');
    const divH = document.getElementById('divider-h');
    const divV = document.getElementById('divider-v');

    const media = document.querySelector('.media-panel');
    const style = document.body.style;
    const minimum = name => parseFloat(getComputedStyle(document.documentElement).getPropertyValue(name));
    setupDivider(divH, {
        axis: 'y',
        onDrag(delta) {
            const total = playerSection.offsetHeight + timeline.offsetHeight;
            const upper = Math.max(210, Math.min(total - 160, playerSection.offsetHeight + delta));
            style.setProperty('--upper-size', `${upper}fr`);
            style.setProperty('--timeline-size', `${total - upper}fr`);
        }
    });
    const resizeColumns = (left, right, leftKey, rightKey, leftMin, rightMin) => {
        return delta => {
            const total = left.offsetWidth + right.offsetWidth;
            const width = Math.max(minimum(leftMin), Math.min(total - minimum(rightMin), left.offsetWidth + delta));
            const widths = { media: media.offsetWidth, viewer: playerSection.offsetWidth, inspector: rightSection.offsetWidth };
            widths[leftKey] = width;
            widths[rightKey] = total - width;
            for (const [key, value] of Object.entries(widths)) {
                if (value > 0) {
                    style.setProperty(`--${key}-size`, `${value}fr`);
                }
            }
        };
    };
    setupDivider(document.getElementById('divider-media'), {
        axis: 'x',
        onDrag: resizeColumns(media, playerSection, 'media', 'viewer', '--media-min', '--viewer-min')
    });
    setupDivider(divV, {
        axis: 'x',
        onDrag: resizeColumns(playerSection, rightSection, 'viewer', 'inspector', '--viewer-min', '--inspector-min')
    });
}

window.onload = init;
window.onbeforeunload = () => {
    try { _saveCurrentProject(); } catch (e) { console.error(e); }
    player.audio.dispose();
    player.root?.cleanup();
};
