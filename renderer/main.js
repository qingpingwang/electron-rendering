const path = require('path');
const player = require('./state');
const { play, stop, seek } = require('./player/controls');
const { loadVideo, loadTest } = require('./player/loader');
const { updateUI } = require('./player/renderer');
const { log } = require('./utils/logger');

const ROOT_DIR = path.join(__dirname, '..');

function init() {
    player.canvas = document.getElementById('canvas');
    player.ctx = player.canvas.getContext('2d');

    log('初始化...', 'info');

    try {
        player.addon = require(path.join(ROOT_DIR, 'build', 'Release', 'video_player'));
        player.root = player.addon.createRoot();
        player.root.init();

        document.getElementById('gpu-info').textContent = `GPU: ${player.root.gpuInfo}`;
        log(`✓ Addon 加载成功 | GPU: ${player.root.gpuInfo}`, 'ok');

    } catch (e) {
        log(`✗ Addon 加载失败: ${e.message}`, 'err');
        console.error(e);
        return;
    }

    document.getElementById('btn-play').onclick = play;
    document.getElementById('btn-stop').onclick = stop;
    document.getElementById('btn-load').onclick = loadVideo;
    document.getElementById('btn-test').onclick = loadTest;

    const progress = document.getElementById('progress');
    let dragging = false;

    progress.onmousedown = e => {
        dragging = true;
        const rect = progress.getBoundingClientRect();
        seek((e.clientX - rect.left) / rect.width);
    };

    document.onmousemove = e => {
        if (!dragging) return;
        const rect = progress.getBoundingClientRect();
        seek((e.clientX - rect.left) / rect.width);
    };

    document.onmouseup = () => { dragging = false; };

    document.onkeydown = e => {
        if (e.code === 'Space') { e.preventDefault(); play(); }
        else if (e.code === 'Escape') stop();
    };

    updateUI();
    log('就绪', 'ok');
}

window.onload = init;
window.onbeforeunload = () => {
    player.audio.dispose();
    player.root?.cleanup();
};
