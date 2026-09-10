const path = require('path');
const fs = require('fs');
const { pathToFileURL } = require('url');
const { formatTime } = require('../utils/logger');

const TRACK_STYLE = {
    video: { bg: '#075359', accent: '#24C4B4', icon: '\u{1F3AC}', name: '视频' },
    text:  { bg: '#984a38', accent: '#E8725C', icon: 'T',          name: '文本' },
    image: { bg: '#d29424', icon: '◔', name: '贴片' },
    svg: { bg: '#d29424', icon: '◔', name: '贴片' },
    sticker: { bg: '#d29424', icon: '◔', name: '贴纸' },
    effect: { bg: '#744982', icon: '☆', name: '特效' },
    filter: { bg: '#744982', icon: '◉', name: '滤镜' },
    audio: { bg: '#7B5DB8', accent: '#9B7DD4', icon: '\u{266A}',   name: '音频' },
};

const THUMB_PX = 60;
const LABEL_W = 72;
const ZOOM_MIN = 0.2;
const ZOOM_MAX = 20;

class Timeline {
    constructor(container) {
        this.el = container;
        this.tracks = [];
        this.duration = 0;
        this.currentTime = 0;
        this.onSeek = null;
        this.onTrackMute = null;
        this.onRefresh = null;
        this.onSelectLayer = null;
        this._selectedId = null;

        this._materials = {};
        this._groups = [];
        this._pxPerUs = 0;
        this._rulerScroll = null;
        this._rulerInner = null;
        this._bodyWrap = null;
        this._body = null;
        this._playhead = null;
        this._playheadRuler = null;
        this._zoomInput = null;
        this._dragging = false;
        this._thumbTimer = null;
        this._thumbGen = 0;
        this._stripCache = new Map();
        this._frameCache = new Map();
        this._mediaVersions = new Map();
        this._stickerHeads = new Map();

        this._build();
        this._bindEvents();
    }

    get _totalPx() {
        return this.duration * this._pxPerUs;
    }

    load(config, groups, basePath = '', proxies = {}) {
        this._selectedId = null;
        this.duration = config.duration || 0;
        this.currentTime = 0;

        this._materials = {};
        const previousScale = this._pxPerUs;
        for (const v of (config.materials?.videos || [])) {
            const file = proxies[path.resolve(basePath, v.path)]?.thumbnail || path.resolve(basePath, v.path);
            let version;
            try { const stat = fs.statSync(file); version = `${stat.size}:${stat.mtimeMs}`; }
            catch { version = 'missing'; }
            if (this._mediaVersions.get(file) !== version) {
                for (const key of this._stripCache.keys()) { if (key.startsWith(`${file}|`)) { this._stripCache.delete(key); } }
                for (const key of this._frameCache.keys()) { if (key.startsWith(`${file}@`)) { this._frameCache.delete(key); } }
                this._mediaVersions.set(file, version);
            }
            this._materials[v.id] = {
                path: file,
                name: path.basename(v.path),
                proxy: Boolean(proxies[path.resolve(basePath, v.path)]),
                duration: v.duration || 0,
            };
        }

        for (const category of ['images', 'svgs', 'stickers', 'effects', 'filters', 'audios']) {
            for (const material of config.materials?.[category] || []) {
                let name = material.name;
                const file = material.path ? path.resolve(basePath, material.path) : null;
                if (file && ['effects', 'filters'].includes(category)) {
                    try { name = JSON.parse(fs.readFileSync(path.join(file, 'config.json'), 'utf8')).name || name; } catch { /* Name remains optional. */ }
                }
                this._materials[material.id] = { ...material, name, path: file };
            }
        }
        this._groups = groups || [];

        this.tracks = (config.tracks || []).map((track, idx) => ({
            id: track.id,
            type: track.type,
            visible: track.visible !== false,
            group: this._groups[idx] || null,
            segments: (track.segments || []).map(seg => ({
                id: seg.id || seg.material_id,
                name: this._materials[seg.material_id]?.name || TRACK_STYLE[track.type]?.name || track.type,
                materialId: seg.material_id,
                visible: seg.visible !== false,
                start: seg.target_timerange?.start || 0,
                duration: seg.target_timerange?.duration || 0,
                srcStart: seg.source_timerange?.start || 0,
                srcDuration: seg.source_timerange?.duration || 0,
            })),
        }));

        this._pxPerUs = previousScale || this._calcFitScale();
        this._updateZoomInput();
        this._render();
        this._thumbGen++;
        const gen = this._thumbGen;
        requestAnimationFrame(() => this._generateThumbnails(gen));
    }

