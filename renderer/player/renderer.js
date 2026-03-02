const player = require('../state');
const { log, formatTime } = require('../utils/logger');

function updateUI() {
    const $ = id => document.getElementById(id);

    $('btn-play').disabled = player.duration === 0;
    $('btn-stop').disabled = player.duration === 0;
    $('btn-play').textContent = player.audio.playing ? '⏸ 暂停' : '▶ 播放';

    $('time-current').textContent = formatTime(player.currentTime);
    $('time-total').textContent = formatTime(player.duration);

    const progress = player.duration > 0 ? (player.currentTime / player.duration) * 100 : 0;
    $('progress-fill').style.width = `${progress}%`;

    if (player.width > 0) {
        $('video-info').textContent = `${player.width}×${player.height} | ${player.frameRate.toFixed(1)}fps | 帧: #${player.frameCount}`;
    }
}

function snapToFrame(timeMs) {
    if (player.frameRate <= 0) return Math.floor(timeMs);
    const frameMs = 1000 / player.frameRate;
    return Math.round(timeMs / frameMs) * frameMs;
}

function render(timeMs) {
    if (!player.root) return;
    if (timeMs !== undefined) player.currentTime = timeMs;

    const t0 = performance.now();

    player.root.setCurrentTime(snapToFrame(player.currentTime));
    const result = player.root.draw();

    const t1 = performance.now();

    if (!result) return;

    const { pixels, status } = result;

    if (player.canvas.width !== player.width || player.canvas.height !== player.height) {
        player.canvas.width = player.width;
        player.canvas.height = player.height;
        log(`Canvas 调整为 ${player.width}×${player.height}`, 'info');
    }

    const imageData = new ImageData(
        new Uint8ClampedArray(pixels.buffer, pixels.byteOffset, pixels.length),
        player.width,
        player.height
    );
    player.ctx.putImageData(imageData, 0, 0);

    player.frameCount++;

    const totalTime = performance.now() - t0;
    const renderTime = t1 - t0;
    const tag = status === 0 ? '缓存' : '渲染';
    log(`#${player.frameCount} | ${formatTime(player.currentTime)} | ${tag} ${renderTime.toFixed(2)}ms | 总: ${totalTime.toFixed(2)}ms`, 'info');

    updateUI();
}

function startRenderLoop() {
    stopRenderLoop();

    const tick = () => {
        if (!player.audio.playing) {
            player.animationId = null;
            return;
        }

        const needStop = player.audio.currentTimeMs >= player.duration;
        const t = needStop ? player.duration - 1 : player.audio.currentTimeMs;
        if (!player.root.isSameFrame(snapToFrame(t))) {
            render(t);
        }
        if (needStop) {
            player.audio.stop();
            log('播放结束', 'ok');
            updateUI();
            player.animationId = null;
            return;
        }
        player.animationId = requestAnimationFrame(tick);
    };
    player.animationId = requestAnimationFrame(tick);
}

function stopRenderLoop() {
    if (player.animationId) {
        cancelAnimationFrame(player.animationId);
        player.animationId = null;
    }
}

module.exports = { render, updateUI, startRenderLoop, stopRenderLoop };
