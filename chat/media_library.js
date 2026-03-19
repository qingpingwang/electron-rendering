const { ipcRenderer } = require('electron');

const mediaPanel = document.getElementById('mediaPanel');
const previewModal = document.getElementById('previewModal');
const previewContent = document.getElementById('previewContent');
const closePreviewBtn = document.getElementById('btn-close-preview');

let mediaItems = [];
let activeFilter = 'all';
let searchQuery = '';
let isVisible = false;
let _isStreamingFn = null;

const FILTERS = [
    { id: 'all', label: '全部', icon: 'apps' },
    { id: 'video', label: '视频', icon: 'videocam' },
    { id: 'audio', label: '音频', icon: 'audiotrack' },
    { id: 'image', label: '图片', icon: 'image' },
];

function init() {
    renderPanelStructure();
    bindEvents();
    ipcRenderer.on('media:list-result', (_e, items) => {
        mediaItems = items || [];
        renderMediaList();
    });
}

function renderPanelStructure() {
    mediaPanel.innerHTML = `
        <div class="flex items-center justify-between px-4 h-11 border-b border-border-dark shrink-0">
            <div class="flex items-center gap-2">
                <span class="material-symbols-rounded text-lg text-indigo-400">perm_media</span>
                <span class="text-sm font-semibold text-slate-400">素材库</span>
            </div>
            <button id="btn-close-media" class="p-1 rounded-lg text-slate-500 hover:text-slate-300 hover:bg-white/5 transition-colors">
                <span class="material-symbols-rounded text-lg">close</span>
            </button>
        </div>

        <!-- Search -->
        <div class="px-3 pt-3 pb-2">
            <div class="flex items-center gap-2 bg-input-dark border border-border-dark rounded-lg px-2.5 py-1.5 focus-within:border-indigo-500/40 transition-colors">
                <span class="material-symbols-rounded text-base text-slate-500">search</span>
                <input id="media-search" type="text" placeholder="搜索素材..."
                    class="flex-1 bg-transparent border-none outline-none text-sm text-slate-300 placeholder:text-slate-600">
            </div>
        </div>

        <!-- Filter tabs -->
        <div id="media-filters" class="flex gap-1 px-3 pb-2">
            ${FILTERS.map(f => `
                <button data-filter="${f.id}"
                    class="media-filter-btn flex items-center gap-1 px-2.5 py-1 rounded-lg text-xs transition-colors
                           ${f.id === 'all' ? 'bg-primary/20 text-indigo-300' : 'text-slate-500 hover:text-slate-300 hover:bg-white/5'}">
                    <span class="material-symbols-rounded text-sm">${f.icon}</span>
                    ${f.label}
                </button>
            `).join('')}
        </div>

        <!-- Media list -->
        <div id="media-list" class="flex-1 overflow-y-auto px-3 pb-3 scrollbar-thin">
            <div class="flex flex-col items-center justify-center py-10 text-slate-600 text-sm gap-2">
                <span class="material-symbols-rounded text-3xl">folder_open</span>
                <span>暂无素材</span>
            </div>
        </div>

        <!-- Upload button -->
        <div class="px-3 py-2 border-t border-border-dark shrink-0">
            <button id="btn-import-media"
                class="w-full flex items-center justify-center gap-2 py-2 rounded-lg bg-primary/20 text-indigo-300 hover:bg-primary/30 transition-colors text-sm font-medium">
                <span class="material-symbols-rounded text-lg">upload</span>
                导入素材
            </button>
        </div>

        <!-- Upload overlay -->
        <div id="upload-overlay" class="upload-overlay absolute inset-0 flex items-center justify-center z-10" style="display:none;">
            <div class="flex flex-col items-center gap-3">
                <div class="tool-spinner" style="width:24px;height:24px;border-width:3px;"></div>
                <span class="text-sm text-slate-400">导入中...</span>
            </div>
        </div>
    `;
}