    setCurrentTime(timeUs) {
        this.currentTime = timeUs;
        this._updatePlayhead();
    }

    /** 时间轴 DOM 与图层数据同步（如工具/API 修改了 layer 后调用） */
    refresh() {
        this._render();
    }

    setZoom(pxPerUs) {
        const fit = this._calcFitScale();
        this._pxPerUs = Math.max(fit * ZOOM_MIN, Math.min(fit * ZOOM_MAX, pxPerUs));
        this._updateZoomInput();
        this._render();
        this._debouncedRefreshThumbs();
    }

    clear() {
        this._thumbGen++;
        clearTimeout(this._thumbTimer);
        this._mediaVersions.clear();
        this._stickerHeads.clear();
        this.tracks = [];
        this.duration = 0;
        this.currentTime = 0;
        this._pxPerUs = 0;
        this._stripCache.clear();
        this._frameCache.clear();
        this._render();
    }

    _calcFitScale() {
        if (!this.duration) return 0;
        const w = this._rulerScroll
            ? this._rulerScroll.offsetWidth
            : (this.el.offsetWidth - LABEL_W);
        return Math.max(0.001, w / this.duration);
    }

    // ---- DOM ----

    _build() {
        this.el.innerHTML = '';
        this.el.classList.add('tl');

        const rulerRow = document.createElement('div');
        rulerRow.className = 'tl-ruler-row';

        const rulerSpacer = document.createElement('div');
        rulerSpacer.className = 'tl-label-spacer';
        rulerRow.appendChild(rulerSpacer);

        this._rulerScroll = document.createElement('div');
        this._rulerScroll.className = 'tl-ruler-scroll';

        this._rulerInner = document.createElement('div');
        this._rulerInner.className = 'tl-ruler-inner';

        this._playheadRuler = document.createElement('div');
        this._playheadRuler.className = 'tl-playhead-handle';
        this._rulerInner.appendChild(this._playheadRuler);

        this._rulerScroll.appendChild(this._rulerInner);
        rulerRow.appendChild(this._rulerScroll);
        this.el.appendChild(rulerRow);

        this._bodyWrap = document.createElement('div');
        this._bodyWrap.className = 'tl-body-wrap';

        this._body = document.createElement('div');
        this._body.className = 'tl-body';

        this._bodyWrap.appendChild(this._body);
        this.el.appendChild(this._bodyWrap);

        this._playhead = document.createElement('div');
        this._playhead.className = 'tl-playhead-line';
        this.el.appendChild(this._playhead);

        const zoomBar = document.createElement('div');
        zoomBar.className = 'tl-zoom-bar';
        const zoomLabel = document.createElement('span');
        zoomLabel.className = 'tl-zoom-label';
        zoomLabel.textContent = '−';
        zoomBar.appendChild(zoomLabel);
        this._zoomInput = document.createElement('input');
        this._zoomInput.type = 'range';
        this._zoomInput.min = '0';
        this._zoomInput.max = '100';
        this._zoomInput.value = '0';
        this._zoomInput.className = 'tl-zoom-slider';
        zoomBar.appendChild(this._zoomInput);
        const zoomLabelR = document.createElement('span');
        zoomLabelR.className = 'tl-zoom-label';
        zoomLabelR.textContent = '+';
        zoomBar.appendChild(zoomLabelR);
        this.el.appendChild(zoomBar);
    }

    // ---- Events ----

