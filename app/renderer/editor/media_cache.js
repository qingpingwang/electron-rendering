const path = require('path');
const { randomUUID } = require('crypto');
const { ipcRenderer } = require('electron');

async function prepareMedia(config, projectRoot) {
    const sources = [...new Set((config.materials?.videos || []).map(item => path.resolve(projectRoot, item.path)))];
    const cacheDir = path.join(projectRoot, '.cache');
    const status = document.createElement('div');
    status.className = 'media-cache-status';
    status.setAttribute('role', 'status');
    status.textContent = `构建缓存中 0/${sources.length}`;
    document.body.appendChild(status);
    const requestId = randomUUID();
    const onProgress = (_, event) => {
        if (event.requestId !== requestId) { return; }
        const { completed, total, message } = event.progress;
        status.textContent = `构建缓存中 ${completed}/${total}${message ? ` · ${message}` : ''}`;
    };
    ipcRenderer.on('media-cache:progress', onProgress);
    try {
        const results = await ipcRenderer.invoke('media-cache:prepare', { videoPaths: sources, cacheDir, requestId });
        return Object.fromEntries(results.map(result => [result.source, result]));
    } finally {
        ipcRenderer.removeListener('media-cache:progress', onProgress);
        status.remove();
    }
}
module.exports = prepareMedia;