function bindEvents() {
    mediaPanel.addEventListener('click', (e) => {
        const closeBtn = e.target.closest('#btn-close-media');
        if (closeBtn) { hide(); return; }

        const filterBtn = e.target.closest('.media-filter-btn');
        if (filterBtn) {
            setFilter(filterBtn.dataset.filter);
            return;
        }

        const importBtn = e.target.closest('#btn-import-media');
        if (importBtn) {
            importMedia();
            return;
        }

        const deleteBtn = e.target.closest('.btn-delete-media');
        if (deleteBtn) {
            e.stopPropagation();
            const id = deleteBtn.dataset.id;
            if (id) deleteMedia(id);
            return;
        }

        const copyBtn = e.target.closest('.btn-copy-id');
        if (copyBtn) {
            e.stopPropagation();
            const id = copyBtn.dataset.id;
            if (id) copyToClipboard(id);
            return;
        }

        const card = e.target.closest('.resource-card');
        if (card && card.dataset.id) {
            openPreview(card.dataset.id);
        }
    });

    const searchInput = mediaPanel.querySelector('#media-search');
    if (searchInput) {
        searchInput.addEventListener('input', (e) => {
            searchQuery = e.target.value.trim().toLowerCase();
            renderMediaList();
        });
    }

    closePreviewBtn?.addEventListener('click', closePreview);
    previewModal?.addEventListener('click', (e) => {
        if (e.target === previewModal) closePreview();
    });

    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && previewModal.style.display !== 'none') {
            closePreview();
        }
    });
}

function setFilter(filterId) {
    activeFilter = filterId;
    mediaPanel.querySelectorAll('.media-filter-btn').forEach(btn => {
        if (btn.dataset.filter === filterId) {
            btn.className = btn.className.replace(/text-slate-500 hover:text-slate-300 hover:bg-white\/5/g, '').replace(/bg-primary\/20 text-indigo-300/g, '');
            btn.classList.add('bg-primary/20', 'text-indigo-300');
        } else {
            btn.className = btn.className.replace(/bg-primary\/20 text-indigo-300/g, '');
            btn.classList.add('text-slate-500');
        }
    });
    renderMediaList();
}

function getFilteredItems() {
    return mediaItems.filter(item => {
        if (activeFilter !== 'all' && item.type !== activeFilter) return false;
        if (searchQuery) {
            const haystack = `${item.name} ${item.type} ${item.resolution || ''}`.toLowerCase();
            if (!haystack.includes(searchQuery)) return false;
        }
        return true;
    });
}

function renderMediaList() {
    const listEl = mediaPanel.querySelector('#media-list');
    if (!listEl) return;

    const filtered = getFilteredItems();

    if (filtered.length === 0) {
        listEl.innerHTML = `
            <div class="flex flex-col items-center justify-center py-10 text-slate-600 text-sm gap-2">
                <span class="material-symbols-rounded text-3xl">folder_open</span>
                <span>${searchQuery ? '未找到匹配的素材' : '暂无素材'}</span>
            </div>
        `;
        return;
    }

    listEl.innerHTML = filtered.map(item => renderCard(item)).join('');
}

function renderCard(item) {
    const icon = item.type === 'video' ? 'videocam'
        : item.type === 'audio' ? 'audiotrack'
        : 'image';
    const sizeStr = item.size ? formatFileSize(item.size) : '';
    const durationStr = item.duration ? formatDuration(item.duration) : '';
    const meta = [sizeStr, item.resolution, durationStr].filter(Boolean).join(' · ');
    const shortId = item.id.slice(0, 8);

    let preview = '';
    if (item.type === 'image') {
        preview = `<img src="file://${item.path}" class="w-full h-28 object-cover rounded-t-lg" alt="${escapeHtml(item.name)}" loading="lazy">`;
    } else if (item.type === 'video') {
        preview = `
            <div class="w-full h-28 rounded-t-lg bg-black/40 flex items-center justify-center relative overflow-hidden">
                <video src="file://${item.path}" class="absolute inset-0 w-full h-full object-cover opacity-60" muted preload="metadata"></video>
                <span class="material-symbols-rounded text-3xl text-white/70 relative z-10">play_circle</span>
                ${durationStr ? `<span class="absolute bottom-1 right-1 text-[10px] bg-black/60 text-white px-1.5 py-0.5 rounded z-10">${durationStr}</span>` : ''}
            </div>
        `;
    } else {
        preview = `
            <div class="w-full h-28 rounded-t-lg bg-gradient-to-br from-indigo-900/30 to-purple-900/30 flex items-center justify-center">
                <span class="material-symbols-rounded text-4xl text-indigo-400/60">graphic_eq</span>
                ${durationStr ? `<span class="absolute bottom-1 right-1 text-[10px] bg-black/60 text-white px-1.5 py-0.5 rounded">${durationStr}</span>` : ''}
            </div>
        `;
    }

    return `
        <div class="resource-card rounded-lg overflow-hidden bg-white/[0.03] border border-border-dark cursor-pointer mb-2 relative group" data-id="${item.id}">
            ${preview}
            <div class="px-3 py-2">
                <div class="flex items-center gap-1.5 mb-1">
                    <span class="material-symbols-rounded text-xs text-slate-500">${icon}</span>
                    <span class="text-xs text-slate-300 truncate flex-1" title="${escapeHtml(item.name)}">${escapeHtml(item.name)}</span>
                    <button class="btn-delete-media opacity-0 group-hover:opacity-100 p-0.5 rounded text-slate-600 hover:text-red-400 transition-all" data-id="${item.id}" title="移除">
                        <span class="material-symbols-rounded text-sm">delete</span>
                    </button>
                </div>
                ${meta ? `<div class="text-[10px] text-slate-600 mb-1">${meta}</div>` : ''}
                <div class="flex items-center gap-1">
                    <code class="text-[10px] text-slate-600 font-mono">${shortId}</code>
                    <button class="btn-copy-id p-0.5 rounded text-slate-600 hover:text-indigo-400 transition-colors" data-id="${item.id}" title="复制 ID">
                        <span class="material-symbols-rounded text-xs">content_copy</span>
                    </button>
                </div>
            </div>
        </div>
    `;
}