    _bindEvents() {
        const seek = (e) => {
            if (!this.duration || !this._pxPerUs) return;
            const rect = this._rulerScroll.getBoundingClientRect();
            const scrollX = this._bodyWrap.scrollLeft;
            const rawPx = (e.clientX - rect.left) + scrollX;
            const timeUs = Math.max(0, Math.min(this.duration, rawPx / this._pxPerUs));
            this.setCurrentTime(timeUs);
            if (this.onSeek) this.onSeek(timeUs);
        };

        const startDrag = (e) => {
            if (drag || e.target.closest('.tl-label, .tl-segment')) return;
            this._dragging = true;
            seek(e);
        };

        this._rulerScroll.addEventListener('mousedown', startDrag);
        this._body.addEventListener('mousedown', startDrag);

        document.addEventListener('mousemove', e => {
            if (this._dragging) seek(e);
        });
        document.addEventListener('mouseup', () => {
            this._dragging = false;
        });

        this._bodyWrap.addEventListener('scroll', () => {
            this._rulerScroll.scrollLeft = this._bodyWrap.scrollLeft;
            this._updatePlayhead();
        });

        this._zoomInput.addEventListener('input', () => {
            const val = parseFloat(this._zoomInput.value);
            const fit = this._calcFitScale();
            const lo = fit * ZOOM_MIN;
            const hi = fit * ZOOM_MAX;
            this._pxPerUs = lo * Math.pow(hi / lo, val / 100);
            this._render();
            this._debouncedRefreshThumbs();
        });

        this.el.addEventListener('wheel', (e) => {
            if (!e.ctrlKey && !e.metaKey) return;
            e.preventDefault();
            const fit = this._calcFitScale();
            const lo = fit * ZOOM_MIN;
            const hi = fit * ZOOM_MAX;
            const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
            this._pxPerUs = Math.max(lo, Math.min(hi, this._pxPerUs * factor));
            this._updateZoomInput();
            this._render();
            this._debouncedRefreshThumbs();
        }, { passive: false });

        window.addEventListener('resize', () => {
            this._updatePlayhead();
            this._debouncedRefreshThumbs();
        });

        let drag = null;
        this._body.addEventListener('pointerdown', e => {
            const el = e.target.closest('.tl-segment');
            if (e.button !== 0 || e.target.closest('button, .tl-label')) { return; }
            if (!el) { this._deselectSegment(); return; }
            e.preventDefault();
            const ti = Number(el.dataset.trackIdx), si = Number(el.dataset.segIdx);
            const segment = this.tracks[ti].segments[si];
            this._selectSegment(ti, si);
            if (this.currentTime < segment.start || this.currentTime >= segment.start + segment.duration) {
                this.onSeek?.(segment.start);
            }
            drag = { el, ti, si, x: e.clientX, y: e.clientY, start: this.tracks[ti].segments[si].start, moved: false };
            this._body.setPointerCapture(e.pointerId);
        });
        this._body.addEventListener('pointermove', e => {
            if (!drag) { return; }
            const delta = e.clientX - drag.x;
            if (Math.hypot(delta, e.clientY - drag.y) < 4 && !drag.moved) { return; }
            drag.moved = true;
            drag.el.classList.add('dragging');
            drag.time = Math.max(0, drag.start + delta / this._pxPerUs);
            if (!e.altKey && Math.abs(drag.time - this.currentTime) * this._pxPerUs < 8) { drag.time = this.currentTime; }
            const rows = [...this._body.querySelectorAll('.tl-track')];
            rows.forEach(row => row.classList.remove('drop-target', 'drop-invalid'));
            const row = rows.find(row => { const r = row.getBoundingClientRect(); return e.clientY >= r.top && e.clientY <= r.bottom; });
            drag.target = row ? Number(row.dataset.trackIdx) : null;
            const valid = drag.target !== null && this.tracks[drag.target].type === this.tracks[drag.ti].type;
            if (row) { row.classList.add(valid ? 'drop-target' : 'drop-invalid'); }
            drag.valid = valid;
            drag.el.style.left = `${drag.time * this._pxPerUs}px`;
            drag.el.style.transform = `translateY(${e.clientY - drag.y}px)`;
        });
        this._body.addEventListener('pointerup', e => {
            if (!drag) { return; }
            const d = drag; drag = null;
            if (this._body.hasPointerCapture(e.pointerId)) { this._body.releasePointerCapture(e.pointerId); }
            d.el.classList.remove('dragging');
            d.el.style.transform = '';
            this._body.querySelectorAll('.tl-track').forEach(row => row.classList.remove('drop-target', 'drop-invalid'));
            if (d.moved && d.valid && this.onMoveSegment) {
                this.onMoveSegment(this.tracks[d.ti].id, this.tracks[d.ti].segments[d.si].id, d.time, this.tracks[d.target].id);
            } else if (d.moved) {
                this._render();
            }
        });
        this._body.addEventListener('pointercancel', () => { drag = null; this._render(); });

    }

