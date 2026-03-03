const AudioPlayer = require('./audio/audio_player');
const VideoPlayer = require('./video/video_player');
const Timeline = require('./timeline/timeline');

const player = {
    addon: null,
    root: null,
    video: null,
    audio: new AudioPlayer(),
    timeline: null,
};

player.initCanvas = function (canvas) {
    player.video = new VideoPlayer(canvas);
};

player.initTimeline = function (container) {
    player.timeline = new Timeline(container);
};

module.exports = player;