// ---- Actions ----

function importMedia() {
    if (_isStreamingFn && _isStreamingFn()) return;

    const overlay = mediaPanel.querySelector('#upload-overlay');
    if (overlay) overlay.style.display = 'flex';

    ipcRenderer.invoke('media:import').then(result => {
        if (overlay) overlay.style.display = 'none';
        if (result && result.length) {
            mediaItems.push(...result);
            renderMediaList();
        }
    }).catch(() => {
        if (overlay) overlay.style.display = 'none';
    });
}

function deleteMedia(id) {
    if (_isStreamingFn && _isStreamingFn()) return;

    ipcRenderer.invoke('media:delete', id).then(success => {
        if (success) {
            mediaItems = mediaItems.filter(m => m.id !== id);
            renderMediaList();
        }
    });
}

function openPreview(id) {
    const item = mediaItems.find(m => m.id === id);
    if (!item) return;

    let content = '';
    if (item.type === 'video') {
        content = `<video src="file://${item.path}" controls autoplay class="w-full max-h-[80vh] rounded-xl bg-black"></video>`;
    } else if (item.type === 'audio') {
        content = `
            <div class="flex flex-col items-center gap-6 p-10">
                <span class="material-symbols-rounded text-6xl text-indigo-400">graphic_eq</span>
                <div class="text-sm text-slate-300">${escapeHtml(item.name)}</div>
                <audio src="file://${item.path}" controls autoplay class="w-full"></audio>
            </div>
        `;
    } else {
        content = `<img src="file://${item.path}" class="w-full max-h-[80vh] object-contain rounded-xl" alt="${escapeHtml(item.name)}">`;
    }

    previewContent.innerHTML = content;
    previewModal.style.display = 'flex';
}

function closePreview() {
    previewModal.style.display = 'none';
    previewContent.querySelectorAll('video, audio').forEach(el => {
        el.pause();
        el.src = '';
    });
    previewContent.innerHTML = '';
}

// ---- Visibility ----

function show() {
    mediaPanel.style.display = 'flex';
    isVisible = true;
    ipcRenderer.send('media:list');
}

function hide() {
    mediaPanel.style.display = 'none';
    isVisible = false;
}

function toggle() {
    if (isVisible) hide();
    else show();
}

// ---- Helpers ----

function formatFileSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function formatDuration(seconds) {
    if (!seconds || seconds <= 0) return '';
    const m = Math.floor(seconds / 60);
    const s = Math.floor(seconds % 60);
    return `${m}:${String(s).padStart(2, '0')}`;
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function copyToClipboard(text) {
    navigator.clipboard.writeText(text).catch(() => {});
}

function setStreamingCheck(fn) {
    _isStreamingFn = fn;
}

module.exports = { init, show, hide, toggle, setStreamingCheck };
