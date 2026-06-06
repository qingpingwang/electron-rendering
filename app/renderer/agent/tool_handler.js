const { ipcRenderer } = require('electron');
const player = require('../state');

function initToolHandler() {
    ipcRenderer.on('tool-call', async (_event, { id, action, params }) => {
        // 等待 load-project / loadFromConfig 完成，否则 root.loaded 仍为 false
        await player.whenProjectOpsIdle();
        let data;
        try {
            const result = executeAction(action, params);
            data = { success: true, ...result };
        } catch (e) {
            data = { success: false, error: e.message };
        }
        ipcRenderer.send('tool-result', { id, data });
    });
}

function executeAction(action, params) {
    switch (action) {
        case 'getProjectInfo': return getProjectInfo();
        case 'getTextLayerDigest': return getTextLayerDigest();
        case 'getProjectProtocol': return getProjectProtocol();
        case 'updateText': return updateText(params);
        case 'setLayerProperty': return setLayerProperty(params);
        case 'setCurrentTime': return setCurrentTime(params);
        default: throw new Error(`Unknown action: ${action}`);
    }
}

/** 供动态系统提示词使用：扁平列出文字图层 id→文案，与内存图层一致，便于模型核对改字是否已生效 */
function getTextLayerDigest() {
    const root = player.root;
    if (!root || !root.loaded) {
        return { loaded: false, message: '没有加载项目', layers: [] };
    }
    const groups = root.getGroups();
    const layers = [];
    for (let gi = 0; gi < groups.length; gi++) {
        const g = groups[gi];
        const ls = g.layers || [];
        for (let li = 0; li < ls.length; li++) {
            const l = ls[li];
            if (l.type === 'text') {
                layers.push({ layerId: l.id, text: l.text });
            }
        }
    }
    return { loaded: true, layers };
}

/** 供聊天侧动态系统提示词使用：完整工程协议 JSON（非工具暴露） */
function getProjectProtocol() {
    const root = player.root;
    if (!root || !root.loaded) {
        return { loaded: false, message: '没有加载项目', protocol: null };
    }
    const protocol = typeof root.exportConfig === 'function' ? root.exportConfig() : null;
    return { loaded: true, protocol: protocol || null };
}

function getProjectInfo() {
    const root = player.root;
    if (!root || !root.loaded) {
        return { loaded: false, message: '没有加载项目' };
    }

    const groups = root.getGroups();
    const tracks = [];

    for (let gi = 0; gi < groups.length; gi++) {
        const g = groups[gi];
        const layers = g.layers || [];
        const layerInfos = [];

        for (let li = 0; li < layers.length; li++) {
            const l = layers[li];
            const info = {
                id: l.id,
                type: l.type,
                startTime: l.startTime,
                endTime: l.endTime,
                durationMs: l.durationMs,
                visible: l.visible,
                alpha: l.alpha,
            };
            if (l.type === 'text') {
                info.text = l.text;
            }
            layerInfos.push(info);
        }

        tracks.push({
            index: gi,
            id: g.id,
            type: g.type,
            visible: g.visible,
            layerCount: layerInfos.length,
            layers: layerInfos,
        });
    }

    return {
        loaded: true,
        width: root.width,
        height: root.height,
        durationMs: root.durationMs,
        frameRate: root.frameRate,
        trackCount: tracks.length,
        tracks,
    };
}

function findLayerById(layerId) {
    const root = player.root;
    if (!root) throw new Error('项目未加载');
    return root.findLayerById(layerId);
}

function updateText({ layerId, text }) {
    const layer = findLayerById(layerId);
    if (layer.type !== 'text') {
        throw new Error(`图层 "${layerId}" 不是文字图层（类型: ${layer.type}）`);
    }
    const oldText = layer.text;
    layer.text = text;
    refreshAfterLayerMutation();
    return { layerId, oldText, newText: text };
}

function setLayerProperty({ layerId, property, value }) {
    const layer = findLayerById(layerId);
    const oldValue = layer[property];

    if (oldValue === undefined) {
        throw new Error(`图层 "${layerId}" 没有属性 "${property}"`);
    }

    layer[property] = value;
    refreshAfterLayerMutation();
    return { layerId, property, oldValue, newValue: value };
}

function setCurrentTime({ timeMs }) {
    if (!player.root || !player.root.loaded) {
        throw new Error('项目未加载');
    }
    player.video.render(timeMs, true, false);
    if (player.timeline) {
        player.timeline.setCurrentTime(timeMs);
    }
    return { timeMs };
}

function refreshCanvas() {
    if (player.video) {
        player.video.render(player.video.currentTime, true, false);
    }
}

function refreshAfterLayerMutation() {
    refreshCanvas();
    if (typeof player.notifyUiAfterLayerChange === 'function') {
        player.notifyUiAfterLayerChange();
    }
}

module.exports = { initToolHandler };
