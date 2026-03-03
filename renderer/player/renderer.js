const player = require('../state');
const { formatTimecode } = require('../utils/logger');

function updateUI() {
    const $ = id => document.getElementById(id);
    const v = player.video;

    $('btn-play').disabled = !v || v.duration === 0;
    $('btn-play').innerHTML = player.audio.playing ? '&#9646;&#9646;' : '&#9654;';

    const fps = v ? v.frameRate : 30;
    $('time-current').textContent = formatTimecode(v ? v.currentTime : 0, fps);
    $('time-total').textContent = formatTimecode(v ? v.duration : 0, fps);
}

module.exports = { updateUI };
