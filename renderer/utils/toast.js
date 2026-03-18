const TOAST_CSS_ID = '__toast-style';

const TYPES = {
    info:    { bg: 'rgba(78, 205, 196, 0.9)',  color: '#111' },
    success: { bg: 'rgba(78, 205, 130, 0.9)',  color: '#111' },
    warning: { bg: 'rgba(255, 180, 50, 0.9)',  color: '#111' },
    error:   { bg: 'rgba(255, 80, 80, 0.9)',   color: '#fff' },
};

function _ensureStyle() {
    if (document.getElementById(TOAST_CSS_ID)) return;
    const style = document.createElement('style');
    style.id = TOAST_CSS_ID;
    style.textContent = `
        #toast-container {
            position: fixed;
            top: 24px;
            left: 50%;
            transform: translateX(-50%);
            z-index: 10000;
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 8px;
            pointer-events: none;
        }
        .toast {
            padding: 10px 20px;
            font-size: 13px;
            border-radius: 8px;
            backdrop-filter: blur(8px);
            opacity: 0;
            transform: translateY(-8px);
            transition: opacity 0.25s, transform 0.25s;
            pointer-events: auto;
            max-width: 420px;
            text-align: center;
            line-height: 1.4;
        }
        .toast.show {
            opacity: 1;
            transform: translateY(0);
        }
    `;
    document.head.appendChild(style);
}

function _getContainer() {
    _ensureStyle();
    let container = document.getElementById('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        document.body.appendChild(container);
    }
    return container;
}

/**
 * @param {string} message
 * @param {'info'|'success'|'warning'|'error'} [type='info']
 * @param {number} [duration=3000]
 */
function show(message, type = 'info', duration = 3000) {
    const container = _getContainer();
    const t = TYPES[type] || TYPES.info;

    const el = document.createElement('div');
    el.className = 'toast';
    el.style.background = t.bg;
    el.style.color = t.color;
    el.textContent = message;
    container.appendChild(el);

    requestAnimationFrame(() => el.classList.add('show'));

    setTimeout(() => {
        el.classList.remove('show');
        el.addEventListener('transitionend', () => el.remove());
    }, duration);
}

module.exports = { show };
