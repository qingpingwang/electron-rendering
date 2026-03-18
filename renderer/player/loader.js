const path = require('path');
const player = require('../state');
const { updateUI } = require('./renderer');
const { stop } = require('./controls');
const { log, formatTime } = require('../utils/logger');

async function loadFromConfig(config) {
    document.getElementById('file-info').textContent = '加载中...';

    try {
        stop();

        const jsonStr = JSON.stringify(config);
        log(`加载配置: ${config.tracks?.length || 0} 轨道`, 'info');

        const t0 = performance.now();
        const result = player.root.load(jsonStr);
        if (!result.success) {
            throw new Error(`C++ 加载失败: ${result.error}`);
        }
        const t1 = performance.now();

        player.video.load(player.root);

        const groups = player.root.getGroups();
        if (player.timeline) player.timeline.load(config, groups);

        document.getElementById('file-info').textContent = '已加载';

        log(`✓ 加载成功 (${(t1-t0).toFixed(1)}ms) | ID: ${player.root.id || '-'} | ${player.video.width}×${player.video.height} | ${player.video.frameRate.toFixed(2)}fps | ${formatTime(player.video.duration)}`, 'ok');
        log(`轨道组: ${groups.length}`, 'info');
        groups.forEach((g, gi) => {
            log(`  [${gi}] "${g.id}" | ${g.type}`, 'info');
            g.layers.forEach((layer, i) => {
                let info = `    [${i}] "${layer.name}" | ${formatTime(layer.startTime)}~${formatTime(layer.endTime)}`;
                if (g.type === 'text') info += ` | text="${layer.text}"`;
                if (g.type === 'video' && layer.videoFrameRate) info += ` | ${layer.videoFrameRate.toFixed(1)}fps`;
                log(info, 'info');
            });
        });

        try {
            const audioInfos = player.root.getAudioInfos();
            const infoKeys = Object.keys(audioInfos);
            log(`音频信息: ${infoKeys.length} 条 [${infoKeys.join(', ')}]`, 'info');
            for (const [k, v] of Object.entries(audioInfos)) {
                log(`  ${k}: vol=${v.volume} path=${v.path} type=${v.layerType}`, 'info');
            }

            const audioCount = await player.audio.load(player.root);
            log(`✓ 音频解码: ${audioCount}/${infoKeys.length} 条成功 | ctx=${player.audio.ctx.state}`, audioCount > 0 ? 'ok' : 'warn');

            for (const [id, track] of player.audio.tracks) {
                log(`  ${id}: ${track.buffer.duration.toFixed(2)}s ${track.buffer.numberOfChannels}ch ${track.buffer.sampleRate}Hz`, 'info');
            }
        } catch (e) {
            log(`⚠ 音频加载失败: ${e.message}`, 'warn');
        }

        player.video.render(0);
        updateUI();

    } catch (e) {
        document.getElementById('file-info').textContent = '错误';
        log(`✗ ${e.message}`, 'err');
        console.error(e);
    }
}

async function loadVideo() {
    let filePath = null;

    try {
        const { dialog } = require('@electron/remote');
        const result = await dialog.showOpenDialog({
            title: '选择视频文件',
            filters: [{ name: '视频', extensions: ['mp4', 'mov', 'avi', 'mkv', 'webm'] }],
            properties: ['openFile']
        });
        if (!result.canceled && result.filePaths.length > 0) {
            filePath = result.filePaths[0];
        }
    } catch (e) {
        filePath = await new Promise(resolve => {
            const input = document.createElement('input');
            input.type = 'file';
            input.accept = 'video/*';
            input.onchange = ev => resolve(ev.target.files[0]?.path || null);
            input.click();
        });
    }

    if (!filePath) return;

    const videoInfo = player.addon.getVideoInfo(filePath);
    if (!videoInfo.success) {
        log(`加载失败: ${videoInfo.error || '未知错误'}`, 'error');
        return;
    }

    log(`${path.basename(filePath)} (${videoInfo.width}x${videoInfo.height})`, 'info');

    const config = {
        id: 'video_' + Date.now(),
        duration: videoInfo.durationMs,
        fps: videoInfo.frameRate,
        canvas_config: {
            width: videoInfo.width,
            height: videoInfo.height,
            ratio: `${videoInfo.width}:${videoInfo.height}`
        },
        tracks: [{
            type: 'video',
            segments: [{
                material_id: 'mat_0',
                target_timerange: { start: 0, duration: videoInfo.durationMs },
                source_timerange: { start: 0, duration: videoInfo.durationMs }
            }]
        }],
        materials: {
            videos: [{ id: 'mat_0', path: filePath }]
        }
    };

    await loadFromConfig(config);
}

module.exports = { loadFromConfig, loadVideo };
