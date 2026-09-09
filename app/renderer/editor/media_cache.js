const path = require('path');
const { randomUUID } = require('crypto');
const { ipcRenderer } = require('electron');

async function prepareMedia(config, projectRoot, ready = {}) {
    const sources = [...new Set((config.materials?.videos || []).map(item => path.resolve(projectRoot, item.path)))];
    const reusable = Object.fromEntries(sources.filter(source => {
        const result = ready[source];
        return result && [result.thumbnail, result.video, ...(result.audio ? [result.audio] : [])].every(file => require('fs').existsSync(file));
    }).map(source => [source, ready[source]]));
    const missing = sources.filter(source => !reusable[source]);
    if (!missing.length) { return reusable; }
    const cacheDir = path.join(projectRoot, '.cache');
    const status = document.createElement('div');
    status.className = 'media-cache-status';
    status.setAttribute('role', 'status');
    status.textContent = `构建缓存中 0/${sources.length}`;
    const requestId = randomUUID();
    const onProgress = (_, event) => {
        if (event.requestId !== requestId) { return; }
        const { completed, total, message, type, status: state } = event.progress;
        if (type === 'file-progress' && state === 'running' && !status.isConnected) {
            document.body.appendChild(status);
        }
        status.textContent = `构建缓存中 ${completed}/${total}${message ? ` · ${message}` : ''}`;
    };
    ipcRenderer.on('media-cache:progress', onProgress);
    try {
        const results = await ipcRenderer.invoke('media-cache:prepare', { videoPaths: missing, cacheDir, requestId });
        return { ...reusable, ...Object.fromEntries(results.map(result => [result.source, result])) };
    } finally {
        ipcRenderer.removeListener('media-cache:progress', onProgress);
        status.remove();
    }
}
module.exports = prepareMedia;
