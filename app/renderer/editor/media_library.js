const path = require('path');
const fs = require('fs');
const createResourcePreview = require('./resource_preview');
const RESOURCE_ROOT = path.resolve(__dirname, '../../../resources');
const CATEGORIES = [
    ['media', '媒体', '▣'], ['audio', '音频', '♫'], ['texts', '文本', 'Tᴛ'],
    ['stickers', '贴纸', '◔'], ['effects', '特效', '☆'], ['transitions', '转场', '⋈'],
    ['captions', '字幕', '▤'], ['filters', '滤镜', '♧'], ['adjustments', '调节', '☷'],
];
const CATEGORY_ICONS = {
    media: '<rect x="3" y="5" width="18" height="14" rx="1"/><path d="m10 9 5 3-5 3Z"/>',
    audio: '<path d="M10 17V5l10-2v12M10 8l10-2"/><ellipse cx="7" cy="17" rx="3" ry="2"/><ellipse cx="17" cy="15" rx="3" ry="2"/>',
    texts: '<path d="M3 6V4h18v2M12 4v16M8 20h8"/>',
    stickers: '<path d="M20 14a9 9 0 1 1-10-11 10 10 0 0 0 10 11Z"/><path d="M10 3v7a4 4 0 0 0 4 4h6"/>',
    effects: '<path d="m12 2 3 7 7 1-5 5 1 7-6-4-6 4 1-7-5-5 7-1Z"/>',
    transitions: '<path d="m3 5 9 7-9 7ZM21 5l-9 7 9 7Z"/>',
    captions: '<rect x="3" y="4" width="18" height="16" rx="1"/><path d="M6 10h12M6 14h5m2 0h5M6 17h12"/>',
    filters: '<circle cx="9" cy="9" r="6"/><circle cx="15" cy="15" r="6"/>',
    adjustments: '<path d="M3 6h18M3 12h18M3 18h18"/><path d="M8 3v6m8 0v6m-8 0v6"/>',
};
const { pathToFileURL } = require('url');
const { randomUUID } = require('crypto');
const player = require('../state');
const { log } = require('../utils/logger');

