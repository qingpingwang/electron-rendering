const player = require('../state');
const { formatTimeHMS } = require('../utils/logger');

function updateUI() {
    const $ = id => document.getElementById(id);
    const v = player.video;

    $('btn-play').disabled = !v || v.duration === 0;
    $('btn-play').innerHTML = player.audio.playing ? '&#9646;&#9646;' : '&#9654;';

    $('time-current').textContent = formatTimeHMS(v ? v.currentTime : 0);
    $('time-total').textContent = formatTimeHMS(v ? v.duration : 0);
}

module.exports = { updateUI };
