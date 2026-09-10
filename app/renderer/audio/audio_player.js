const fs = require('fs');
const path = require('path');

class AudioPlayer {
    constructor() {
        this.ctx = new AudioContext();
        this.masterGain = this.ctx.createGain();
        this.masterGain.connect(this.ctx.destination);

        this.tracks = new Map();
        this.activeSources = [];

        this.playing = false;
        this._startOffset = 0;
        this._playStartCtxTime = 0;
        this.duration = 0;
    }

    get currentTimeUs() {
        if (!this.playing) return this._startOffset;
        const elapsed = (this.ctx.currentTime - this._playStartCtxTime) * 1000000;
        return Math.min(this._startOffset + elapsed, this.duration);
    }

    async load(root, proxies = {}) {
        this.stop();
        this.tracks.clear();
        this.duration = root.durationUs;

        const audioInfos = root.getAudioInfos();
        const loadPromises = [];
        const decoded = new Map();

        for (const [layerId, info] of Object.entries(audioInfos)) {
            if (info.volume <= 0) continue;

            const proxy = info.layerType === 'video' ? proxies[path.resolve(info.path)] : null;
            if (proxy && !proxy.audio) { continue; }
            const filePath = proxy ? proxy.audio : path.resolve(info.path);
            if (!decoded.has(filePath)) { decoded.set(filePath, this._decodeFile(filePath)); }
            loadPromises.push(
                decoded.get(filePath).then(buffer => {
                    if (buffer) {
                        this.tracks.set(layerId, {
                            buffer, volume: info.volume, info,
                            groupId: info.groupId, muted: false,
                        });
                    }
                }).catch(e => {
                    console.warn(`[AudioPlayer] skip ${layerId}: ${e.message}`);
                })
            );
        }

        await Promise.all(loadPromises);
        return this.tracks.size;
    }

    async play() {
        if (this.playing) return;
        if (this.ctx.state === 'suspended') await this.ctx.resume();
        if (this._startOffset >= this.duration) this._startOffset = 0;

        this._startSources(this._startOffset);
        this.playing = true;
    }

    pause() {
        if (!this.playing) return;
        this._startOffset = this.currentTimeUs;
        this._stopSources();
        this.playing = false;
    }

    seek(timeUs) {
        const wasPlaying = this.playing;
        if (this.playing) {
            this._stopSources();
            this.playing = false;
        }
        this._startOffset = Math.max(0, Math.min(timeUs, this.duration));
        if (wasPlaying) this.play();
    }

    stop() {
        this._stopSources();
        this._startOffset = 0;
        this.playing = false;
    }

    dispose() {
        this.stop();
        this.tracks.clear();
        this.ctx.close();
    }

    muteGroup(groupId, muted) {
        for (const [, track] of this.tracks) {
            if (track.groupId !== groupId) continue;
            track.muted = muted;
        }
        for (const src of this.activeSources) {
            if (src.groupId === groupId) {
                src.gain.gain.value = muted ? 0 : src.volume;
            }
        }
    }

    // ========== internal ==========

    async _decodeFile(filePath) {
        const fileData = await fs.promises.readFile(filePath);
        const ab = fileData.buffer.slice(
            fileData.byteOffset,
            fileData.byteOffset + fileData.byteLength
        );
        return this.ctx.decodeAudioData(ab);
    }

    _startSources(fromUs) {
        this._stopSources();
        this._playStartCtxTime = this.ctx.currentTime;

        for (const [, track] of this.tracks) {
            const { buffer, volume, info, groupId, muted } = track;
            const tStart = info.targetRange.start;
            const tDur = info.targetRange.duration;
            const tEnd = tStart + tDur;

            if (fromUs >= tEnd) continue;

            const source = this.ctx.createBufferSource();
            source.buffer = buffer;

            const gain = this.ctx.createGain();
            gain.gain.value = muted ? 0 : volume;
            source.connect(gain);
            gain.connect(this.masterGain);

            const srcStartSec = info.sourceRange.start / 1000000;
            const srcDurSec = info.sourceRange.duration / 1000000;

            if (fromUs <= tStart) {
                const delaySec = (tStart - fromUs) / 1000000;
                source.start(this.ctx.currentTime + delaySec, srcStartSec, srcDurSec);
            } else {
                const progress = (fromUs - tStart) / tDur;
                const offsetSec = srcStartSec + progress * srcDurSec;
                const remainSec = srcDurSec * (1 - progress);
                source.start(0, offsetSec, Math.max(0, remainSec));
            }

            this.activeSources.push({ source, gain, groupId, volume });
        }
    }

    _stopSources() {
        for (const { source, gain } of this.activeSources) {
            try { source.stop(); } catch (e) { /* already stopped */ }
            source.disconnect();
            gain.disconnect();
        }
        this.activeSources = [];
    }
}

module.exports = AudioPlayer;
