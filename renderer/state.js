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

/**
 * 项目加载/重载等异步操作串成一条链，避免工具 IPC 在 root.load 完成前执行（否则 loaded 仍为 false）。
 */
let _projectOpChain = Promise.resolve();

player.scheduleProjectOp = function (fn) {
    const run = _projectOpChain.then(() => fn());
    _projectOpChain = run.catch((e) => {
        console.error('[project op]', e);
    });
    return run;
};

/** 工具调用前应 await，确保当前（及之前排队的）项目操作已结束 */
player.whenProjectOpsIdle = function () {
    return _projectOpChain;
};

module.exports = player;
