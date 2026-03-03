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

    get currentTimeMs() {
        if (!this.playing) return this._startOffset;
        const elapsed = (this.ctx.currentTime - this._playStartCtxTime) * 1000;
        return Math.min(this._startOffset + elapsed, this.duration);
    }

    async load(root) {
        this.stop();
        this.tracks.clear();
        this.duration = root.durationMs;

        const audioInfos = root.getAudioInfos();
        const loadPromises = [];

        for (const [layerId, info] of Object.entries(audioInfos)) {
            if (info.volume <= 0) continue;

            const filePath = path.resolve(info.path);
            loadPromises.push(
                this._decodeFile(filePath).then(buffer => {
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
        this._startOffset = this.currentTimeMs;
        this._stopSources();
        this.playing = false;
    }

    seek(timeMs) {
        const wasPlaying = this.playing;
        if (this.playing) {
            this._stopSources();
            this.playing = false;
        }
        this._startOffset = Math.max(0, Math.min(timeMs, this.duration));
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
        const fileData = fs.readFileSync(filePath);
        const ab = fileData.buffer.slice(
            fileData.byteOffset,
            fileData.byteOffset + fileData.byteLength
        );
        return this.ctx.decodeAudioData(ab);
    }

    _startSources(fromMs) {
        this._stopSources();
        this._playStartCtxTime = this.ctx.currentTime;

        for (const [, track] of this.tracks) {
            const { buffer, volume, info, groupId, muted } = track;
            const tStart = info.targetRange.start;
            const tDur = info.targetRange.duration;
            const tEnd = tStart + tDur;

            if (fromMs >= tEnd) continue;

            const source = this.ctx.createBufferSource();
            source.buffer = buffer;

            const gain = this.ctx.createGain();
            gain.gain.value = muted ? 0 : volume;
            source.connect(gain);
            gain.connect(this.masterGain);

            const srcStartSec = info.sourceRange.start / 1000;
            const srcDurSec = info.sourceRange.duration / 1000;

            if (fromMs <= tStart) {
                const delaySec = (tStart - fromMs) / 1000;
                source.start(this.ctx.currentTime + delaySec, srcStartSec, srcDurSec);
            } else {
                const progress = (fromMs - tStart) / tDur;
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
