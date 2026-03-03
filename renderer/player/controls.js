const player = require('../state');
const { updateUI } = require('./renderer');
const { log } = require('../utils/logger');

async function play() {
    if (!player.video || player.video.duration === 0) return;

    if (player.audio.playing) {
        player.audio.pause();
        player.video.stopRenderLoop();
        log('暂停', 'info');
        updateUI();
    } else {
        await player.audio.play();
        player.video.startRenderLoop(() => {
            if (!player.audio.playing) return null;
            const t = player.audio.currentTimeMs;
            if (t >= player.video.duration) {
                player.audio.stop();
                player.video.render(player.video.duration - 1);
                log('播放结束', 'ok');
                return null;
            }
            return t;
        });
        log(`播放 (音轨: ${player.audio.tracks.size}, ctx: ${player.audio.ctx.state})`, 'ok');
    }
}

function stop() {
    player.audio.stop();
    if (player.video) {
        player.video.stopRenderLoop();
        player.video.render(0);
    }
    log('停止', 'info');
}

function seek(ratio) {
    if (!player.video || player.video.duration === 0) return;
    const timeMs = player.video.duration * Math.max(0, Math.min(1, ratio));
    player.audio.seek(timeMs);
    player.video.render(timeMs);
}

module.exports = { play, stop, seek };