    _updateZoomInput() {
        if (!this._zoomInput) return;
        const fit = this._calcFitScale();
        const lo = fit * ZOOM_MIN;
        const hi = fit * ZOOM_MAX;
        if (lo >= hi || this._pxPerUs <= lo) {
            this._zoomInput.value = '0';
        } else {
            const val = 100 * Math.log(this._pxPerUs / lo) / Math.log(hi / lo);
            this._zoomInput.value = String(Math.round(Math.max(0, Math.min(100, val))));
        }
    }

    // ---- Render ----

    _render() {
        this._renderRuler();
        this._captureStrips();
        this._renderTracks();
        this._updatePlayhead();
    }

    _renderRuler() {
        const ticks = this._rulerInner.querySelectorAll('.tl-tick');
        ticks.forEach(el => el.remove());
        if (!this.duration || !this._pxPerUs) return;

        this._rulerInner.style.width = `${this._totalPx}px`;

        const step = this._calcTickStep();
        for (let t = 0; t <= this.duration; t += step) {
            const px = t * this._pxPerUs;
            const tick = document.createElement('span');
            tick.className = 'tl-tick';
            tick.style.left = `${px}px`;
            tick.textContent = this._rulerTime(t, step);
            this._rulerInner.appendChild(tick);
        }
    }

