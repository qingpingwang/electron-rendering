const path = require('path');
const player = require('./state');
const { play, seek } = require('./player/controls');
const { loadTest } = require('./player/loader');
const { updateUI } = require('./player/renderer');
const { log } = require('./utils/logger');

const ROOT_DIR = path.join(__dirname, '..');

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

    player.video.onRender = () => {
        updateUI();
        if (player.timeline) player.timeline.setCurrentTime(player.video.currentTime);
    };

    player.timeline.onSeek = (timeMs) => {
        seek(timeMs / player.video.duration);
    };

    player.timeline.onTrackMute = (groupId, muted) => {
        player.audio.muteGroup(groupId, muted);
    };

    player.timeline.onRefresh = () => {
        if (!player.audio.playing) {
            player.video.render(player.video.currentTime, true);
        }
    };

    document.getElementById('btn-play').onclick = play;
    document.getElementById('btn-test').onclick = loadTest;

    const logSection = document.getElementById('log-section');
    const logFold = document.getElementById('log-fold');
    const logExpand = document.getElementById('log-expand');
    const divV = document.getElementById('divider-v');

    function toggleLog() {
        const collapsed = logSection.classList.toggle('collapsed');
        divV.style.display = collapsed ? 'none' : '';
        logExpand.classList.toggle('visible', collapsed);
    }

    logFold.onclick = toggleLog;
    logExpand.onclick = toggleLog;

    initDividers();

    document.onkeydown = e => {
        if (e.code === 'Space') { e.preventDefault(); play(); }
        else if (e.code === 'Escape') stop();
    };

    updateUI();
    log('就绪', 'ok');
}

function initDividers() {
    const playerSection = document.getElementById('player-section');
    const timeline = document.getElementById('timeline');
    const logSection = document.getElementById('log-section');
    const divH = document.getElementById('divider-h');
    const divV = document.getElementById('divider-v');

    setupDivider(divH, {
        axis: 'y',
        onDrag(delta) {
            const parent = playerSection.parentElement;
            const total = parent.offsetHeight - divH.offsetHeight;
            const curPlayerH = playerSection.offsetHeight;
            const curTlH = timeline.offsetHeight;
            const newPlayerH = Math.max(120, Math.min(total - 60, curPlayerH + delta));
            const newTlH = total - newPlayerH;
            playerSection.style.flex = 'none';
            playerSection.style.height = `${newPlayerH}px`;
            timeline.style.height = `${newTlH}px`;
        }
    });

    setupDivider(divV, {
        axis: 'x',
        onDrag(delta) {
            const newW = Math.max(180, logSection.offsetWidth - delta);
            logSection.style.width = `${newW}px`;
        }
    });
}

function setupDivider(el, { axis, onDrag }) {
    let startPos = 0;
    const cls = axis === 'y' ? 'resizing-h' : 'resizing-v';

    el.addEventListener('mousedown', e => {
        e.preventDefault();
        startPos = axis === 'y' ? e.clientY : e.clientX;
        el.classList.add('active');
        document.body.classList.add(cls);

        const onMove = (e) => {
            const cur = axis === 'y' ? e.clientY : e.clientX;
            const delta = cur - startPos;
            startPos = cur;
            onDrag(delta);
        };

        const onUp = () => {
            el.classList.remove('active');
            document.body.classList.remove(cls);
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
        };

        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
    });
}

window.onload = init;
window.onbeforeunload = () => {
    player.audio.dispose();
    player.root?.cleanup();
};
