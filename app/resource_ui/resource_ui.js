/**
 * 资源制作窗口主逻辑：左栏沙箱列表、预览播放器、日志面板、三栏分割条。
 */
const path = require('path');
const fs   = require('fs');

const { getSandboxRoot } = require('../agent/sandbox');
const { PREVIEW_PROTOCOL, resolvePreviewPaths } = require('./preview_protocol');
const VideoPlayer                      = require('../renderer/video/video_player');

const APP_DIR      = path.join(__dirname, '..');
const PROJECT_ROOT = path.join(APP_DIR, '..');

// ─────────────────────────────────────────────
// 预览播放器状态
// ─────────────────────────────────────────────
const preview = {
    addon:   null,
    root:    null,
    video:   null,
    playing: false,
    _startWall: 0,
    _startMs:   0,
    _animId:    null,
};

// ─────────────────────────────────────────────
// 挂载状态
// ─────────────────────────────────────────────
let _mountedEffect     = null;   // { id, name, path, type }
let _mountedTransition = null;   // { id, name, path, type, duration }
let _effectFolder      = null;   // 当前选中 effect 所在目录
let _transitionFolder  = null;   // 当前选中 transition 所在目录
// format 未知时用户在 UI 选择的结果（folder → 'effect'|'transition'）
const _formatChoices   = {};

// ─────────────────────────────────────────────
// DOM 引用
// ─────────────────────────────────────────────
const canvasEl        = document.getElementById('preview-canvas');
const playBtn         = document.getElementById('btn-play-preview');
const timeDisplay     = document.getElementById('time-display');
const seekBar         = document.getElementById('seek-bar');
const statusEl        = document.getElementById('preview-status');
const placeholderEl   = document.getElementById('preview-placeholder');
const resourceListEl  = document.getElementById('resource-list');
const resTitleEl      = document.getElementById('res-title');
const resourceLabelEl = document.getElementById('preview-resource-label');
const refreshBtn      = document.getElementById('btn-refresh-list');
const paramPanelEl    = document.getElementById('param-panel');

// 日志面板
const logBodyEl  = document.getElementById('log-body');
const logPanel   = document.getElementById('log-panel');
const logHeader  = document.getElementById('log-header');
const clearBtn   = document.getElementById('btn-clear-log');

// 分割条
const dividerV1  = document.getElementById('divider-v1');
const dividerV2  = document.getElementById('divider-v2');
const dividerH1  = document.getElementById('divider-h1');
const leftSidebar  = document.getElementById('left-sidebar');
const centerPanel  = document.getElementById('center-panel');
const chatPanel    = document.getElementById('chatPanel');
const contentRow   = document.getElementById('content-row');
const previewContainer = document.getElementById('preview-container');
const logPanelEl   = logPanel;

// ─────────────────────────────────────────────
// 日志
// ─────────────────────────────────────────────
const MAX_LOG_LINES = 500;
let _logLineCount = 0;

