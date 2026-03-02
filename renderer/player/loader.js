const path = require('path');
const fs = require('fs');
const player = require('../state');
const { render, updateUI } = require('./renderer');
const { stop } = require('./controls');
const { log, formatTime } = require('../utils/logger');

const ROOT_DIR = path.join(__dirname, '..', '..');

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

        player.width = player.root.width;
        player.height = player.root.height;
        player.duration = player.root.durationMs;
        player.frameRate = player.root.frameRate;
        player.currentTime = 0;

        document.getElementById('file-info').textContent = '已加载';
        document.getElementById('project-id').textContent = player.root.id ? `项目: ${player.root.id}` : '项目: -';
        document.getElementById('gpu-info').textContent = `GPU: ${player.root.gpuInfo}`;

        log(`✓ 加载成功 (${(t1-t0).toFixed(1)}ms) | ID: ${player.root.id || '-'} | ${player.root.width}×${player.root.height} | ${player.root.frameRate.toFixed(2)}fps | ${formatTime(player.root.durationMs)}`, 'ok');

        const layers = player.root.getLayers();
        log(`图层数量: ${layers.length}`, 'info');
        layers.forEach((layer, i) => {
            let info = `  [${i}] "${layer.name}" | ${layer.type} | ${formatTime(layer.startTime)}~${formatTime(layer.endTime)}`;
            if (layer.type === 'text') info += ` | text="${layer.text}"`;
            if (layer.type === 'video' && layer.videoFrameRate) info += ` | ${layer.videoFrameRate.toFixed(1)}fps`;
            log(info, 'info');
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

        render(0);
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

async function loadTest() {
    try {
        const testPath = path.join(ROOT_DIR, 'test', 'test.json');
        log(`加载测试: ${testPath}`, 'info');

        const jsonStr = fs.readFileSync(testPath, 'utf8');
        const config = JSON.parse(jsonStr);

        await loadFromConfig(config);

    } catch (e) {
        log(`✗ 测试加载失败: ${e.message}`, 'err');
        console.error(e);
    }
}

module.exports = { loadFromConfig, loadVideo, loadTest };