    _renderTracks() {
        const existing = this._body.querySelectorAll('.tl-track');
        existing.forEach(el => el.remove());

        const totalPx = this._totalPx;
        this._body.style.width = `${LABEL_W + totalPx}px`;

        // SDK 按协议顺序叠加，最后一轨在最上层；时间轴从上层向下展示。
        for (let trackIdx = this.tracks.length - 1; trackIdx >= 0; trackIdx--) {
            const track = this.tracks[trackIdx];
            const style = TRACK_STYLE[track.type] || { bg: '#66686b', icon: '◇', name: track.type };

            const row = document.createElement('div');
            row.className = `tl-track tl-track-${track.type}`;
            row.dataset.trackIdx = trackIdx;

            const label = document.createElement('div');
            label.className = 'tl-label';

            const iconEl = document.createElement('span');
            iconEl.className = 'tl-label-icon';
            iconEl.textContent = style.icon;
            label.appendChild(iconEl);

            const group = track.group;
            row.classList.toggle('is-invisible', group ? !group.visible : !track.visible);
            const layers = new Map((group?.layers || []).map(layer => [layer.id, layer]));

            if (track.type !== 'audio' && group) {
                const eyeBtn = document.createElement('button');
                eyeBtn.className = 'tl-label-btn tl-btn-eye';
                eyeBtn.textContent = '\u{1F441}';
                eyeBtn.title = '显示/隐藏';
                eyeBtn.classList.toggle('off', !group.visible);

                eyeBtn.addEventListener('click', () => {
                    group.visible = !group.visible;
                    eyeBtn.classList.toggle('off', !group.visible);
                    row.classList.toggle('is-invisible', !group.visible);
                    if (this.onRefresh) this.onRefresh();
                });
                label.appendChild(eyeBtn);
            }

            if ((track.type === 'video' || track.type === 'audio') && group) {
                const sndBtn = document.createElement('button');
                sndBtn.className = 'tl-label-btn tl-btn-snd';
                sndBtn.title = '声音开/关';
                sndBtn.textContent = group.muted ? '\u{1F507}' : '\u{1F50A}';
                sndBtn.classList.toggle('off', group.muted);

                sndBtn.addEventListener('click', () => {
                    group.muted = !group.muted;
                    sndBtn.classList.toggle('off', group.muted);
                    sndBtn.textContent = group.muted ? '\u{1F507}' : '\u{1F50A}';
                    if (this.onTrackMute) this.onTrackMute(track.id, group.muted);
                });
                label.appendChild(sndBtn);
            }

            row.appendChild(label);

            const segsEl = document.createElement('div');
            segsEl.className = 'tl-segments';
            segsEl.style.width = `${totalPx}px`;

            for (let segIdx = 0; segIdx < track.segments.length; segIdx++) {
                const seg = track.segments[segIdx];
                const leftPx = seg.start * this._pxPerUs;
                const widthPx = seg.duration * this._pxPerUs;

                const segEl = document.createElement('div');
                segEl.className = `tl-segment tl-seg-${track.type}`;
                segEl.classList.toggle('selected', seg.id === this._selectedId);
                const layer = layers.get(seg.id);
                segEl.classList.toggle('is-invisible', layer ? !layer.visible : !seg.visible);
                segEl.dataset.trackIdx = trackIdx;
                segEl.dataset.segIdx = segIdx;
                segEl.style.left = `${leftPx}px`;
                segEl.style.width = `${widthPx}px`;
                segEl.style.background = style.bg;
                segEl.title = `${seg.name}\n${formatTime(seg.start)} ~ ${formatTime(seg.start + seg.duration)}`;

                if (track.type === 'video') {
                    const mat = this._materials[seg.materialId];
                    if (mat) segEl.dataset.videoPath = mat.path;
                    segEl.dataset.srcStart = seg.srcStart;
                    segEl.dataset.srcDuration = seg.srcDuration;

                    const info = document.createElement('div');
                    info.className = 'tl-seg-info';
                    const fname = mat ? (mat.name || path.basename(mat.path)) : seg.name;
                    const dur = mat ? mat.duration : seg.srcDuration;
                    info.textContent = `${fname}  ${formatTime(dur)}`;
                    segEl.appendChild(info);

                    const stripWrap = document.createElement('div');
                    stripWrap.className = 'tl-seg-strip';
                    segEl.appendChild(stripWrap);

                    const bottom = document.createElement('div');
                    bottom.className = 'tl-seg-pad';
                    segEl.appendChild(bottom);
                } else if (['image', 'svg', 'sticker'].includes(track.type)) {
                    const material = this._materials[seg.materialId];
                    if (material?.path) {
                        const icon = document.createElement('canvas');
                        icon.className = 'tl-sticker-preview';
                        icon.width = 40; icon.height = 40;
                        icon.setAttribute('aria-label', seg.name);
                        segEl.appendChild(icon);
                        this._loadStickerHead(material.path).then(frame => {
                            if (icon.isConnected) { icon.getContext('2d').drawImage(frame, 0, 0); }
                        }).catch(error => {
                            icon.title = '贴片预览加载失败';
                            console.warn('贴片预览加载失败', material.path, error);
                        });
                    }
                } else {
                    const segName = document.createElement('span');
                    segName.className = 'tl-seg-name';
                    const layer = track.group?.layers?.[segIdx];
                    let label = seg.name;
                    if (layer) {
                        if (track.type === 'text') {
                            const t = (layer.text || '').replace(/\s+/g, ' ').trim();
                            label = t
                                ? (t.length > 28 ? `${t.slice(0, 28)}…` : t)
                                : (layer.id || seg.name);
                        }
                    }
                    if (track.type === 'effect') {
                        const star = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
                        star.setAttribute('viewBox', '0 0 24 24');
                        star.setAttribute('aria-hidden', 'true');
                        star.classList.add('tl-effect-icon');
                        const outline = document.createElementNS(star.namespaceURI, 'path');
                        outline.setAttribute('d', 'm12 2 3 7 7 1-5 5 1 7-6-4-6 4 1-7-5-5 7-1Z');
                        star.appendChild(outline);
                        segEl.appendChild(star);
                    }
                    segName.textContent = label;
                    segEl.title = `${label}\n${formatTime(seg.start)} ~ ${formatTime(seg.start + seg.duration)}`;
                    segEl.appendChild(segName);
                }

                segsEl.appendChild(segEl);
            }

            row.appendChild(segsEl);
            this._body.appendChild(row);
        }
        for (const el of this._body.querySelectorAll('.tl-seg-video[data-video-path]')) {
            this._updateThumbMetrics(el);
            const cached = this._stripCache.get(this._segKey(el.dataset));
            if (cached) { this._attachStrip(el, cached); }
        }
    }

