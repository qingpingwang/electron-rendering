const { log, formatTime } = require('../utils/logger');

class VideoPlayer {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');

        this.root = null;
        this.width = 0;
        this.height = 0;
        this.duration = 0;
        this.frameRate = 0;
        this.currentTime = 0;
        this.frameCount = 0;
        this._animId = null;
        this.onRender = null;
    }

    get loaded() {
        return this.root && this.root.loaded;
    }

    load(root) {
        this.root = root;
        this.width = root.width;
        this.height = root.height;
        this.duration = root.durationMs;
        this.frameRate = root.frameRate;
        this.currentTime = 0;
        this.frameCount = 0;

        if (this.canvas.width !== this.width || this.canvas.height !== this.height) {
            this.canvas.width = this.width;
            this.canvas.height = this.height;
        }
    }

    render(timeMs, force = false, prepareNext = true) {
        if (!this.root) return;
        if (timeMs !== undefined) this.currentTime = timeMs;

        const t0 = performance.now();

        this.root.setCurrentTime(this._snapToFrame(this.currentTime));
        const result = this.root.draw(force, prepareNext);

        const t1 = performance.now();

        if (!result) return;

        const { pixels, status } = result;

        if (this.canvas.width !== this.width || this.canvas.height !== this.height) {
            this.canvas.width = this.width;
            this.canvas.height = this.height;
        }

        const imageData = new ImageData(
            new Uint8ClampedArray(pixels.buffer, pixels.byteOffset, pixels.length),
            this.width,
            this.height
        );
        this.ctx.putImageData(imageData, 0, 0);

        this.frameCount++;

        const renderTime = t1 - t0;
        const totalTime = performance.now() - t0;
        const tag = status === 0 ? '缓存' : '渲染';
        log(`#${this.frameCount} | ${formatTime(this.currentTime)} | ${tag} ${renderTime.toFixed(2)}ms | 总: ${totalTime.toFixed(2)}ms`, 'info');

        if (this.onRender) this.onRender();
    }

    isSameFrame(timeMs) {
        if (!this.root) return false;
        return this.root.isSameFrame(this._snapToFrame(timeMs));
    }

    startRenderLoop(getTimeMs) {
        this.stopRenderLoop();

        const tick = () => {
            const timeMs = getTimeMs();
            if (timeMs === null) {
                this._animId = null;
                return;
            }

            const needStop = timeMs >= this.duration;
            const t = needStop ? this.duration - 1 : timeMs;

            if (!this.isSameFrame(t)) {
                this.render(t);
            }

            if (needStop) {
                this._animId = null;
                return;
            }

            this._animId = requestAnimationFrame(tick);
        };
        this._animId = requestAnimationFrame(tick);
    }

    stopRenderLoop() {
        if (this._animId) {
            cancelAnimationFrame(this._animId);
            this._animId = null;
        }
    }

    _snapToFrame(timeMs) {
        if (this.frameRate <= 0) return Math.floor(timeMs);
        const frameMs = 1000 / this.frameRate;
        return Math.round(timeMs / frameMs) * frameMs;
    }
}

module.exports = VideoPlayer;
