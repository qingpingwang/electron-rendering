function formatTimecode(ms, fps = 30) {
    const totalSec = Math.max(0, ms) / 1000;
    const h = Math.floor(totalSec / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    const s = Math.floor(totalSec % 60);
    const f = Math.floor((totalSec % 1) * fps);
    const p = (n) => String(n).padStart(2, '0');
    return `${p(h)}:${p(m)}:${p(s)}:${p(f)}`;
}

function formatTime(ms) {
    return formatTimecode(ms);
}

function log(msg, type = 'info') {
    const el = document.getElementById('log-content');
    const t = new Date().toLocaleTimeString();
    el.innerHTML += `<div class="log-${type}">[${t}] ${msg}</div>`;
    el.scrollTop = el.scrollHeight;
}

module.exports = { log, formatTime, formatTimecode };
