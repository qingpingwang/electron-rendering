const { formatTime } = require('../utils/logger');
const PC = require('./prop_controls');

const TYPE_NAMES = { video: '视频', text: '文本', audio: '音频' };

class Inspector {
    constructor(container) {
        this.el = container;
        this._info = null;
        this._controls = [];
        this._runCtrls = [];
        this._runsEl = null;
        this.onChange = null;
    }

    update(info) {
        this._destroyControls();
        this._info = info;
        if (!info) { this.clear(); return; }

        const { layer, trackType, segName, segment, group } = info;
        this.el.innerHTML = '';

        this._appendHTML(sectionHTML('基本信息'));
        this._appendHTML(rowHTML('类型', `<span class="insp-badge ${trackType}">${TYPE_NAMES[trackType] || trackType}</span>`));
        const startTime = layer
            ? safeGet(() => layer.startTime, 0)
            : (segment?.start || 0);
        const duration = layer
            ? safeGet(() => layer.durationMs, 0)
            : (segment?.duration || 0);
        this._appendHTML(rowHTML('名称', esc(layer ? (safeGet(() => layer.id, '') || segName) : (segName || '音频片段'))));
        this._appendHTML(rowHTML('时间', `${formatTime(startTime)} ~ ${formatTime(startTime + duration)}`));
        this._appendHTML(rowHTML('时长', formatTime(duration)));

        this._appendHTML(sectionHTML('属性'));
        if (layer) {
            this._addToggle('可见', !!safeGet(() => layer.visible, true), v => { layer.visible = v; this._emit('visible', v); });
        }

        if (trackType === 'video') {
            if (layer.videoFrameRate)
                this._appendHTML(rowHTML('帧率', `${layer.videoFrameRate.toFixed(1)} fps`));
            this._buildSRT(layer);
        }
        if (trackType === 'text') {
            this._buildText(layer);
            this._buildSRT(layer);
        }
        if (trackType === 'audio') {
            this._appendHTML(rowHTML('文件', esc(layer ? safeGet(() => layer.audioName, '') : (segName || ''))));
            const muted = layer ? !!safeGet(() => layer.muted, false) : !!group?.muted;
            if (layer) {
                this._addToggle('静音', muted, v => { layer.muted = v; this._emit('muted', v); });
            } else {
                this._appendHTML(rowHTML('静音', muted ? '是' : '否'));
            }
            const volume = layer
                ? Number(safeGet(() => layer.volume, 1))
                : Number(segment?.volume ?? 1);
            this._appendHTML(rowHTML('音量', `${(Math.max(0, volume) * 100).toFixed(0)}%`));
        }

        this._appendHTML(sectionHTML('源片段'));
        const srcStart = layer ? safeGet(() => layer.sourceStart, 0) : (segment?.srcStart || 0);
        const srcDur = layer ? safeGet(() => layer.sourceDuration, 0) : (segment?.srcDuration || duration);
        this._appendHTML(rowHTML('起始', formatTime(srcStart)));
        this._appendHTML(rowHTML('时长', formatTime(srcDur)));

        if (!layer && trackType === 'audio') {
            this._appendHTML(rowHTML('提示', '音频轨道使用安全模式展示，避免原生层崩溃'));
            return;
        }
    }

    clear() {
        this._destroyControls();
        this._info = null;
        this.el.innerHTML = '<div class="insp-empty">未选择图层</div>';
    }

    /** 当前选中的图层上下文（供外部在数据被工具等修改后刷新面板） */
    getCurrentInfo() {
        return this._info;
    }