function log(msg, type = 'info') {
    if (!logBodyEl) return;
    const now = new Date();
    const ts  = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`;

    if (_logLineCount >= MAX_LOG_LINES) {
        logBodyEl.firstElementChild?.remove();
        _logLineCount--;
    }

    const line = document.createElement('div');
    line.className = `log-line ${type}`;
    line.innerHTML = `<span class="log-time">${ts}</span><span class="log-msg">${escapeHtml(String(msg))}</span>`;
    logBodyEl.appendChild(line);
    _logLineCount++;
    logBodyEl.scrollTop = logBodyEl.scrollHeight;
}

function clearLog() {
    if (logBodyEl) logBodyEl.innerHTML = '';
    _logLineCount = 0;
}

// ─────────────────────────────────────────────
// 通用工具
// ─────────────────────────────────────────────
function escapeHtml(str) {
    const d = document.createElement('div');
    d.textContent = str || '';
    return d.innerHTML;
}

function formatMs(ms) {
    if (!ms || ms <= 0) return '0:00';
    const s   = Math.floor(ms / 1000);
    const min = Math.floor(s / 60);
    const sec = s % 60;
    return `${min}:${String(sec).padStart(2, '0')}`;
}

function setStatus(msg, isError = false) {
    if (!statusEl) return;
    if (!msg) { statusEl.className = 'preview-status'; return; }
    statusEl.textContent = msg;
    statusEl.className = `preview-status visible${isError ? ' error' : ''}`;
}

// ─────────────────────────────────────────────
// 三栏垂直分割条
// ─────────────────────────────────────────────
function setupVerticalDividers() {
    // 初始化默认宽度（1 : 4 : 1），在首次 layout 后设置
    function applyDefaultWidths() {
        const totalW = contentRow.offsetWidth - 8; // 减去两条分割条（各 4px）
        if (totalW <= 0) return;
        const unit = Math.floor(totalW / 12);
        leftSidebar.style.width = `${unit * 2}px`;
        chatPanel.style.width   = `${unit * 3}px`;
    }

    // 左侧分割条：拖动改变左栏宽度
    setupDivider(dividerV1, {
        axis: 'x',
        onDrag(delta) {
            const min = 120, max = contentRow.offsetWidth - 8 - chatPanel.offsetWidth - 200;
            const newW = Math.max(min, Math.min(max, leftSidebar.offsetWidth + delta));
            leftSidebar.style.width = `${newW}px`;
        },
    });

    // 右侧分割条：拖动改变右栏宽度（divider 在中栏右侧，向右拖 delta>0 应缩小右栏）
    setupDivider(dividerV2, {
        axis: 'x',
        onDrag(delta) {
            const min = 160, max = contentRow.offsetWidth - 8 - leftSidebar.offsetWidth - 200;
            const newW = Math.max(min, Math.min(max, chatPanel.offsetWidth - delta));
            chatPanel.style.width = `${newW}px`;
        },
    });

    // setTimeout 比 rAF 更晚执行，确保 flex 首次布局已完成再读 offsetWidth
    setTimeout(applyDefaultWidths, 0);
}

// ─────────────────────────────────────────────
// 中栏水平分割条（预览 vs 日志）
// ─────────────────────────────────────────────
const LOG_DEFAULT_H = 120;
const LOG_MIN_H     = 28;
const LOG_MAX_H     = 400;

function setupHorizontalDivider() {
    logPanelEl.style.height = `${LOG_DEFAULT_H}px`;

    setupDivider(dividerH1, {
        axis: 'y',
        onDrag(delta) {
            const curH = logPanelEl.offsetHeight;
            const newH = Math.max(LOG_MIN_H, Math.min(LOG_MAX_H, curH - delta));
            logPanelEl.style.height = `${newH}px`;
            // 折叠状态下只保留标题条
            if (newH <= LOG_MIN_H + 4) {
                logPanelEl.classList.add('collapsed');
            } else {
                logPanelEl.classList.remove('collapsed');
            }
        },
    });
}

// ─────────────────────────────────────────────
// 通用 setupDivider（内联，不依赖外部文件）
// ─────────────────────────────────────────────
function setupDivider(el, { axis, onDrag }) {
    if (!el) return;
    let startPos = 0;
    const cls = axis === 'y' ? 'resizing-h' : 'resizing-v';

    el.addEventListener('mousedown', (e) => {
        e.preventDefault();
        startPos = axis === 'y' ? e.clientY : e.clientX;
        el.classList.add('active');
        document.body.classList.add(cls);

        const onMove = (e) => {
            const cur   = axis === 'y' ? e.clientY : e.clientX;
            const delta = cur - startPos;
            startPos = cur;
            onDrag(delta);
        };
        const onUp = () => {
            el.classList.remove('active');
            document.body.classList.remove(cls);
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
        };
        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
    });
}

// ─────────────────────────────────────────────
// 日志折叠
// ─────────────────────────────────────────────
function setupLogPanel() {
    if (!logHeader) return;
    logHeader.addEventListener('click', (e) => {
        if (e.target.closest('#btn-clear-log')) return;
        const collapsed = logPanelEl.classList.toggle('collapsed');
        logPanelEl.style.height = collapsed ? `${LOG_MIN_H}px` : `${LOG_DEFAULT_H}px`;
    });
    if (clearBtn) clearBtn.addEventListener('click', clearLog);
}

// ─────────────────────────────────────────────
// 预览播放器
// ─────────────────────────────────────────────
function initPlayer() {
    log('初始化预览播放器...', 'info');
    try {
        preview.addon = require(path.join(PROJECT_ROOT, 'build', 'Release', 'video_player'));
        preview.root  = preview.addon.createRoot();
        preview.root.init();
        preview.video = new VideoPlayer(canvasEl);
        log(`GPU: ${preview.root.gpuInfo || '未知'}`, 'ok');
        log('预览播放器初始化成功', 'ok');
        loadCurrentProtocol();
    } catch (e) {
        log(`预览初始化失败: ${e.message}`, 'err');
        setStatus(`预览初始化失败: ${e.message}`, true);
    }
}

function loadCurrentProtocol() {
    if (!preview.root) return false;

    const proto = JSON.parse(JSON.stringify(PREVIEW_PROTOCOL));

    // 注入 effect 和 transition，均挂到 segment_0
    const seg0 = proto.tracks[0].segments.find(s => s.id === 'segment_0');

    if (_mountedEffect) {
        proto.materials.effects = [{ ..._mountedEffect }];
        if (seg0) seg0.extra_material_refs.push(_mountedEffect.id);
    } else {
        proto.materials.effects = [];
    }

    if (_mountedTransition) {
        proto.materials.transitions = [{ ..._mountedTransition }];
        if (seg0) seg0.extra_material_refs.push(_mountedTransition.id);
    } else {
        proto.materials.transitions = [];
    }

    const resolved = resolvePreviewPaths(proto, PROJECT_ROOT);
    log(`加载协议: ${_mountedEffect ? `effect=${_mountedEffect.name}` : '无 effect'}, ${_mountedTransition ? `transition=${_mountedTransition.name}` : '无 transition'}`, 'load');

    try {
        stopPlayback();
        const t0     = performance.now();
        const result = preview.root.load(JSON.stringify(resolved), PROJECT_ROOT);
        const dt     = (performance.now() - t0).toFixed(1);

        if (!result || !result.success) {
            const errMsg = result?.error || '未知错误';
            log(`✗ 加载失败 (${dt}ms): ${errMsg}`, 'err');
            setStatus(`加载失败: ${errMsg}`, true);
            _notifyChatLoadError({ stage: 'loadProject', path: _effectFolder || _transitionFolder || '', message: errMsg });
            return false;
        }

        log(`✓ 加载成功 (${dt}ms) | ${preview.root.width}×${preview.root.height} | ${(preview.root.frameRate||0).toFixed(1)}fps | ${formatMs(preview.root.durationMs)}`, 'ok');

        preview.video.load(preview.root);
        preview.video.render(0);
        updatePlaybackUI();

        if (placeholderEl) placeholderEl.classList.add('hidden');
        setStatus('');
        if (playBtn) playBtn.disabled = false;
        if (seekBar) seekBar.disabled = false;
        buildParamPanel();
        return true;
    } catch (e) {
        log(`✗ 加载异常: ${e.message}`, 'err');
        setStatus(`加载异常: ${e.message}`, true);
        _notifyChatLoadError({ stage: 'loadProject', path: _effectFolder || _transitionFolder || '', message: e.message });
        return false;
    }
}

function updatePlaybackUI() {
    if (!preview.video) return;
    const dur = preview.video.duration || 0;
    const cur = preview.video.currentTime || 0;
    if (timeDisplay) timeDisplay.textContent = `${formatMs(cur)} / ${formatMs(dur)}`;
    if (seekBar && dur > 0) seekBar.value = Math.round((cur / dur) * 1000);
}

function startPlayback() {
    if (!preview.video || !preview.video.loaded) return;
    if (preview.playing) return;

    preview.playing    = true;
    preview._startWall = performance.now();
    preview._startMs   = preview.video.currentTime || 0;

    if (playBtn) playBtn.querySelector('.material-symbols-rounded').textContent = 'pause';
    log('▶ 播放', 'info');

    const tick = () => {
        if (!preview.playing) return;
        const elapsed = performance.now() - preview._startWall;
        const timeMs  = preview._startMs + elapsed;
        const dur     = preview.video.duration || 0;

        if (timeMs >= dur) {
            preview.video.render(dur > 0 ? dur - 1 : 0);
            updatePlaybackUI();
            stopPlayback();
            log('■ 播放结束', 'info');
            return;
        }

        if (!preview.video.isSameFrame(timeMs)) {
            preview.video.render(timeMs);
            updatePlaybackUI();
        }
        preview._animId = requestAnimationFrame(tick);
    };
    preview._animId = requestAnimationFrame(tick);
}

function stopPlayback() {
    if (preview._animId) {
        cancelAnimationFrame(preview._animId);
        preview._animId = null;
    }
    if (preview.playing) log('⏸ 暂停', 'info');
    preview.playing = false;
    if (playBtn) playBtn.querySelector('.material-symbols-rounded').textContent = 'play_arrow';
}

function togglePlayback() {
    if (preview.playing) stopPlayback();
    else startPlayback();
}

function seekTo(ms) {
    if (!preview.video || !preview.video.loaded) return;
    const wasPlaying = preview.playing;
    stopPlayback();
    preview.video.render(ms);
    updatePlaybackUI();
    if (wasPlaying) startPlayback();
}

// ─────────────────────────────────────────────
// 沙箱资源列表（左栏）
// ─────────────────────────────────────────────
function scanSandbox() {
    const sandboxRoot = getSandboxRoot();
    const result = [];
    try {
        const entries = fs.readdirSync(sandboxRoot, { withFileTypes: true });
        for (const entry of entries) {
            if (!entry.isDirectory()) continue;
            const configPath = path.join(sandboxRoot, entry.name, 'config.json');
            if (!fs.existsSync(configPath)) continue;
            try {
                const cfg = JSON.parse(fs.readFileSync(configPath, 'utf-8'));
                result.push({
                    folder:   entry.name,
                    name:     (typeof cfg.name === 'string' && cfg.name.trim()) ? cfg.name.trim() : entry.name,
                    format:   cfg.format === 'transition' ? 'transition' : cfg.format === 'effect' ? 'effect' : null,
                    duration: cfg.suggestionDuration || 1000,
                });
            } catch {
                result.push({ folder: entry.name, name: entry.name, format: null, duration: 1000 });
            }
        }
    } catch (e) {
        log(`扫描沙箱失败: ${e.message}`, 'warn');
    }
    return result;
}

function renderResourceList() {
    const items = scanSandbox();
    if (!resourceListEl) return;

    if (items.length === 0) {
        resourceListEl.innerHTML = '<div class="resource-list-empty">沙箱内暂无资源工程<br>请先在聊天中让 AI 创建</div>';
        return;
    }

    resourceListEl.innerHTML = '';
    for (const item of items) {
        // 最终类型：config.json 里的 format 优先，其次用户在 UI 选的
        const resolvedFormat = item.format || _formatChoices[item.folder] || null;
        const isActive = resolvedFormat === 'effect'
            ? _effectFolder === item.folder
            : resolvedFormat === 'transition'
                ? _transitionFolder === item.folder
                : false;

        const el = document.createElement('div');

        if (!resolvedFormat) {
            // 类型未知 → 卡片不可直接点击，显示类型选择按钮
            el.className = 'resource-item unknown';
            el.innerHTML = `
                <div class="resource-item-name">${escapeHtml(item.name)}</div>
                <div class="resource-item-meta">
                    <span class="resource-item-folder">${escapeHtml(item.folder)}</span>
                    <div class="resource-type-btns">
                        <span class="resource-type-hint">选择类型:</span>
                        <button class="type-btn effect">特效</button>
                        <button class="type-btn transition">转场</button>
                    </div>
                </div>`;
            el.querySelector('.type-btn.effect').addEventListener('click', (e) => {
                e.stopPropagation();
                _formatChoices[item.folder] = 'effect';
                mountResource({ ...item, format: 'effect' });
            });
            el.querySelector('.type-btn.transition').addEventListener('click', (e) => {
                e.stopPropagation();
                _formatChoices[item.folder] = 'transition';
                mountResource({ ...item, format: 'transition' });
            });
        } else {
            // 类型已知 → 整张卡片可点击，激活态用样式表达
            const typeLabel = resolvedFormat === 'transition' ? '转场' : '特效';
            el.className = `resource-item selectable${isActive ? ' active' : ''}`;
            el.title = isActive ? `点击取消 ${typeLabel}` : `点击挂载 ${typeLabel}`;
            el.innerHTML = `
                <div class="resource-item-name">${escapeHtml(item.name)}</div>
                <div class="resource-item-meta">
                    <span class="resource-item-folder">${escapeHtml(item.folder)}</span>
                    <span class="resource-type-badge ${resolvedFormat}">${typeLabel}</span>
                </div>`;
            el.addEventListener('click', () => mountResource({ ...item, format: resolvedFormat }));
        }

        resourceListEl.appendChild(el);
    }
    log(`扫描到 ${items.length} 个资源工程`, 'info');
}

// ─────────────────────────────────────────────
// 资源挂载（§6.4）
// ─────────────────────────────────────────────

/** 设置 effect 或 transition 的挂载状态（null 表示卸载） */
function _applyMount(type, folder, resource) {
    if (type === 'effect') {
        _effectFolder  = folder;
        _mountedEffect = resource;
    } else {
        _transitionFolder  = folder;
        _mountedTransition = resource;
    }
}

function mountResource(item) {
    if (!preview.root) {
        log('预览播放器未就绪，无法挂载', 'warn');
        setStatus('预览播放器未就绪', true);
        return;
    }

    const absPath  = path.join(getSandboxRoot(), item.folder);
    const type     = item.format;
    const isMounted = type === 'effect'
        ? _effectFolder === item.folder
        : _transitionFolder === item.folder;

    if (isMounted) {
        _applyMount(type, null, null);
        log(`卸载 ${type}: ${item.name}`, 'info');
    } else {
        const extra = type === 'transition' ? { duration: item.duration || 1000 } : {};
        _applyMount(type, item.folder, { id: `${type}_res`, name: item.name, path: absPath, type, ...extra });
        log(`挂载 ${type}: ${item.name} (${item.folder})`, 'load');
    }

    renderResourceList();

    if (resourceLabelEl) {
        const parts = [];
        if (_mountedEffect)     parts.push(`E·${_mountedEffect.name}`);
        if (_mountedTransition) parts.push(`T·${_mountedTransition.name}`);
        resourceLabelEl.textContent = parts.length ? parts.join('  ') : '未挂载';
    }

    loadCurrentProtocol();
}

// ─────────────────────────────────────────────
// 参数面板（uniform[]）
// ─────────────────────────────────────────────
function buildParamPanel() {
    if (!paramPanelEl) return;

    // 收集所有已挂载资源的 uniform[]
    const sections = [];

    for (const [mounted, label, sliderClass] of [
        [_mountedEffect,     '特效参数', 'effect'],
        [_mountedTransition, '转场参数', 'transition'],
    ]) {
        if (!mounted) continue;
        const cfgPath = path.join(mounted.path, 'config.json');
        let uniforms = [];
        try {
            const cfg = JSON.parse(fs.readFileSync(cfgPath, 'utf-8'));
            uniforms = Array.isArray(cfg.uniform) ? cfg.uniform : [];
        } catch { /* 读不到 config 就跳过 */ }

        const rows = uniforms.filter(u => u.type !== 'animation' && u.type !== 'texture');
        if (rows.length) sections.push({ label, sliderClass, materialId: mounted.id, rows });
    }

    if (!sections.length) {
        paramPanelEl.className = 'empty';
        return;
    }

    paramPanelEl.className = '';
    paramPanelEl.innerHTML = '';

    for (const { label, sliderClass, materialId, rows } of sections) {
        const title = document.createElement('div');
        title.className = 'param-panel-title';
        title.textContent = label;
        paramPanelEl.appendChild(title);

        for (const u of rows) {
            const name       = u.name        || u.uniformTarget || '?';
            const type       = (u.type || 'float').toLowerCase();
            const defVal     = u.defaultValue ?? u.value ?? 0;
            const [rMin, rMax] = Array.isArray(u.range) ? u.range : [0, 1];

            const item = document.createElement('div');
            item.className = 'param-item';

            if (type === 'boolean') {
                // Checkbox
                const curVal = typeof defVal === 'boolean' ? defVal : !!defVal;
                item.innerHTML = `
                    <div class="param-checkbox-row">
                        <input type="checkbox" class="param-checkbox" id="pc_${materialId}_${u.name}"
                               ${curVal ? 'checked' : ''}>
                        <label for="pc_${materialId}_${u.name}">${escapeHtml(name)}</label>
                    </div>`;
                item.querySelector('input').addEventListener('change', (e) => {
                    if (preview.root) {
                        preview.root.setMaterialBoolParam(materialId, u.name, e.target.checked);
                        preview.video && preview.video.render(preview.video.currentTime || 0);
                    }
                });
            } else if (type === 'vec2' || type === 'vec3' || type === 'vec4') {
                // Vec：多个分量滑条
                const dim  = type === 'vec2' ? 2 : type === 'vec3' ? 3 : 4;
                const vals = Array.isArray(defVal) ? defVal : Array(dim).fill(defVal || 0);
                const axes = ['X', 'Y', 'Z', 'W'].slice(0, dim);
                item.innerHTML = `<div class="param-label"><span>${escapeHtml(name)}</span></div>
                    <div class="param-vec-row">${axes.map((ax, i) => `
                        <div>
                            <div class="param-label">
                                <span style="color:#4f5d8a">${ax}</span>
                                <span class="param-label-value" id="pv_${materialId}_${u.name}_${i}">${(vals[i] ?? 0).toFixed(3)}</span>
                            </div>
                            <input type="range" class="param-slider ${sliderClass}-slider"
                                   min="${rMin}" max="${rMax}" step="${(rMax - rMin) / 200}"
                                   value="${vals[i] ?? 0}"
                                   data-midx="${i}">
                        </div>`).join('')}
                    </div>`;
                const curVals = [...vals];
                item.querySelectorAll('input[type=range]').forEach(inp => {
                    inp.addEventListener('input', () => {
                        const idx = parseInt(inp.dataset.midx);
                        curVals[idx] = parseFloat(inp.value);
                        const span = item.querySelector(`#pv_${materialId}_${u.name}_${idx}`);
                        if (span) span.textContent = curVals[idx].toFixed(3);
                        if (preview.root) {
                            preview.root.setMaterialVecParam(materialId, u.name, curVals);
                            preview.video && preview.video.render(preview.video.currentTime || 0);
                        }
                    });
                });
            } else {
                // Float（默认）
                const val = typeof defVal === 'number' ? defVal : parseFloat(defVal) || 0;
                item.innerHTML = `
                    <div class="param-label">
                        <span>${escapeHtml(name)}</span>
                        <span class="param-label-value" id="pv_${materialId}_${u.name}">${val.toFixed(3)}</span>
                    </div>
                    <input type="range" class="param-slider ${sliderClass}-slider"
                           min="${rMin}" max="${rMax}" step="${(rMax - rMin) / 200}"
                           value="${val}">`;
                const span = item.querySelector(`#pv_${materialId}_${u.name}`);
                item.querySelector('input').addEventListener('input', (e) => {
                    const v = parseFloat(e.target.value);
                    if (span) span.textContent = v.toFixed(3);
                    if (preview.root) {
                        preview.root.setMaterialFloatParam(materialId, u.name, v);
                        preview.video && preview.video.render(preview.video.currentTime || 0);
                    }
                });
            }

            paramPanelEl.appendChild(item);
        }
    }
}
// ─────────────────────────────────────────────
function _notifyChatLoadError(errPayload) {
    try {
        const errText = `⚠️ **预览加载失败**\n\n\`\`\`json\n${JSON.stringify(errPayload, null, 2)}\n\`\`\`\n\n请根据上方错误修改 shader 或 config.json。`;
        _injectSystemMessage(errText);
    } catch (e) {
        log(`回传聊天错误失败: ${e.message}`, 'warn');
    }
}

