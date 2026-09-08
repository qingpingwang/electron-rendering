const player = require('../state');
const KEYS = ['transformX', 'transformY', 'scaleX', 'scaleY', 'rotation'];
const snapshot = layer => Object.fromEntries(KEYS.map(k => [k, layer[k]]));
const center = q => ({ x: (q.tl.x + q.br.x) / 2, y: (q.tl.y + q.br.y) / 2 });
const angle = q => Math.atan2(q.tr.y - q.tl.y, q.tr.x - q.tl.x);
const deltaAngle = a => Math.atan2(Math.sin(a), Math.cos(a));

class CanvasTransform {
    constructor(canvas, onCommit) {
        this.canvas = canvas;
        this.onCommit = onCommit;
        this.info = null;
        this.drag = null;
        this.raf = 0;
        this.svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        this.svg.classList.add('transform-overlay');
        this.svg.setAttribute('aria-label', '画布图层变换');
        canvas.parentElement.appendChild(this.svg);
        this.svg.addEventListener('pointerdown', e => this.start(e));
        this.svg.addEventListener('pointermove', e => this.move(e));
        this.svg.addEventListener('pointerup', e => this.finish(e));
        this.svg.addEventListener('pointercancel', () => this.cancel());
        new ResizeObserver(() => this.refresh()).observe(canvas.parentElement);
    }
    select(info) {
        this.cancel();
        this.info = info;
        this.refresh();
    }
    point(e) {
        const r = this.canvas.getBoundingClientRect();
        return { x: (e.clientX - r.left) * this.canvas.width / r.width, y: (e.clientY - r.top) * this.canvas.height / r.height };
    }
    bounds() {
        const layer = this.info?.layer;
        if (!layer || !layer.visible || player.video.currentTime < layer.startTime || player.video.currentTime >= layer.endTime) {
            return null;
        }
        return player.root.getLayerBounds(layer.id)?.bound;
    }
    refresh() {
        const r = this.canvas.getBoundingClientRect();
        const parent = this.canvas.parentElement.getBoundingClientRect();
        const margin = 40;
        const padding = margin * this.canvas.width / Math.max(1, r.width);
        Object.assign(this.svg.style, { left: `${r.left - parent.left - margin}px`, top: `${r.top - parent.top - margin}px`, width: `${r.width + margin * 2}px`, height: `${r.height + margin * 2}px` });
        this.svg.setAttribute('viewBox', `${-padding} ${-padding} ${this.canvas.width + padding * 2} ${this.canvas.height + padding * 2}`);
        let q;
        try { q = this.bounds(); } catch { this.info = null; }
        this.svg.replaceChildren();
        if (!q) { return; }
        const unit = this.canvas.width / r.width;
        const add = (name, attrs) => {
            const el = document.createElementNS(this.svg.namespaceURI, name);
            Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, v));
            this.svg.appendChild(el);
            return el;
        };
        add('polygon', { points: [q.tl, q.tr, q.br, q.bl].map(p => `${p.x},${p.y}`).join(' '), fill: 'transparent', stroke: '#66ead2', 'stroke-width': 1.5 * unit, 'data-action': 'move' });
        for (const [key, p] of Object.entries(q)) {
            add('circle', { cx: p.x, cy: p.y, r: 4.5 * unit, fill: '#fff', stroke: '#173f37', 'stroke-width': unit, 'data-action': 'scale', 'data-corner': key });
        }
        // Anchor below the layer in its local orientation, so it follows rotation.
        const top = { x: (q.tl.x + q.tr.x) / 2, y: (q.tl.y + q.tr.y) / 2 };
        const bottom = { x: (q.bl.x + q.br.x) / 2, y: (q.bl.y + q.br.y) / 2 };
        const distance = Math.hypot(bottom.x - top.x, bottom.y - top.y) || 1;
        const end = { x: bottom.x + (bottom.x - top.x) / distance * 27 * unit, y: bottom.y + (bottom.y - top.y) / distance * 27 * unit };
        add('circle', { cx: end.x, cy: end.y, r: 10 * unit, fill: '#fff', stroke: '#202225', 'stroke-width': unit, 'data-action': 'rotate' });
        add('path', { d: 'M 5 -2 A 5.4 5.4 0 1 0 4 4 M 5 -6 L 5 -2 L 1 -2', transform: `translate(${end.x} ${end.y}) scale(${unit})`, fill: 'none', stroke: '#252629', 'stroke-width': 1.7, 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'pointer-events': 'none' });
    }
    start(e) {
        if (e.button !== 0 || !player.root?.loaded) { return; }
        e.preventDefault();
        if (player.audio.playing) {
            player.audio.pause();
            player.video.stopRenderLoop();
        }
        const p = this.point(e);
        const action = e.target.dataset.action;
        if (!action || action === 'move') {
            const hit = player.root.hitTest(p.x, p.y);
            if (!hit?.id) { player.timeline._deselectSegment(); return; }
            player.timeline.selectLayer(hit.id);
        }
        const layer = this.info?.layer, q = this.bounds();
        if (!layer || !q) { return; }
        const before = snapshot(layer), c = center(q);
        // Derive translation/rotation axes from SDK bounds, for both GL and text layers.
        const eps = 0.01;
        layer.transformX += eps;
        const dx = center(player.root.getLayerBounds(layer.id).bound);
        layer.transformX = before.transformX;
        layer.transformY += eps;
        const dy = center(player.root.getLayerBounds(layer.id).bound);
        layer.transformY = before.transformY;
        layer.rotation += 1;
        const rotationSign = Math.sign(deltaAngle(angle(player.root.getLayerBounds(layer.id).bound) - angle(q))) || 1;
        layer.rotation = before.rotation;
        this.drag = { pointer: e.pointerId, action: action || 'move', p, c, before, layer, dx: { x: (dx.x - c.x) / eps, y: (dx.y - c.y) / eps }, dy: { x: (dy.x - c.x) / eps, y: (dy.y - c.y) / eps }, rotationSign };
        this.svg.setPointerCapture(e.pointerId);
    }
    move(e) {
        const d = this.drag;
        if (!d || d.pointer !== e.pointerId) { return; }
        const p = this.point(e), layer = d.layer;
        if (d.action === 'move') {
            let x = p.x - d.p.x, y = p.y - d.p.y;
            if (e.shiftKey) { if (Math.abs(x) > Math.abs(y)) { y = 0; } else { x = 0; } }
            const det = d.dx.x * d.dy.y - d.dx.y * d.dy.x;
            if (Math.abs(det) > 0.0001) {
                layer.transformX = d.before.transformX + (x * d.dy.y - y * d.dy.x) / det;
                layer.transformY = d.before.transformY + (y * d.dx.x - x * d.dx.y) / det;
            }
        } else if (d.action === 'rotate') {
            const delta = deltaAngle(Math.atan2(p.y - d.c.y, p.x - d.c.x) - Math.atan2(d.p.y - d.c.y, d.p.x - d.c.x));
            let degrees = d.before.rotation + delta * 180 / Math.PI / d.rotationSign;
            if (e.shiftKey) { degrees = Math.round(degrees / 15) * 15; }
            layer.rotation = degrees;
        } else {
            const ratio = Math.max(0.02, Math.min(20, Math.hypot(p.x - d.c.x, p.y - d.c.y) / Math.max(1, Math.hypot(d.p.x - d.c.x, d.p.y - d.c.y))));
            layer.scaleX = d.before.scaleX * ratio;
            layer.scaleY = d.before.scaleY * ratio;
        }
        if (!this.raf) {
            this.raf = requestAnimationFrame(() => { this.raf = 0; player.video.render(player.video.currentTime, true, false); });
        }
    }
    finish(e) {
        if (!this.drag || this.drag.pointer !== e.pointerId) { return; }
        this.move(e);
        const d = this.drag;
        this.drag = null;
        if (this.svg.hasPointerCapture(e.pointerId)) { this.svg.releasePointerCapture(e.pointerId); }
        this.onCommit(d.layer, d.before, snapshot(d.layer));
    }
    cancel() {
        cancelAnimationFrame(this.raf); this.raf = 0;
        if (!this.drag) { return; }
        const d = this.drag;
        this.drag = null;
        cancelAnimationFrame(this.raf); this.raf = 0;
        Object.assign(d.layer, d.before);
        if (this.svg.hasPointerCapture(d.pointer)) { this.svg.releasePointerCapture(d.pointer); }
        player.video.render(player.video.currentTime, true, false);
    }
}
module.exports = CanvasTransform;