    _buildSRT(layer) {
        this.el.appendChild(PC.section('画面变换'));

        this._addGroupedSliders('缩放', [
            { label: 'X', value: layer.scaleX ?? 1, min: 0.01, max: 5, step: 0.01, precision: 2,
                onChange: v => { layer.scaleX = v; this._emit('scaleX', v); } },
            { label: 'Y', value: layer.scaleY ?? 1, min: 0.01, max: 5, step: 0.01, precision: 2,
                onChange: v => { layer.scaleY = v; this._emit('scaleY', v); } },
        ]);

        this._addGroupedSliders('位移', [
            { label: 'X', value: layer.transformX ?? 0, min: -2, max: 2, step: 0.01, precision: 2,
                onChange: v => { layer.transformX = v; this._emit('transformX', v); } },
            { label: 'Y', value: layer.transformY ?? 0, min: -2, max: 2, step: 0.01, precision: 2,
                onChange: v => { layer.transformY = v; this._emit('transformY', v); } },
        ]);

        this._addGroupedSliders('旋转', [
            { label: '角度', value: layer.rotation ?? 0, min: -360, max: 360, step: 0.1, precision: 1, unit: '°',
                onChange: v => { layer.rotation = v; this._emit('rotation', v); } },
        ]);

        this._addGroupedSliders('透明度', [
            { label: '值', value: layer.alpha ?? 1, min: 0, max: 1, step: 0.01, precision: 2,
                onChange: v => { layer.alpha = v; this._emit('alpha', v); } },
        ]);
    }

    _buildText(layer) {
        this.el.appendChild(PC.section('文字内容'));

        let renderRAF = 0;
        const ta = PC.textarea({
            label: '', value: layer.text || '', rows: 3,
            onChange: v => {
                layer.text = v;
                this._refreshTextRuns(layer);
                cancelAnimationFrame(renderRAF);
                renderRAF = requestAnimationFrame(() => this._emit('text', v));
            },
        });
        this._controls.push(ta);
        this.el.appendChild(ta.el);

        this._addCtrl(PC.buttonGroup({
            label: '对齐',
            options: [
                { value: 'left', label: '左', title: '左对齐' },
                { value: 'center', label: '中', title: '居中对齐' },
                { value: 'right', label: '右', title: '右对齐' },
            ],
            value: layer.alignment || 'left',
            onChange: v => { layer.alignment = v; this._emit('alignment', v); },
        }));

        this._runsEl = document.createElement('div');
        this._lastRunCount = 0;
        this.el.appendChild(this._runsEl);
        this._renderTextRuns(layer);
    }

    _refreshTextRuns(layer) {
        const newCount = layer.styleRunCount || 0;

        if (newCount === this._lastRunCount && newCount > 0) {
            const sections = this._runsEl.querySelectorAll('.insp-section');
            for (let i = 0; i < newCount && i < sections.length; i++) {
                const run = layer.getStyleRun(i);
                if (!run) continue;
                sections[i].textContent = newCount > 1
                    ? `样式 ${i + 1}（字${run.rangeStart}–${run.rangeEnd}）`
                    : '文字样式';
            }
            return;
        }

        for (const c of this._runCtrls) { if (c.destroy) c.destroy(); }
        this._runCtrls = [];
        this._runsEl.innerHTML = '';
        this._renderTextRuns(layer);
    }