function _injectSystemMessage(markdownText) {
    const messagesEl = document.getElementById('messages');
    if (!messagesEl) return;
    const { marked } = require('marked');
    const el = document.createElement('div');
    el.className = 'flex justify-start gap-2';
    el.innerHTML = `
        <div class="w-8 h-8 rounded-full bg-amber-900/40 flex items-center justify-center shrink-0 mt-0.5">
            <span class="material-symbols-rounded text-sm text-amber-400">warning</span>
        </div>
        <div class="max-w-[85%] flex flex-col">
            <div class="msg-bubble bg-amber-900/20 border border-amber-800/30 rounded-2xl rounded-bl-md px-4 py-2.5 text-sm leading-relaxed text-amber-200">
                <div class="message-content">${marked.parse(markdownText)}</div>
            </div>
        </div>
    `;
    messagesEl.appendChild(el);
    messagesEl.scrollTop = messagesEl.scrollHeight;
}

// ─────────────────────────────────────────────
// write_file auto-mount 钩子（§6.4）
// ─────────────────────────────────────────────
window.__onResourceWritten = function (absFilePath) {
    if (!absFilePath || !absFilePath.endsWith('config.json')) return;

    const sandboxRoot = getSandboxRoot();
    const relDir  = path.relative(sandboxRoot, path.dirname(absFilePath));
    if (!relDir || relDir.startsWith('..') || path.isAbsolute(relDir)) return;

    log(`检测到 config.json 写入: ${relDir}，触发自动挂载`, 'load');
    renderResourceList();

    try {
        const cfg = JSON.parse(fs.readFileSync(absFilePath, 'utf-8'));
        mountResource({
            folder:   relDir,
            name:     (typeof cfg.name === 'string' && cfg.name.trim()) ? cfg.name.trim() : relDir,
            format:   cfg.format === 'transition' ? 'transition' : 'effect',
            duration: cfg.suggestionDuration || 1000,
        });
    } catch (e) {
        log(`自动挂载读取 config 失败: ${e.message}`, 'err');
    }
};

