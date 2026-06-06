const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const agent = require('../agent');

const VIDEO_EXTS = ['.mp4', '.mov', '.avi', '.mkv', '.webm'];
const AUDIO_EXTS = ['.mp3', '.wav', '.aac', '.ogg', '.flac', '.m4a'];
const IMAGE_EXTS = ['.jpg', '.jpeg', '.png', '.gif', '.webp', '.bmp', '.svg'];

function classifyFile(filePath) {
    const ext = path.extname(filePath).toLowerCase();
    if (VIDEO_EXTS.includes(ext)) return 'video';
    if (AUDIO_EXTS.includes(ext)) return 'audio';
    if (IMAGE_EXTS.includes(ext)) return 'image';
    return null;
}

class MediaManager {
    constructor() {
        this._uuid = null;
        this._items = [];
    }

    async setProject(uuid) {
        this._uuid = uuid;
        await agent.initThread(uuid);
        const { resources = [] } = await agent.getResources(uuid);
        this._items = resources.map((r) => ({
            id: r.resource_id,
            name: r.resource_name,
            path: r.resource_path,
            type: r.resource_type,
            size: r.resource_size,
            addedAt: r.added_at,
        }));
    }

    getItems() {
        return this._items;
    }

    async addItems(filePaths) {
        if (!this._uuid) return [];
        const added = [];
        for (const fp of filePaths) {
            const absPath = path.resolve(fp);
            if (!fs.existsSync(absPath)) continue;
            if (this._items.some(m => m.path === absPath)) continue;

            const type = classifyFile(absPath);
            if (!type) continue;

            const stat = fs.statSync(absPath);
            const item = {
                id: crypto.randomUUID(),
                name: path.basename(absPath),
                path: absPath,
                type,
                size: stat.size,
                addedAt: new Date().toISOString(),
            };
            this._items.push(item);
            added.push(item);
        }
        if (added.length) {
            await agent.addResources(this._uuid, added);
        }
        return added;
    }

    async removeItem(id) {
        if (!this._uuid) return false;
        const { success } = await agent.removeResource(this._uuid, id);
        if (success) {
            this._items = this._items.filter(m => m.id !== id);
        }
        return success;
    }
}

module.exports = MediaManager;
