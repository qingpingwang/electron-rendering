const player = require('../state');
const { render, updateUI, startRenderLoop, stopRenderLoop } = require('./renderer');
const { log } = require('../utils/logger');

async function play() {
    if (player.duration === 0) return;

    if (player.audio.playing) {
        player.audio.pause();
        stopRenderLoop();
        log('暂停', 'info');
    } else {
        await player.audio.play();
        startRenderLoop();
        log(`播放 (音轨: ${player.audio.tracks.size}, ctx: ${player.audio.ctx.state})`, 'ok');
    }
    updateUI();
}

function stop() {
    player.audio.stop();
    stopRenderLoop();
    player.currentTime = 0;
    player.frameCount = 0;
    render(0);
    log('停止', 'info');
}

function seek(ratio) {
    if (player.duration === 0) return;
    const timeMs = player.duration * Math.max(0, Math.min(1, ratio));
    player.audio.seek(timeMs);
    render(timeMs);
}

module.exports = { play, stop, seek };
