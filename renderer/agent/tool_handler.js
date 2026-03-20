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
        case 'updateText': return updateText(params);
        case 'setLayerProperty': return setLayerProperty(params);
        case 'setCurrentTime': return setCurrentTime(params);
        default: throw new Error(`Unknown action: ${action}`);
    }
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
                name: l.name,
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

function findLayer(name) {
    const root = player.root;
    if (!root || !root.loaded) throw new Error('项目未加载');

    const groups = root.getGroups();
    for (const g of groups) {
        const layers = g.layers || [];
        for (const l of layers) {
            if (l.name === name) return l;
        }
    }
    throw new Error(`未找到名为 "${name}" 的图层`);
}

function updateText({ layerName, text }) {
    const layer = findLayer(layerName);
    if (layer.type !== 'text') {
        throw new Error(`图层 "${layerName}" 不是文字图层（类型: ${layer.type}）`);
    }
    const oldText = layer.text;
    layer.text = text;
    refreshCanvas();
    return { layerName, oldText, newText: text };
}

function setLayerProperty({ layerName, property, value }) {
    const layer = findLayer(layerName);
    const oldValue = layer[property];

    if (oldValue === undefined) {
        throw new Error(`图层 "${layerName}" 没有属性 "${property}"`);
    }

    layer[property] = value;
    refreshCanvas();
    return { layerName, property, oldValue, newValue: value };
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

module.exports = { initToolHandler };