    _renderTextRuns(layer) {
        const runCount = layer.styleRunCount || 0;
        this._lastRunCount = runCount;
        if (runCount === 0) return;

        for (let i = 0; i < runCount; i++) {
            const run = layer.getStyleRun(i);
            if (!run) continue;

            const runLabel = runCount > 1 ? `样式 ${i + 1}（字${run.rangeStart}–${run.rangeEnd}）` : '文字样式';
            this._runsEl.appendChild(PC.section(runLabel));

            this._addRunCtrl(PC.slider({
                label: '字号', value: run.fontSize, min: 1, max: 500, step: 1, precision: 0, unit: '',
                onChange: v => { layer.setStyleRunFontSize(i, v); this._emit('fontSize', v); },
            }));

            this._addRunCtrl(PC.colorInput({
                label: '填充色', r: run.fill.r, g: run.fill.g, b: run.fill.b, a: run.fill.a,
                onChange: (r, g, b, a) => { layer.setStyleRunFill(i, r, g, b, a); this._emit('fill'); },
            }));

            this._addRunGroupedSliders('间距', [
                { label: '字距', value: run.letterSpacing, min: -50, max: 200, step: 0.5, precision: 1,
                    onChange: v => { layer.setStyleRunLetterSpacing(i, v); this._emit('letterSpacing', v); } },
                { label: '行高', value: run.lineHeight, min: 0.5, max: 5, step: 0.05, precision: 2,
                    onChange: v => { layer.setStyleRunLineHeight(i, v); this._emit('lineHeight', v); } },
            ]);

            if (run.strokes && run.strokes.length > 0) {
                for (let si = 0; si < run.strokes.length; si++) {
                    const st = run.strokes[si];
                    const g = PC.group(`描边 ${si + 1}`);
                    this._runsEl.appendChild(g.el);

                    const wCtrl = PC.slider({
                        label: '宽度', value: st.width, min: 0, max: 0.5, step: 0.005, precision: 3,
                        onChange: v => { layer.setStyleRunStrokeWidth(i, si, v); this._emit('strokeWidth', v); },
                    });
                    this._runCtrls.push(wCtrl);
                    g.body.appendChild(wCtrl.el);

                    const cCtrl = PC.colorInput({
                        label: '颜色', r: st.r, g: st.g, b: st.b, a: st.a,
                        showAlpha: false,
                        onChange: (r, gb, b, a) => { layer.setStyleRunStrokeColor(i, si, r, gb, b, a); this._emit('strokeColor'); },
                    });
                    this._runCtrls.push(cCtrl);
                    g.body.appendChild(cCtrl.el);
                }
            }
        }
    }

    _addRunCtrl(ctrl) {
        this._runCtrls.push(ctrl);
        this._runsEl.appendChild(ctrl.el);
    }

    _addRunGroupedSliders(title, sliderConfigs) {
        const g = PC.group(title);
        this._runsEl.appendChild(g.el);
        for (const cfg of sliderConfigs) {
            const ctrl = PC.slider(cfg);
            this._runCtrls.push(ctrl);
            g.body.appendChild(ctrl.el);
        }
    }

    _addGroupedSliders(title, sliderConfigs) {
        const g = PC.group(title);
        this.el.appendChild(g.el);
        for (const cfg of sliderConfigs) {
            const ctrl = PC.slider(cfg);
            this._controls.push(ctrl);
            g.body.appendChild(ctrl.el);
        }
    }

    _addCtrl(ctrl) {
        this._controls.push(ctrl);
        this.el.appendChild(ctrl.el);
    }

    _addToggle(label, checked, onChange) {
        const ctrl = PC.toggle({ label, checked, onChange });
        this._controls.push(ctrl);
        this.el.appendChild(ctrl.el);
    }

    _appendHTML(html) {
        const tpl = document.createElement('template');
        tpl.innerHTML = html;
        this.el.appendChild(tpl.content);
    }

    _emit(prop, value) {
        if (this.onChange) this.onChange(prop, value);
    }

    _destroyControls() {
        for (const c of this._controls) { if (c.destroy) c.destroy(); }
        this._controls = [];
        for (const c of this._runCtrls) { if (c.destroy) c.destroy(); }
        this._runCtrls = [];
        this._runsEl = null;
    }
}

function sectionHTML(title) {
    return `<div class="insp-section">${title}</div>`;
}

function rowHTML(label, value) {
    return `<div class="insp-row"><span class="insp-label">${label}</span><span class="insp-value">${value}</span></div>`;
}

function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;');
}

function f01(v) { return Math.round(v * 255); }

function safeGet(fn, fallback) {
    try {
        const v = fn();
        return v === undefined || v === null ? fallback : v;
    } catch (e) {
        return fallback;
    }
}

module.exports = Inspector;