// ─────────────────────────────────────────────
// window.__onProjectOpened 扩展
// ─────────────────────────────────────────────
const _chatOnProjectOpened = window.__onProjectOpened;
window.__onProjectOpened = async function (uuid, options = {}) {
    if (resTitleEl) resTitleEl.textContent = options.name || uuid || '';
    log(`打开工程: ${options.name || uuid} (mode=${options.mode || '-'})`, 'info');

    _effectFolder     = null;
    _transitionFolder = null;
    renderResourceList();

    if (typeof _chatOnProjectOpened === 'function') {
        await _chatOnProjectOpened(uuid, options);
    }
};

// ─────────────────────────────────────────────
// 事件绑定
// ─────────────────────────────────────────────
if (playBtn)    playBtn.addEventListener('click', togglePlayback);
if (refreshBtn) refreshBtn.addEventListener('click', renderResourceList);

if (seekBar) {
    seekBar.addEventListener('input', () => {
        if (!preview.video || !preview.video.loaded) return;
        const dur = preview.video.duration || 0;
        seekTo((seekBar.value / 1000) * dur);
    });
}

// ─────────────────────────────────────────────
// 初始化
// ─────────────────────────────────────────────
setupLogPanel();
setupVerticalDividers();
setupHorizontalDivider();
renderResourceList();
initPlayer();
