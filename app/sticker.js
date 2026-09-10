const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');

// Presentation only: never rewrites catalog paths or SDK material metadata.
function parseSticker(directory) {
    const config = JSON.parse(fs.readFileSync(path.join(directory, 'config.json'), 'utf8'));
    if (!config.path) {
        throw new Error(`贴纸配置缺少 path：${directory}`);
    }
    const source = path.resolve(directory, config.path);
    const sequence = config.singleWidth > 0 && config.singleHeight > 0;
    const extension = path.extname(source).toLowerCase();
    if (!sequence && !['.gif', '.png', '.jpg', '.jpeg', '.webp'].includes(extension)) {
        throw new Error(`不支持的贴纸预览类型：${source}`);
    }
    return {
        kind: sequence ? 'sequence' : extension === '.gif' ? 'gif' : 'image',
        source,
        frameCount: sequence ? config.frameCount : 1,
        frameDurationUs: sequence ? Math.floor(1000000 / (config.fps ?? 30)) : 0,
        atlasWidth: sequence ? config.bigWidth : undefined,
        atlasHeight: sequence ? config.bigHeight : undefined,
        frameWidth: sequence ? config.singleWidth : undefined,
        frameHeight: sequence ? config.singleHeight : undefined,
    };
}

async function createPreview(directory, size = 88) {
    const info = parseSticker(directory);
    const canvas = document.createElement('canvas');
    canvas.width = size * (window.devicePixelRatio || 1);
    canvas.height = canvas.width;
    const context = canvas.getContext('2d');
    const frames = [];
    let atlas;
    try {
        if (info.kind === 'gif') {
            const decoder = new ImageDecoder({ data: new Uint8Array(fs.readFileSync(info.source)), type: 'image/gif' });
            try {
                await decoder.tracks.ready;
                const count = decoder.tracks.selectedTrack?.frameCount;
                if (!Number.isInteger(count) || count <= 0) {
                    throw new Error(`贴纸 GIF 没有可解码帧：${directory}`);
                }
                for (let index = 0; index < count; index++) {
                    const { image } = await decoder.decode({ frameIndex: index });
                    frames.push({ image, durationUs: image.duration > 0 ? image.duration : (count > 1 ? 100000 : 0) });
                }
            } finally {
                decoder.close();
            }
        } else {
            atlas = new Image();
            atlas.src = pathToFileURL(info.source).href;
            await atlas.decode();
            if (info.kind === 'sequence') {
                const columns = info.atlasWidth / info.frameWidth;
                const rows = info.atlasHeight / info.frameHeight;
                if (!Number.isInteger(columns) || !Number.isInteger(rows) || columns <= 0 || rows <= 0 ||
                    !Number.isInteger(info.frameCount) || info.frameCount <= 0 || info.frameCount > columns * rows ||
                    !Number.isFinite(info.frameDurationUs) || info.frameDurationUs <= 0 ||
                    atlas.naturalWidth !== info.atlasWidth || atlas.naturalHeight !== info.atlasHeight) {
                    throw new Error(`无效的贴纸序列帧配置：${directory}`);
                }
                for (let index = 0; index < info.frameCount; index++) {
                    frames.push({ image: atlas, x: (index % columns) * info.frameWidth,
                        y: Math.floor(index / columns) * info.frameHeight,
                        width: info.frameWidth, height: info.frameHeight, durationUs: info.frameDurationUs });
                }
            } else {
                frames.push({ image: atlas, durationUs: 0 });
            }
        }
    } catch (error) {
        frames.forEach(frame => frame.image.close?.());
        throw error;
    }
    const totalUs = frames.reduce((sum, frame) => sum + frame.durationUs, 0);
    let displayed = -1;
    let elapsedUs = 0;
    let startedAt = 0;
    let request = null;
    let disposed = false;
    const draw = index => {
        if (index === displayed) { return; }
        const frame = frames[index];
        const width = frame.width || frame.image.displayWidth || frame.image.naturalWidth;
        const height = frame.height || frame.image.displayHeight || frame.image.naturalHeight;
        const scale = Math.min(canvas.width / width, canvas.height / height);
        const w = width * scale, h = height * scale;
        // Clear transparent pixels too, so frames never leave trails.
        context.clearRect(0, 0, canvas.width, canvas.height);
        context.drawImage(frame.image, frame.x || 0, frame.y || 0, width, height,
            (canvas.width - w) / 2, (canvas.height - h) / 2, w, h);
        displayed = index;
    };
    const tick = now => {
        if (!canvas.isConnected) { canvas.pauseStickerPreview(); return; }
        let timeUs = (elapsedUs + (now - startedAt) * 1000) % totalUs;
        let index = 0;
        while (index < frames.length - 1 && timeUs >= frames[index].durationUs) {
            timeUs -= frames[index++].durationUs;
        }
        draw(index);
        request = requestAnimationFrame(tick);
    };
    canvas.playStickerPreview = () => {
        if (disposed || request !== null || frames.length < 2 || totalUs <= 0) { return; }
        startedAt = performance.now();
        request = requestAnimationFrame(tick);
    };
    canvas.pauseStickerPreview = () => {
        if (request === null) { return; }
        elapsedUs = (elapsedUs + (performance.now() - startedAt) * 1000) % totalUs;
        cancelAnimationFrame(request);
        request = null;
    };
    canvas.disposeStickerPreview = () => {
        if (disposed) { return; }
        canvas.pauseStickerPreview();
        disposed = true;
        frames.forEach(frame => frame.image.close?.());
        frames.length = 0;
        atlas = null;
    };
    draw(0);
    return canvas;
}

module.exports = { parseSticker, createPreview };
