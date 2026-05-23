/** 时:分:秒.毫秒（不展示帧） */
function formatTimeHMS(ms) {
    const t = Math.max(0, Math.floor(Number(ms) || 0));
    const h = Math.floor(t / 3600000);
    const m = Math.floor((t % 3600000) / 60000);
    const s = Math.floor((t % 60000) / 1000);
    const milli = t % 1000;
    const p2 = (n) => String(n).padStart(2, '0');
    const p3 = (n) => String(n).padStart(3, '0');
    return `${p2(h)}:${p2(m)}:${p2(s)}.${p3(milli)}`;
}

/** 时:分:秒:帧（需要帧号时用） */
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
    return formatTimeHMS(ms);
}

function log(msg, type = 'info') {
    const el = document.getElementById('log-content');
    if (!el) return;
    const t = new Date().toLocaleTimeString();
    el.innerHTML += `<div class="log-${type}">[${t}] ${msg}</div>`;
    el.scrollTop = el.scrollHeight;
}

module.exports = { log, formatTime, formatTimeHMS, formatTimecode };
