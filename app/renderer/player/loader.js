const path = require('path');
const player = require('../state');
const { updateUI } = require('./renderer');
const { stop } = require('./controls');
const { log, formatTime } = require('../utils/logger');

async function loadFromConfig(config, protocolPath = '', { editing = false, timeUs = 0, prepareCache = !editing } = {}) {
    return player.scheduleProjectOp(async () => {

        try {
            player.beforeProjectLoad?.();
            if (editing) {
                player.audio.stop();
                player.video.stopRenderLoop();
            } else {
                stop();
            }
            player.loading = true;
            updateUI();
            protocolPath = path.resolve(protocolPath || player.projectBase || process.cwd());
            if (!editing) {
                require('../../project_files').ensure(protocolPath, config);
            }
            if (prepareCache) {
                const ready = editing && protocolPath === player.projectBase ? player.mediaProxies : {};
                player.mediaProxies = await require('../editor/media_cache')(config, protocolPath, ready);
            }

            // Text style fonts are nested JSON; resolve them against the project too.
            const loadConfig = { ...config, materials: { ...config.materials } };
            loadConfig.materials.texts = (config.materials?.texts || []).map(material => {
                const content = JSON.parse(material.content);
                for (const style of content.styles || []) {
                    if (style.font?.path) {
                        style.font.path = path.resolve(protocolPath, style.font.path);
                    }
                }
                return { ...material, content: JSON.stringify(content) };
            });
            const jsonStr = JSON.stringify(loadConfig);
            log(`加载配置: ${config.tracks?.length || 0} 轨道`, 'info');

            const t0 = performance.now();
            const result = player.root.load(jsonStr, protocolPath);
            if (!result.success) {
                throw new Error(`C++ 加载失败: ${result.error}`);
            }
            for (const track of config.tracks || []) {
                if (track.type !== 'video') { continue; }
                for (const segment of track.segments || []) {
                    const material = config.materials.videos.find(item => item.id === segment.material_id);
                    const proxy = player.mediaProxies[path.resolve(protocolPath, material.path)];
                    player.root.findLayerById(segment.id).setProxyPath(proxy.video);
                }
            }
            const t1 = performance.now();

            player.projectBase = protocolPath;
            player.video.load(player.root);
            player.mediaLibrary?.sync(config, protocolPath);

            const groups = player.root.getGroups();
            if (player.timeline) player.timeline.load(config, groups, protocolPath, player.mediaProxies);


            log(`✓ 加载成功 (${(t1-t0).toFixed(1)}ms) | ID: ${player.root.id || '-'} | ${player.video.width}×${player.video.height} | ${player.video.frameRate.toFixed(2)}fps | ${formatTime(player.video.duration)}`, 'ok');
            log(`轨道组: ${groups.length}`, 'info');
            groups.forEach((g, gi) => {
                log(`  [${gi}] "${g.id}" | ${g.type}`, 'info');
                g.layers.forEach((layer, i) => {
                    let info = `    [${i}] "${layer.id}" | ${formatTime(layer.startTime)}~${formatTime(layer.endTime)}`;
                    if (g.type === 'text') info += ` | text="${layer.text}"`;
                    if (g.type === 'video' && layer.videoFrameRate) info += ` | ${layer.videoFrameRate.toFixed(1)}fps`;
                    log(info, 'info');
                });
            });

            player.video.render(Math.max(0, Math.min(timeUs, player.video.duration)), true, false);
            try {
                const audioInfos = player.root.getAudioInfos();
                const infoKeys = Object.keys(audioInfos);
                log(`音频信息: ${infoKeys.length} 条 [${infoKeys.join(', ')}]`, 'info');
                for (const [k, v] of Object.entries(audioInfos)) {
                    log(`  ${k}: vol=${v.volume} path=${v.path} type=${v.layerType}`, 'info');
                }

                const audioCount = await player.audio.load(player.root, player.mediaProxies);
                log(`✓ 音频解码: ${audioCount}/${infoKeys.length} 条成功 | ctx=${player.audio.ctx.state}`, audioCount > 0 ? 'ok' : 'warn');

                for (const [id, track] of player.audio.tracks) {
                    log(`  ${id}: ${track.buffer.duration.toFixed(2)}s ${track.buffer.numberOfChannels}ch ${track.buffer.sampleRate}Hz`, 'info');
                }
            } catch (e) {
                log(`⚠ 音频加载失败: ${e.message}`, 'warn');
            }

            player.audio.seek(player.video.currentTime);
            updateUI();

        } catch (e) {
            log(`✗ ${e.message}`, 'err');
            console.error(e);
            throw e;
        } finally {
            player.loading = false;
            updateUI();
            player.refreshProjectInspector?.();
        }
    });
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
        log(`加载失败: ${videoInfo.error || '未知错误'}`, 'err');
        return;
    }

    log(`${path.basename(filePath)} (${videoInfo.width}x${videoInfo.height})`, 'info');

    const config = {
        id: 'video_' + Date.now(),
        duration: videoInfo.durationUs,
        fps: videoInfo.frameRate,
        canvas_config: {
            width: videoInfo.width,
            height: videoInfo.height,
            ratio: `${videoInfo.width}:${videoInfo.height}`
        },
        tracks: [{
            id: 'track_0',
            type: 'video',
            segments: [{
                id: 'segment_0',
                material_id: 'mat_0',
                target_timerange: { start: 0, duration: videoInfo.durationUs },
                source_timerange: { start: 0, duration: videoInfo.durationUs }
            }]
        }],
        materials: {
            videos: [{ id: 'mat_0', path: filePath }]
        }
    };

    await loadFromConfig(config);
}

module.exports = { loadFromConfig, loadVideo };
