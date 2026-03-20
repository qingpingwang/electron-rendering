const path = require('path');
const { formatTime } = require('../utils/logger');

const TRACK_STYLE = {
    video: { bg: '#1B9C8F', accent: '#24C4B4', icon: '\u{1F3AC}', name: '视频' },
    text:  { bg: '#D4605A', accent: '#E8725C', icon: 'T',          name: '文本' },
    audio: { bg: '#7B5DB8', accent: '#9B7DD4', icon: '\u{266A}',   name: '音频' },
};

const THUMB_PX = 80;
const THUMB_RATIO = 1.8;
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

        this._materials = {};
        this._groups = [];
        this._pxPerMs = 0;
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

        this._build();
        this._bindEvents();
    }

    get _totalPx() {
        return this.duration * this._pxPerMs;
    }

    load(config, groups) {
        this.duration = config.duration || 0;
        this.currentTime = 0;

        this._materials = {};
        this._stripCache.clear();
        this._frameCache.clear();
        for (const v of (config.materials?.videos || [])) {
            this._materials[v.id] = {
                path: path.resolve(v.path),
                duration: v.duration || 0,
            };
        }

        this._groups = groups || [];

        this.tracks = (config.tracks || []).map((track, idx) => ({
            id: track.id,
            type: track.type,
            group: this._groups[idx] || null,
            segments: (track.segments || []).map(seg => ({
                id: seg.id || seg.material_id,
                name: seg.id || seg.material_id,
                materialId: seg.material_id,
                start: seg.target_timerange?.start || 0,
                duration: seg.target_timerange?.duration || 0,
                srcStart: seg.source_timerange?.start || 0,
                srcDuration: seg.source_timerange?.duration || 0,
            })),
        }));

        this._pxPerMs = this._calcFitScale();
        this._updateZoomInput();
        this._render();
        this._thumbGen++;
        const gen = this._thumbGen;
        requestAnimationFrame(() => this._generateThumbnails(gen));
    }

    setCurrentTime(timeMs) {
        this.currentTime = timeMs;
        this._updatePlayhead();
    }

    /** 时间轴 DOM 与图层数据同步（如工具/API 修改了 layer 后调用） */
    refresh() {
        this._render();
    }

    setZoom(pxPerMs) {
        const fit = this._calcFitScale();
        this._pxPerMs = Math.max(fit * ZOOM_MIN, Math.min(fit * ZOOM_MAX, pxPerMs));
        this._updateZoomInput();
        this._render();
        this._debouncedRefreshThumbs();
    }

    clear() {
        this.tracks = [];
        this.duration = 0;
        this.currentTime = 0;
        this._pxPerMs = 0;
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
            if (!this.duration || !this._pxPerMs) return;
            const rect = this._rulerScroll.getBoundingClientRect();
            const scrollX = this._bodyWrap.scrollLeft;
            const rawPx = (e.clientX - rect.left) + scrollX;
            const timeMs = Math.max(0, Math.min(this.duration, rawPx / this._pxPerMs));
            this.setCurrentTime(timeMs);
            if (this.onSeek) this.onSeek(timeMs);
        };

        const startDrag = (e) => {
            if (e.target.closest('.tl-label')) return;
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
            this._pxPerMs = lo * Math.pow(hi / lo, val / 100);
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
            this._pxPerMs = Math.max(lo, Math.min(hi, this._pxPerMs * factor));
            this._updateZoomInput();
            this._render();
            this._debouncedRefreshThumbs();
        }, { passive: false });

        window.addEventListener('resize', () => {
            this._updatePlayhead();
        });

        let clickPos = null;
        this._body.addEventListener('mousedown', (e) => {
            clickPos = { x: e.clientX, y: e.clientY };
        });
        this._body.addEventListener('click', (e) => {
            if (clickPos) {
                const dx = e.clientX - clickPos.x;
                const dy = e.clientY - clickPos.y;
                if (dx * dx + dy * dy > 25) return;
            }
            if (e.target.closest('.tl-label')) return;
            const segEl = e.target.closest('.tl-segment');
            if (segEl && segEl.dataset.trackIdx !== undefined) {
                this._selectSegment(parseInt(segEl.dataset.trackIdx), parseInt(segEl.dataset.segIdx));
            } else {
                this._deselectSegment();
            }
        });
    }

    _updateZoomInput() {
        if (!this._zoomInput) return;
        const fit = this._calcFitScale();
        const lo = fit * ZOOM_MIN;
        const hi = fit * ZOOM_MAX;
        if (lo >= hi || this._pxPerMs <= lo) {
            this._zoomInput.value = '0';
        } else {
            const val = 100 * Math.log(this._pxPerMs / lo) / Math.log(hi / lo);
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
        if (!this.duration || !this._pxPerMs) return;

        this._rulerInner.style.width = `${this._totalPx}px`;

        const step = this._calcTickStep();
        for (let t = 0; t <= this.duration; t += step) {
            const px = t * this._pxPerMs;
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

        for (let trackIdx = 0; trackIdx < this.tracks.length; trackIdx++) {
            const track = this.tracks[trackIdx];
            const style = TRACK_STYLE[track.type] || TRACK_STYLE.video;

            const row = document.createElement('div');
            row.className = `tl-track tl-track-${track.type}`;

            const label = document.createElement('div');
            label.className = 'tl-label';

            const iconEl = document.createElement('span');
            iconEl.className = 'tl-label-icon';
            iconEl.textContent = style.icon;
            label.appendChild(iconEl);

            const group = track.group;

            if (track.type !== 'audio' && group) {
                const eyeBtn = document.createElement('button');
                eyeBtn.className = 'tl-label-btn tl-btn-eye';
                eyeBtn.textContent = '\u{1F441}';
                eyeBtn.title = '显示/隐藏';
                eyeBtn.classList.toggle('off', !group.visible);

                eyeBtn.addEventListener('click', () => {
                    group.visible = !group.visible;
                    eyeBtn.classList.toggle('off', !group.visible);
                    const segs = row.querySelector('.tl-segments');
                    if (segs) segs.style.opacity = group.visible ? '1' : '0.3';
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
                const leftPx = seg.start * this._pxPerMs;
                const widthPx = seg.duration * this._pxPerMs;

                const segEl = document.createElement('div');
                segEl.className = `tl-segment tl-seg-${track.type}`;
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
                    const fname = mat ? path.basename(mat.path) : seg.name;
                    const dur = mat ? mat.duration : seg.srcDuration;
                    info.textContent = `${fname}  ${formatTime(dur)}`;
                    segEl.appendChild(info);

                    const stripWrap = document.createElement('div');
                    stripWrap.className = 'tl-seg-strip';
                    segEl.appendChild(stripWrap);

                    const cachedStrip = this._stripCache.get(this._segKey(segEl.dataset));
                    if (cachedStrip) {
                        cachedStrip.classList.add('tl-thumb-old');
                        stripWrap.appendChild(cachedStrip);
                    }

                    const bottom = document.createElement('div');
                    bottom.className = 'tl-seg-pad';
                    segEl.appendChild(bottom);
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
                                : (layer.name || seg.name);
                        } else if (layer.name) {
                            label = layer.name;
                        }
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
    }

    _updatePlayhead() {
        if (!this._playhead || !this._bodyWrap) return;
        const timePx = this.currentTime * this._pxPerMs;
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
        if (!this._pxPerMs) return 1000;
        const rawMs = 120 / this._pxPerMs;
        const nice = [100, 200, 500, 1000, 2000, 5000, 10000, 30000, 60000, 120000, 300000];
        for (const step of nice) {
            if (step >= rawMs * 0.7) return step;
        }
        return 300000;
    }

    _rulerTime(ms, step) {
        const totalSec = Math.floor(ms / 1000);
        const min = Math.floor(totalSec / 60);
        const sec = totalSec % 60;
        const base = `${String(min).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
        if (step < 1000) {
            const frac = Math.floor((ms % 1000) / 100);
            return `${base}.${frac}`;
        }
        return base;
    }

    // ---- Selection ----

    _selectSegment(trackIdx, segIdx) {
        const old = this._body.querySelector('.tl-segment.selected');
        if (old) old.classList.remove('selected');

        const segEl = this._body.querySelector(
            `.tl-segment[data-track-idx="${trackIdx}"][data-seg-idx="${segIdx}"]`);
        if (segEl) segEl.classList.add('selected');

        const track = this.tracks[trackIdx];
        if (!track?.group) return;
        const layers = track.group.layers;
        if (!layers || segIdx >= layers.length) return;

        if (this.onSelectLayer) {
            this.onSelectLayer({
                layer: layers[segIdx],
                group: track.group,
                trackType: track.type,
                segName: track.segments[segIdx]?.name || '',
            });
        }
    }

    _deselectSegment() {
        const old = this._body.querySelector('.tl-segment.selected');
        if (old) old.classList.remove('selected');
        if (this.onSelectLayer) this.onSelectLayer(null);
    }

    // ---- Strip & Frame Cache ----

    _segKey(dataset) {
        return `${dataset.videoPath}|${dataset.srcStart}|${dataset.srcDuration}`;
    }

    _frameCacheKey(videoPath, timeSec) {
        const rt = Math.round(timeSec * 2) / 2;
        return `${videoPath}@${rt.toFixed(1)}`;
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
            canvas.style.cssText = '';
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
                    await this._fillFilmstrip(video, segEl);
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
            video.src = `file://${filePath}`;
            video.onloadeddata = () => resolve(video);
            video.onerror = () => reject(new Error(`cannot load ${filePath}`));
        });
    }

    async _fillFilmstrip(video, segEl) {
        const stripWrap = segEl.querySelector('.tl-seg-strip');
        if (!stripWrap) return;

        const videoPath = segEl.dataset.videoPath;
        const srcStart = (parseFloat(segEl.dataset.srcStart) || 0) / 1000;
        const srcDur = (parseFloat(segEl.dataset.srcDuration) || 0) / 1000;
        const segW = stripWrap.offsetWidth;
        const segH = stripWrap.offsetHeight;

        if (segW <= 0 || srcDur <= 0 || segH <= 0) return;

        const thumbW = Math.round(segH * THUMB_RATIO);
        const count = Math.max(1, Math.ceil(segW / THUMB_PX));
        const canvasW = count * thumbW;

        const strip = document.createElement('canvas');
        strip.width = canvasW;
        strip.height = segH;
        const ctx = strip.getContext('2d');

        const vw = video.videoWidth;
        const vh = video.videoHeight;
        const vidAspect = vw / vh;
        let sx, sy, sw, sh;
        if (vidAspect > THUMB_RATIO) {
            sh = vh; sw = vh * THUMB_RATIO;
            sx = (vw - sw) / 2; sy = 0;
        } else {
            sw = vw; sh = vw / THUMB_RATIO;
            sx = 0; sy = (vh - sh) / 2;
        }

        for (let i = 0; i < count; i++) {
            const dx = i * thumbW;
            const dw = Math.min(thumbW, canvasW - dx);
            const ratio = dw / thumbW;
            const t = srcStart + srcDur * (i + 0.5) / count;
            const fKey = this._frameCacheKey(videoPath, t);
            const cached = this._frameCache.get(fKey);

            if (cached) {
                ctx.drawImage(cached, 0, 0, cached.width, cached.height, dx, 0, dw, segH);
            } else {
                video.currentTime = Math.min(t, video.duration - 0.01);
                await new Promise(r => { video.onseeked = r; });
                ctx.drawImage(video, sx, sy, sw * ratio, sh, dx, 0, dw, segH);

                const fc = document.createElement('canvas');
                fc.width = thumbW;
                fc.height = segH;
                fc.getContext('2d').drawImage(video, sx, sy, sw, sh, 0, 0, thumbW, segH);
                this._frameCacheStore(fKey, fc);
            }
        }

        strip.style.cssText = 'width:100%;height:100%;display:block;';
        strip.classList.add('tl-thumb-new');
        stripWrap.appendChild(strip);

        const old = stripWrap.querySelector('.tl-thumb-old');
        if (old) old.remove();

        this._stripCache.set(this._segKey(segEl.dataset), strip);
    }
}

module.exports = Timeline;
