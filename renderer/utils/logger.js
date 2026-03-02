function formatTime(ms) {
    const sec = Math.floor(ms / 1000);
    const min = Math.floor(sec / 60);
    const s = sec % 60;
    const m = Math.floor(ms % 1000);
    return `${String(min).padStart(2, '0')}:${String(s).padStart(2, '0')}.${String(m).padStart(3, '0')}`;
}

function log(msg, type = 'info') {
    const el = document.getElementById('log-content');
    const t = new Date().toLocaleTimeString();
    el.innerHTML += `<div class="log-${type}">[${t}] ${msg}</div>`;
    el.scrollTop = el.scrollHeight;
}

module.exports = { log, formatTime };