class MediaLibrary {
    constructor() {
        this.items = new Map();
        this.projectItems = {};
        this.catalog = JSON.parse(fs.readFileSync(path.join(RESOURCE_ROOT, 'resources.json'), 'utf8'));
        this.category = 'media';
        this.source = 'local';
        this.sortAscending = true;
        const tabs = document.getElementById('resource-tabs');
        for (const [key, label] of CATEGORIES) {
            const button = document.createElement('button'); button.dataset.category = key;
            const glyph = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            glyph.setAttribute('viewBox', '0 0 24 24');
            glyph.setAttribute('fill', 'none');
            glyph.setAttribute('stroke', 'currentColor');
            glyph.setAttribute('stroke-width', '1.5');
            glyph.setAttribute('stroke-linecap', 'round');
            glyph.setAttribute('stroke-linejoin', 'round');
            glyph.setAttribute('aria-hidden', 'true');
            glyph.innerHTML = CATEGORY_ICONS[key];
            const text = document.createElement('span'); text.textContent = label;
            button.append(glyph, text); button.title = label;
            button.onclick = () => { this.category = key; this.render(); };
            tabs.appendChild(button);
        }
        document.querySelectorAll('[data-source]').forEach(button => {
            button.onclick = () => { this.source = button.dataset.source; this.render(); };
        });
        document.getElementById('sidebar-import').onclick = () => this.pick();
        document.getElementById('resource-sort').onclick = () => { this.sortAscending = !this.sortAscending; this.render(); };
        document.getElementById('resource-filter').onchange = () => this.render();
        this.list = document.getElementById('media-list');
        document.getElementById('btn-import').onclick = () => this.pick();
        document.getElementById('btn-add-text').onclick = () => this.addText().catch(e => log(e.message, 'err'));
        document.getElementById('media-search').oninput = () => this.render();
        const panel = document.querySelector('.media-panel');
        panel.addEventListener('dragover', e => e.preventDefault());
        panel.addEventListener('drop', e => {
            e.preventDefault();
            for (const file of e.dataTransfer.files) { if (file.path) { this.import(file.path); } }
        });
        const timeline = document.getElementById('timeline');
        const clearDropTarget = () => timeline.querySelectorAll('.tl-track').forEach(row => row.classList.remove('drop-target', 'drop-invalid'));
        const dropTrack = e => {
            const row = [...timeline.querySelectorAll('.tl-track')].find(row => {
                const r = row.getBoundingClientRect();
                return e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
            });
            return row ? { row, track: player.timeline.tracks[Number(row.dataset.trackIdx)] } : null;
        };
        timeline.addEventListener('dragover', e => {
            e.preventDefault();
            clearDropTarget();
            const target = dropTrack(e);
            const type = this.draggedItem?.type || 'video';
            const valid = !target || target.track.type === (['effect', 'transition'].includes(type) ? 'video' : type);
            if (target) { target.row.classList.add(valid ? 'drop-target' : 'drop-invalid'); }
            e.dataTransfer.dropEffect = valid ? 'copy' : 'none';
        });
        timeline.addEventListener('dragleave', e => {
            if (!timeline.contains(e.relatedTarget)) { clearDropTarget(); }
        });
        document.addEventListener('dragend', () => { clearDropTarget(); this.draggedItem = null; });
        timeline.addEventListener('drop', e => {
            e.preventDefault();
            const x = e.clientX - player.timeline._rulerScroll.getBoundingClientRect().left + player.timeline._rulerScroll.scrollLeft;
            const time = player.timeline._pxPerMs ? Math.max(0, Math.round(x / player.timeline._pxPerMs)) : 0;
            const target = dropTrack(e);
            clearDropTarget();
            const type = this.draggedItem?.type || 'video';
            if (target && target.track.type !== (['effect', 'transition'].includes(type) ? 'video' : type)) { return; }
            if (target && ['effect', 'transition'].includes(type)) {
                const segment = target.track.segments.find(s => time >= s.start && time < s.start + s.duration);
                if (!segment) { return; }
                player.timeline.selectLayer(segment.id);
            }
            const targetId = target?.track.id;
            const resourceId = e.dataTransfer.getData('application/x-nle-resource');
            if (resourceId) {
                const item = [...Object.values(this.catalog).flat(), ...Object.values(this.projectItems).flat()].find(r => r.id === resourceId);
                if (item) { this.useResource(item, time, targetId).catch(err => log(err.message, 'err')); }
                return;
            }
            const file = e.dataTransfer.getData('application/x-nle-video');
            const paths = file ? [file] : [...e.dataTransfer.files].map(f => f.path).filter(Boolean);
            this.addMany(paths, time, targetId).catch(err => log(err.message, 'err'));
        });
        this.render();
    }
    sync(config, base) {
        this.projectItems = {};
        for (const [category, key, type] of [['media', 'videos', 'video'], ['audio', 'audios', 'audio'], ['effects', 'effects', 'effect'], ['transitions', 'transitions', 'transition']]) {
            this.projectItems[category] = (config.materials?.[key] || []).map(item => ({
                ...item, type, path: path.resolve(base || '', item.path), name: item.name || path.basename(item.path),
            }));
        }
        for (const v of config.materials?.videos || []) {
            const file = path.resolve(base || '', v.path);
            this.items.set(file, { ...v, path: file });
        }
        this.render();
    }
    import(file) {
        const info = player.addon.getVideoInfo(file);
        if (!info.success) { log(`无法导入 ${path.basename(file)}：${info.error}`, 'err'); return null; }
        const item = { ...info, path: file, duration: info.durationMs };
        this.items.set(file, item);
        this.render();
        return item;
    }
    async pick() {
        try {
            const { dialog } = require('@electron/remote');
            const result = await dialog.showOpenDialog({ title: '导入视频素材', properties: ['openFile', 'multiSelections'], filters: [{ name: '视频', extensions: ['mp4', 'mov'] }] });
            for (const file of result.filePaths) { this.import(file); }
        } catch (e) { log(e.message, 'err'); }
    }
    config() {
        if (player.root.loaded) { return JSON.parse(player.root.exportConfig()); }
        return { id: randomUUID(), duration: 5000, fps: 30, canvas_config: { width: 1920, height: 1080, ratio: '16:9' }, materials: { videos: [], texts: [] }, tracks: [] };
    }
    async apply(config, id, time) {
        await require('../player/loader').loadFromConfig(config, player.projectBase || '');
        player.video.render(time, true, false);
        player.timeline.selectLayer(id);
    }
    async addMany(paths, time, targetId) {
        const config = this.config();
        let lastId;
        for (const file of paths) {
            const item = this.import(file);
            if (!item) { continue; }
            const materialId = randomUUID(); lastId = randomUUID();
            config.materials.videos ||= [];
            config.materials.videos.push({ id: materialId, path: file, width: item.width, height: item.height, duration: item.duration });
            this.insertSegment(config, 'video', { id: lastId, material_id: materialId, source_timerange: { start: 0, duration: item.duration }, target_timerange: { start: time, duration: item.duration } }, targetId);
            config.duration = Math.max(config.duration, time + item.duration);
        }
        if (lastId) { await this.apply(config, lastId, time); }
    }
    insertSegment(config, type, segment, targetId) {
        const target = targetId ? config.tracks.find(t => t.id === targetId) : null;
        if (targetId && (!target || target.type !== type)) { throw new Error('目标轨道类型不匹配'); }
        if (target) {
            const range = segment.target_timerange;
            for (const other of [...target.segments].sort((a, b) => a.target_timerange.start - b.target_timerange.start)) {
                const r = other.target_timerange;
                if (range.start < r.start + r.duration && range.start + range.duration > r.start) { range.start = r.start + r.duration; }
            }
            target.segments.push(segment);
            target.segments.sort((a, b) => a.target_timerange.start - b.target_timerange.start);
        } else {
            config.tracks.push({ id: randomUUID(), type, segments: [segment] });
        }
        config.duration = Math.max(config.duration, segment.target_timerange.start + segment.target_timerange.duration);
    }
    async addText() {
        const config = this.config(), materialId = randomUUID(), id = randomUUID();
        const start = Math.round(player.video.currentTime || 0), duration = 3000;
        config.materials.texts ||= [];
        config.materials.texts.push({ id: materialId, alignment: 1, content: JSON.stringify({ text: '输入文字', styles: [{ range: [0, 4], size: 80, fill: { content: { solid: { color: [1, 1, 1], alpha: 1 } } } }] }) });
        config.tracks.push({ id: randomUUID(), type: 'text', segments: [{ id, material_id: materialId, target_timerange: { start, duration } }] });
        config.duration = Math.max(config.duration, start + duration);
        await this.apply(config, id, start);
    }
    async useResource(item, time = Math.round(player.video.currentTime || 0), targetId) {
        const file = path.resolve(RESOURCE_ROOT, item.path);
        if (item.type === 'video') { await this.addMany([file], time, targetId); return; }
        const config = this.config();
        const start = time;
        const materialId = randomUUID();
        if (item.type === 'audio') {
            const bytes = fs.readFileSync(file);
            const decoded = await player.audio.ctx.decodeAudioData(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
            const duration = Math.round(decoded.duration * 1000), id = randomUUID();
            config.materials.audios ||= [];
            config.materials.audios.push({ id: materialId, name: item.name, path: file });
            this.insertSegment(config, 'audio', { id, material_id: materialId, source_timerange: { start: 0, duration }, target_timerange: { start, duration } }, targetId);
            config.duration = Math.max(config.duration, start + duration);
            await this.apply(config, id, start);
            return;
        }
        const selected = player.canvasTransform.info?.layer?.id;
        const track = config.tracks.find(t => t.type === 'video' && t.segments.some(s => s.id === selected));
        if (!track) { log('未应用资源：未选择视频片段', 'warn'); return; }
        const index = track.segments.findIndex(s => s.id === selected), segment = track.segments[index];
        if (item.type === 'transition') {
            const next = track.segments[index + 1];
            if (!next) { log('未应用转场：所选视频片段后没有相邻片段', 'warn'); return; }
            config.materials.transitions ||= [];
            const oldIds = new Set(config.materials.transitions.map(t => t.id));
            segment.extra_material_refs = (segment.extra_material_refs || []).filter(id => !oldIds.has(id));
            config.materials.transitions.push({ id: materialId, name: item.name, path: file, type: 'transition', duration: Math.min(item.duration, segment.target_timerange.duration, next.target_timerange.duration) });
        } else if (item.type === 'effect') {
            config.materials.effects ||= [];
            config.materials.effects.push({ id: materialId, name: item.name, path: file, type: 'effect' });
        } else { return; }
        segment.extra_material_refs ||= [];
        segment.extra_material_refs.push(materialId);
        await this.apply(config, selected, segment.target_timerange.start);
    }
    render() {
        this.list.querySelectorAll('video').forEach(video => video.pause());
        this.list.replaceChildren();
        this.list.classList.toggle('effect-preview-list', ['effects', 'transitions'].includes(this.category));
        const category = CATEGORIES.find(c => c[0] === this.category);
        document.querySelectorAll('[data-category]').forEach(b => { b.classList.toggle('active', b.dataset.category === this.category); b.setAttribute('aria-pressed', b.dataset.category === this.category); });
        document.querySelectorAll('[data-source]').forEach(b => b.classList.toggle('active', b.dataset.source === this.source));
        document.getElementById('btn-import').hidden = this.category !== 'media';
        document.getElementById('btn-add-text').hidden = this.category !== 'texts';
        document.getElementById('resource-filter').hidden = this.category !== 'media';
        document.getElementById('resource-heading').textContent = this.source === 'project' ? '项目素材' : category[1];
        document.getElementById('resource-sort').textContent = `名称 ${this.sortAscending ? '↓' : '↑'}`;
        const library = (this.catalog[this.category] || []).map(item => ({ ...item, path: path.resolve(RESOURCE_ROOT, item.path), resource: item }));
        const imported = this.category === 'media' ? [...this.items.values()].map(v => ({ ...v, type: 'video', name: path.basename(v.path) })) : [];
        const project = (this.projectItems[this.category] || []).map(item => ({ ...item, resource: item }));
        const files = this.source === 'project' ? project : [...imported, ...project];
        const merged = new Map();
        const catalogOnly = ['effects', 'transitions'].includes(this.category);
        const visibleItems = catalogOnly ? library : [...(this.source !== 'project' ? library : []), ...(this.source !== 'library' ? files : [])];
        for (const item of visibleItems) { merged.set(item.path, item); }
        const search = document.getElementById('media-search').value.trim().toLowerCase();
        const filter = this.category === 'media' ? document.getElementById('resource-filter').value : 'all';
        const items = [...merged.values()].filter(v => (v.name || '').toLowerCase().includes(search) && (filter === 'all' || v.type === filter)).sort((a, b) => (this.sortAscending ? 1 : -1) * a.name.localeCompare(b.name, 'zh-CN'));
        document.getElementById('media-count').textContent = `${items.length} 项`;
        for (const item of items) {
            const card = document.createElement('div'); card.className = 'media-card'; card.draggable = true; card.tabIndex = 0; card.setAttribute('role', 'button');
            card.title = item.description ? `${item.name}：${item.description}` : item.name;
            card.setAttribute('aria-label', item.name);
            const isRenderResource = ['effect', 'transition'].includes(item.type);
            const preview = isRenderResource ? createResourcePreview(item, card) : document.createElement('div');
            preview.className = `resource-preview ${item.type}`;
            if (item.type === 'video') {
                const video = document.createElement('video'); video.src = pathToFileURL(player.mediaProxies?.[item.path]?.thumbnail || item.path).href; video.preload = 'metadata'; video.muted = true;
                preview.appendChild(video);
                video.onloadedmetadata = () => { if (Number.isFinite(video.duration)) { duration.textContent = `${Math.floor(video.duration / 60).toString().padStart(2, '0')}:${Math.floor(video.duration % 60).toString().padStart(2, '0')}`; } };
            } else if (!isRenderResource) {
                const glyph = document.createElement('span'); glyph.className = 'resource-glyph'; glyph.textContent = item.type === 'audio' ? '♫' : item.type === 'transition' ? '⋈' : '✧'; preview.appendChild(glyph);
                const label = document.createElement('small'); label.textContent = item.type === 'audio' ? '本地音频' : '本地渲染资源'; preview.appendChild(label);
            }
            const duration = document.createElement('span'); duration.className = 'resource-duration'; preview.appendChild(duration);
            const plus = document.createElement('button'); plus.className = 'resource-add'; plus.textContent = '+'; plus.title = `添加 ${item.name}`;
            const use = () => (item.resource ? this.useResource(item.resource) : this.addMany([item.path], Math.round(player.video.currentTime || 0))).catch(e => { log(e.message, 'err'); });
            plus.onclick = e => { e.stopPropagation(); use(); }; preview.appendChild(plus);
            const name = document.createElement('span'); name.className = 'resource-name'; name.textContent = item.name;
            card.append(preview, name);
            card.ondblclick = use;
            card.onkeydown = e => { if (e.key === 'Enter' && e.target === card) { use(); } };
            card.ondragstart = e => {
                this.draggedItem = item;
                e.dataTransfer.effectAllowed = 'copy';
                e.dataTransfer.setData(item.resource ? 'application/x-nle-resource' : 'application/x-nle-video', item.resource ? item.resource.id : item.path);
                const ghost = document.createElement('canvas');
                ghost.width = 88; ghost.height = 88;
                const ctx = ghost.getContext('2d');
                ctx.fillStyle = '#151618'; ctx.fillRect(0, 0, 88, 88);
                const video = preview.querySelector('video');
                if (video?.readyState >= 2) {
                    const ratio = Math.min(88 / video.videoWidth, 88 / video.videoHeight);
                    const w = video.videoWidth * ratio, h = video.videoHeight * ratio;
                    ctx.drawImage(video, (88 - w) / 2, (88 - h) / 2, w, h);
                }
                Object.assign(ghost.style, { position: 'fixed', left: '-200px', top: '0' });
                document.body.appendChild(ghost);
                e.dataTransfer.setDragImage(ghost, 44, 44);
                setTimeout(() => ghost.remove(), 0);
            };
            this.list.appendChild(card);
        }
        if (!items.length) {
            const empty = document.createElement('div'); empty.className = 'media-empty'; empty.textContent = search ? '没有匹配的资源' : `暂无${category[1]}资源`;
            this.list.appendChild(empty);
        }
    }
}
module.exports = MediaLibrary;
