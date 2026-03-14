/**
 * Reusable property control components for the inspector panel.
 */
const Pickr = require('@simonwep/pickr');

const PropControls = {

    section(title) {
        const el = document.createElement('div');
        el.className = 'insp-section';
        el.textContent = title;
        return el;
    },

    slider({ label, value, min, max, step = 0.01, precision = 2, unit = '', onChange }) {
        const el = document.createElement('div');
        el.className = 'prop-row';

        const labelEl = document.createElement('span');
        labelEl.className = 'prop-label';
        labelEl.textContent = label;

        const track = document.createElement('div');
        track.className = 'prop-track';

        const fill = document.createElement('div');
        fill.className = 'prop-fill';

        const thumb = document.createElement('div');
        thumb.className = 'prop-thumb';

        track.appendChild(fill);
        track.appendChild(thumb);

        const valEl = document.createElement('input');
        valEl.className = 'prop-val';
        valEl.type = 'text';
        valEl.value = fmtVal(value, precision, unit);

        el.appendChild(labelEl);
        el.appendChild(track);
        el.appendChild(valEl);

        let currentValue = value;

        function updateVisual(v) {
            const ratio = (v - min) / (max - min);
            fill.style.width = `${(ratio * 100).toFixed(1)}%`;
            thumb.style.left = `${(ratio * 100).toFixed(1)}%`;
        }

        function commit(v) {
            currentValue = clamp(v, min, max);
            currentValue = roundTo(currentValue, step);
            updateVisual(currentValue);
            valEl.value = fmtVal(currentValue, precision, unit);
            if (onChange) onChange(currentValue);
        }

        updateVisual(value);

        // Drag on track
        let dragging = false;
        const dragStart = (e) => {
            e.preventDefault();
            dragging = true;
            document.body.classList.add('prop-dragging');
            dragMove(e);
        };
        const dragMove = (e) => {
            if (!dragging) return;
            const rect = track.getBoundingClientRect();
            const ratio = clamp((e.clientX - rect.left) / rect.width, 0, 1);
            commit(min + ratio * (max - min));
        };
        const dragEnd = () => {
            dragging = false;
            document.body.classList.remove('prop-dragging');
        };

        track.addEventListener('mousedown', dragStart);
        document.addEventListener('mousemove', dragMove);
        document.addEventListener('mouseup', dragEnd);

        // Direct input editing
        valEl.addEventListener('focus', () => {
            valEl.value = String(currentValue);
            valEl.select();
        });
        valEl.addEventListener('blur', () => {
            const parsed = parseFloat(valEl.value);
            if (!isNaN(parsed)) commit(parsed);
            else valEl.value = fmtVal(currentValue, precision, unit);
        });
        valEl.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') valEl.blur();
            if (e.key === 'Escape') {
                valEl.value = fmtVal(currentValue, precision, unit);
                valEl.blur();
            }
        });

        return {
            el,
            getValue() { return currentValue; },
            setValue(v) {
                currentValue = clamp(v, min, max);
                updateVisual(currentValue);
                valEl.value = fmtVal(currentValue, precision, unit);
            },
            destroy() {
                document.removeEventListener('mousemove', dragMove);
                document.removeEventListener('mouseup', dragEnd);
            },
        };
    },

    toggle({ label, checked, onChange }) {
        const el = document.createElement('div');
        el.className = 'prop-row';

        const labelEl = document.createElement('span');
        labelEl.className = 'prop-label';
        labelEl.textContent = label;

        const valWrap = document.createElement('span');
        valWrap.className = 'prop-val-wrap';

        const input = document.createElement('input');
        input.type = 'checkbox';
        input.className = 'insp-toggle';
        input.checked = !!checked;
        input.addEventListener('change', () => { if (onChange) onChange(input.checked); });

        valWrap.appendChild(input);
        el.appendChild(labelEl);
        el.appendChild(valWrap);

        return {
            el,
            getValue() { return input.checked; },
            setValue(v) { input.checked = v; },
        };
    },

    readonlyRow(label, value) {
        const el = document.createElement('div');
        el.className = 'prop-row prop-readonly';

        const labelEl = document.createElement('span');
        labelEl.className = 'prop-label';
        labelEl.textContent = label;

        const valEl = document.createElement('span');
        valEl.className = 'prop-val-text';
        valEl.textContent = value;

        el.appendChild(labelEl);
        el.appendChild(valEl);
        return { el, setValue(v) { valEl.textContent = v; } };
    },

    textarea({ label, value, rows = 2, onChange }) {
        const el = document.createElement('div');
        el.className = 'prop-block';

        if (label) {
            const labelEl = document.createElement('span');
            labelEl.className = 'prop-block-label';
            labelEl.textContent = label;
            el.appendChild(labelEl);
        }

        const ta = document.createElement('textarea');
        ta.className = 'prop-textarea';
        ta.rows = rows;
        ta.value = value || '';
        ta.spellcheck = false;

        ta.addEventListener('input', () => { if (onChange) onChange(ta.value); });

        el.appendChild(ta);
        return {
            el,
            getValue() { return ta.value; },
            setValue(v) { ta.value = v; },
        };
    },

    buttonGroup({ label, options, value, onChange }) {
        const el = document.createElement('div');
        el.className = 'prop-row';

        const labelEl = document.createElement('span');
        labelEl.className = 'prop-label';
        labelEl.textContent = label;

        const group = document.createElement('div');
        group.className = 'prop-btn-group';

        let current = value;
        const buttons = [];

        for (const opt of options) {
            const btn = document.createElement('button');
            btn.className = 'prop-btn' + (opt.value === current ? ' active' : '');
            btn.textContent = opt.label;
            btn.title = opt.title || opt.label;
            btn.addEventListener('click', () => {
                current = opt.value;
                buttons.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                if (onChange) onChange(current);
            });
            buttons.push(btn);
            group.appendChild(btn);
        }

        el.appendChild(labelEl);
        el.appendChild(group);
        return {
            el,
            getValue() { return current; },
            setValue(v) {
                current = v;
                buttons.forEach((b, i) => b.classList.toggle('active', options[i].value === v));
            },
        };
    },

    colorInput({ label, r, g, b, a = 1, showAlpha = true, onChange }) {
        const el = document.createElement('div');
        el.className = 'prop-row';

        const labelEl = document.createElement('span');
        labelEl.className = 'prop-label';
        labelEl.textContent = label;

        const wrap = document.createElement('div');
        wrap.className = 'prop-color-wrap';

        const swatch = document.createElement('div');
        swatch.className = 'prop-color-swatch';

        const hexDisplay = document.createElement('span');
        hexDisplay.className = 'prop-color-hex-text';

        wrap.appendChild(swatch);
        wrap.appendChild(hexDisplay);
        el.appendChild(labelEl);
        el.appendChild(wrap);

        let cur = { r, g, b, a: showAlpha ? a : 1 };
        let pickr = null;

        function rgbaStr() {
            return `rgba(${Math.round(cur.r * 255)}, ${Math.round(cur.g * 255)}, ${Math.round(cur.b * 255)}, ${cur.a})`;
        }

        function hexStr() {
            return '#' + [cur.r, cur.g, cur.b].map(v => Math.round(v * 255).toString(16).padStart(2, '0')).join('');
        }

        function updateDisplay() {
            swatch.style.background = rgbaStr();
            hexDisplay.textContent = showAlpha
                ? `${hexStr()} ${Math.round(cur.a * 100)}%`
                : hexStr();
        }

        updateDisplay();

        swatch.addEventListener('click', () => {
            if (pickr) return; // Pickr's useAsButton handles subsequent toggles
            pickr = Pickr.create({
                el: swatch,
                theme: 'nano',
                container: document.body,
                useAsButton: true,
                default: rgbaStr(),
                components: {
                    preview: true,
                    opacity: showAlpha,
                    hue: true,
                    interaction: {
                        hex: true,
                        rgba: showAlpha,
                        input: true,
                        save: true,
                    },
                },
            });

            pickr.on('change', (color) => {
                const rgba = color.toRGBA();
                cur.r = rgba[0] / 255;
                cur.g = rgba[1] / 255;
                cur.b = rgba[2] / 255;
                if (showAlpha) cur.a = rgba[3];
                updateDisplay();
                if (onChange) onChange(cur.r, cur.g, cur.b, cur.a);
            });

            pickr.on('save', () => pickr.hide());

            pickr.show();
        });

        return {
            el,
            getValue() { return { ...cur }; },
            setValue(nr, ng, nb, na) {
                cur = { r: nr, g: ng, b: nb, a: showAlpha ? (na ?? cur.a) : 1 };
                updateDisplay();
                if (pickr) pickr.setColor(rgbaStr(), true);
            },
            destroy() {
                if (pickr) pickr.destroyAndRemove();
            },
        };
    },

    group(title) {
        const el = document.createElement('div');
        el.className = 'prop-group';
        const header = document.createElement('div');
        header.className = 'prop-group-title';
        header.textContent = title;
        el.appendChild(header);
        const body = document.createElement('div');
        body.className = 'prop-group-body';
        el.appendChild(body);
        return { el, body };
    },
};

function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
function roundTo(v, step) { return Math.round(v / step) * step; }
function fmtVal(v, precision, unit) { return v.toFixed(precision) + unit; }

module.exports = PropControls;
