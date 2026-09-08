const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');
const { log } = require('../utils/logger');

// Effects and transitions share the same resource-config and hover playback contract.
function createResourcePreview(item, card) {
    const preview = document.createElement('div');
    try {
        const config = JSON.parse(fs.readFileSync(path.join(item.path, 'config.json'), 'utf8'));
        if (!config.preview_video) {
            throw new Error('缺少 preview_video');
        }
        const video = document.createElement('video');
        video.muted = true;
        video.loop = true;
        video.playsInline = true;
        video.preload = 'auto';
        video.src = pathToFileURL(path.resolve(item.path, config.preview_video)).href;
        video.setAttribute('aria-label', `${item.name}预览`);
        preview.appendChild(video);
        card.onmouseenter = () => {
            video.play().catch(error => {
                if (error.name !== 'AbortError') {
                    log(`预览播放失败：${item.name}：${error.message}`, 'err');
                }
            });
        };
        card.onmouseleave = () => video.pause();
        video.onerror = () => {
            const message = document.createElement('small');
            message.textContent = '预览加载失败';
            video.replaceWith(message);
        };
    } catch (error) {
        preview.textContent = '暂无预览';
        log(`无法读取预览：${item.name}：${error.message}`, 'err');
    }
    return preview;
}

module.exports = createResourcePreview;
