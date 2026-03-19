const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const db = require('../db');

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

    setProject(uuid) {
        this._uuid = uuid;
        db.media.pruneInvalid(uuid);
        this._items = db.media.list(uuid);
    }

    getItems() {
        return this._items;
    }

    addItems(filePaths) {
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

            db.media.add(this._uuid, item);
            this._items.push(item);
            added.push(item);
        }
        return added;
    }

    removeItem(id) {
        const success = db.media.remove(id);
        if (success) {
            this._items = this._items.filter(m => m.id !== id);
        }
        return success;
    }
}

module.exports = MediaManager;
