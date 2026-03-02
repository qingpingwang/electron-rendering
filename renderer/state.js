const AudioPlayer = require('./audio/audio_player');

const player = {
    addon: null,
    root: null,
    canvas: null,
    ctx: null,
    currentTime: 0,
    duration: 0,
    frameRate: 0,
    width: 0,
    height: 0,
    frameCount: 0,
    animationId: null,
    audio: new AudioPlayer()
};

module.exports = player;