    _updatePlayhead() {
        if (!this._playhead || !this._bodyWrap) return;
        const timePx = this.currentTime * this._pxPerUs;
        const scrollX = this._bodyWrap.scrollLeft;
        const offsetPx = timePx - scrollX;
        const viewW = this._bodyWrap.clientWidth;
        const visible = offsetPx >= 0 && offsetPx <= viewW;
        this._playhead.style.display = visible ? '' : 'none';
        if (visible) this._playhead.style.left = `${LABEL_W + offsetPx}px`;
        if (this._playheadRuler) {
            this._playheadRuler.style.left = `${timePx}px`;
        }
    }

    _calcTickStep() {
        if (!this._pxPerUs) return 1000000;
        const rawUs = 120 / this._pxPerUs;
        const nice = [100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000, 30000000, 60000000, 120000000, 300000000];
        for (const step of nice) {
            if (step >= rawUs * 0.7) return step;
        }
        return 300000;
    }

    _rulerTime(ms, step) {
        const totalSec = Math.floor(ms / 1000000);
        const min = Math.floor(totalSec / 60);
        const sec = totalSec % 60;
        const base = `${String(min).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
        if (step < 1000000) {
            const frac = Math.floor((ms % 1000000) / 100000);
            return `${base}.${frac}`;
        }
        return base;
    }

    // ---- Selection ----

    selectLayer(id) {
        for (let ti = 0; ti < this.tracks.length; ti++) {
            const si = this.tracks[ti].segments.findIndex(s => s.id === id);
            if (si >= 0) { this._selectSegment(ti, si); return; }
        }
        this._deselectSegment();
    }

    _selectSegment(trackIdx, segIdx) {
        try {
            this._selectedId = this.tracks[trackIdx]?.segments[segIdx]?.id || null;
            const old = this._body.querySelector('.tl-segment.selected');
            if (old) old.classList.remove('selected');

            const segEl = this._body.querySelector(
                `.tl-segment[data-track-idx="${trackIdx}"][data-seg-idx="${segIdx}"]`);
            if (segEl) segEl.classList.add('selected');

            const track = this.tracks[trackIdx];
            if (!track?.group) return;
            const isAudioTrack = track.type === 'audio';

            // 音频片段没有画布图层，只同步时间轴与音频属性。
            let layer = null;
            if (!isAudioTrack) {
                const layers = track.group.layers;
                if (!layers || segIdx >= layers.length) return;
                layer = layers[segIdx];
            }

            if (this.onSelectLayer) {
                this.onSelectLayer({
                    layer,
                    group: track.group,
                    trackType: track.type,
                    segName: track.segments[segIdx]?.name || '',
                    segment: track.segments[segIdx] || null,
                });
            }
        } catch (e) {
            // ignore selection errors
        }
    }

    _deselectSegment() {
        this._selectedId = null;
        const old = this._body.querySelector('.tl-segment.selected');
        if (old) old.classList.remove('selected');
        if (this.onSelectLayer) this.onSelectLayer(null);
    }

    async _loadStickerHead(file) {
        const stat = fs.statSync(file);
        let source = file, sequence;
        if (path.extname(file).toLowerCase() === '.json') {
            sequence = JSON.parse(fs.readFileSync(file, 'utf8'));
            source = path.resolve(path.dirname(file), sequence.path);
            if (!(sequence.singleWidth > 0 && sequence.singleHeight > 0)) {
                return Promise.reject(new Error('无效的序列帧尺寸'));
            }
        }
        const sourceStat = fs.statSync(source);
        const key = `${file}:${stat.mtimeMs}:${stat.size}:${sourceStat.mtimeMs}:${sourceStat.size}`;
        if (this._stickerHeads.has(key)) { return this._stickerHeads.get(key); }
        const pending = (async () => {
            const canvas = document.createElement('canvas');
            canvas.width = 40; canvas.height = 40;
            const draw = (image, width, height) => {
                const scale = Math.min(40 / width, 40 / height);
                const w = width * scale, h = height * scale;
                canvas.getContext('2d').drawImage(image, 0, 0, width, height, (40 - w) / 2, (40 - h) / 2, w, h);
            };
            if (path.extname(source).toLowerCase() === '.gif') {
                const decoder = new ImageDecoder({ data: new Uint8Array(fs.readFileSync(source)), type: 'image/gif' });
                try {
                    const { image } = await decoder.decode({ frameIndex: 0 });
                    try { draw(image, image.displayWidth, image.displayHeight); }
                    finally { image.close(); }
                } finally { decoder.close(); }
            } else {
                const image = new Image();
                image.src = pathToFileURL(source).href;
                await image.decode();
                draw(image, sequence?.singleWidth || image.naturalWidth, sequence?.singleHeight || image.naturalHeight);
            }
            return canvas;
        })();
        this._stickerHeads.set(key, pending);
        pending.catch(() => this._stickerHeads.delete(key));
        if (this._stickerHeads.size > 200) { this._stickerHeads.delete(this._stickerHeads.keys().next().value); }
        return pending;
    }

    // ---- Strip & Frame Cache ----

    _updateThumbMetrics(el) {
        const box = el.querySelector('.tl-seg-strip');
        el.dataset.thumbWidth = box.clientWidth;
        el.dataset.thumbHeight = box.clientHeight;
        el.dataset.thumbDpr = window.devicePixelRatio || 1;
    }

    _attachStrip(el, cached) {
        const copy = document.createElement('canvas');
        copy.width = cached.width; copy.height = cached.height;
        copy.getContext('2d').drawImage(cached, 0, 0);
        copy.style.cssText = `width:${el.dataset.thumbWidth}px;height:${el.dataset.thumbHeight}px;display:block;`;
        el.querySelector('.tl-seg-strip').replaceChildren(copy);
    }

    _segKey(dataset) {
        return `${dataset.videoPath}|${dataset.srcStart}|${dataset.srcDuration}|${dataset.thumbWidth}|${dataset.thumbHeight}|${dataset.thumbDpr}`;
    }

    _frameCacheKey(videoPath, timeSec, width, height, dpr) {
        const rt = Math.round(timeSec * 2) / 2;
        return `${videoPath}@${rt.toFixed(1)}|${width}x${height}|${dpr}`;
    }

    _frameCacheStore(key, canvas) {
        const MAX_FRAMES = 300;
        this._frameCache.set(key, canvas);
        if (this._frameCache.size > MAX_FRAMES) {
            const oldest = this._frameCache.keys().next().value;
            this._frameCache.delete(oldest);
        }
    }

    _captureStrips() {
        const segs = this._body.querySelectorAll('.tl-seg-video[data-video-path]');
        for (const segEl of segs) {
            const canvas = segEl.querySelector('.tl-seg-strip canvas');
            if (!canvas || !canvas.width) continue;
            canvas.classList.remove('tl-thumb-new');

            this._stripCache.set(this._segKey(segEl.dataset), canvas);
        }
    }

    // ---- Thumbnails ----

    _debouncedRefreshThumbs() {
        clearTimeout(this._thumbTimer);
        this._thumbTimer = setTimeout(() => {
            requestAnimationFrame(() => this._refreshThumbnails());
        }, 300);
    }

    _refreshThumbnails() {
        this._thumbGen++;
        this._generateThumbnails(this._thumbGen);
    }

    async _generateThumbnails(genId) {
        if (genId === undefined) genId = this._thumbGen;
        const videoSegs = this._body.querySelectorAll('.tl-seg-video[data-video-path]');
        if (!videoSegs.length) return;

        const groups = new Map();
        for (const el of videoSegs) {
            const previous = this._segKey(el.dataset);
            this._updateThumbMetrics(el);
            if (previous === this._segKey(el.dataset) && el.querySelector('.tl-seg-strip canvas')) { continue; }
            const cached = this._stripCache.get(this._segKey(el.dataset));
            if (cached) { this._attachStrip(el, cached); continue; }
            const vp = el.dataset.videoPath;
            if (!groups.has(vp)) groups.set(vp, []);
            groups.get(vp).push(el);
        }

        for (const [filePath, segs] of groups) {
            if (genId !== this._thumbGen) return;
            try {
                const video = await this._loadVideoEl(filePath);
                for (const segEl of segs) {
                    if (genId !== this._thumbGen) { video.src = ''; return; }
                    const cached = this._stripCache.get(this._segKey(segEl.dataset));
                    if (cached) {
                        this._attachStrip(segEl, cached);
                    } else {
                        await this._fillFilmstrip(video, segEl, genId);
                    }
                }
                video.src = '';
                video.load();
            } catch (e) {
                console.warn('thumbnail extract failed:', filePath, e);
            }
        }
    }

    _loadVideoEl(filePath) {
        return new Promise((resolve, reject) => {
            const video = document.createElement('video');
            video.muted = true;
            video.preload = 'auto';
            video.src = pathToFileURL(filePath).href;
            video.onloadeddata = () => resolve(video);
            video.onerror = () => reject(new Error(`cannot load ${filePath}`));
        });
    }

    async _fillFilmstrip(video, segEl, genId) {
        const stripWrap = segEl.querySelector('.tl-seg-strip');
        if (!stripWrap) return;

        const videoPath = segEl.dataset.videoPath;
        const srcStart = (parseFloat(segEl.dataset.srcStart) || 0) / 1000000;
        const srcDur = (parseFloat(segEl.dataset.srcDuration) || 0) / 1000000;
        const segW = stripWrap.offsetWidth;
        const segH = stripWrap.offsetHeight;

        if (segW <= 0 || srcDur <= 0 || segH <= 0) return;

        const dpr = window.devicePixelRatio || 1;
        const thumbW = THUMB_PX;
        const cellAspect = thumbW / segH;
        const count = Math.max(1, Math.ceil(segW / THUMB_PX));
        const canvasW = segW;

        const strip = document.createElement('canvas');
        strip.width = Math.ceil(canvasW * dpr);
        strip.height = Math.ceil(segH * dpr);
        const ctx = strip.getContext('2d');
        ctx.scale(dpr, dpr);
        ctx.imageSmoothingQuality = 'high';

        const vw = video.videoWidth;
        const vh = video.videoHeight;
        const vidAspect = vw / vh;
        let sx, sy, sw, sh;
        if (vidAspect > cellAspect) {
            sh = vh; sw = vh * cellAspect;
            sx = (vw - sw) / 2; sy = 0;
        } else {
            sw = vw; sh = vw / cellAspect;
            sx = 0; sy = (vh - sh) / 2;
        }

        for (let i = 0; i < count; i++) {
            if (genId !== this._thumbGen || !segEl.isConnected) { return; }
            const dx = i * thumbW;
            const dw = Math.min(thumbW, canvasW - dx);
            const ratio = dw / thumbW;
            const t = Math.floor(srcStart + srcDur * (i + 0.5) / count);
            const fKey = this._frameCacheKey(videoPath, t, thumbW, segH, dpr);
            const cached = this._frameCache.get(fKey);

            if (cached) {
                ctx.drawImage(cached, 0, 0, cached.width * ratio, cached.height, dx, 0, dw, segH);
            } else {
                const target = Math.max(0, Math.min(t, video.duration - 0.01));
                if (Math.abs(video.currentTime - target) > 0.001) {
                    await new Promise((resolve, reject) => {
                        const finish = error => {
                            clearTimeout(timer);
                            video.removeEventListener('seeked', ready);
                            video.removeEventListener('error', failed);
                            error ? reject(error) : resolve();
                        };
                        const ready = () => finish();
                        const failed = () => finish(new Error('视频缩略图定位失败'));
                        const timer = setTimeout(() => finish(new Error('视频缩略图定位超时')), 5000);
                        video.addEventListener('seeked', ready, { once: true });
                        video.addEventListener('error', failed, { once: true });
                        video.currentTime = target;
                    });
                }
                ctx.drawImage(video, sx, sy, sw * ratio, sh, dx, 0, dw, segH);

                const fc = document.createElement('canvas');
                fc.width = Math.ceil(thumbW * dpr);
                fc.height = Math.ceil(segH * dpr);
                const frameCtx = fc.getContext('2d');
                frameCtx.imageSmoothingQuality = 'high';
                frameCtx.drawImage(video, sx, sy, sw, sh, 0, 0, fc.width, fc.height);
                this._frameCacheStore(fKey, fc);
            }
        }

        if (genId !== this._thumbGen || !segEl.isConnected) { return; }
        strip.style.cssText = `width:${segW}px;height:${segH}px;display:block;`;
        strip.classList.add('tl-thumb-new');
        stripWrap.replaceChildren(strip);

        const old = stripWrap.querySelector('.tl-thumb-old');
        if (old) old.remove();

        this._stripCache.set(this._segKey(segEl.dataset), strip);
        if (this._stripCache.size > 200) { this._stripCache.delete(this._stripCache.keys().next().value); }
    }
}

module.exports = Timeline;
