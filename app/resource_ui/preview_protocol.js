/**
 * 资源制作预览协议（前端写死常量，不从磁盘加载）。
 *
 * 结构约定：
 *   - 单条 video 轨道，2 个 segment，共用 material_video_0
 *   - segment_0：extra_material_refs 挂 transition
 *   - segment_1：extra_material_refs 挂 effect
 *   - materials.effects / transitions 运行时各最多 1 条
 *
 * 预览视频：默认使用仓库根目录的 resources/test.mp4（相对项目根解析）。
 * 如需替换，改此常量的 materials.videos[0].path 并重启 UI。
 */
const path = require('path');

const PREVIEW_PROTOCOL = {
    id: 'resource_preview',
    duration: 4000,
    fps: 30,
    canvas_config: { width: 720, height: 1280, ratio: '16:9' },
    materials: {
        videos: [
            {
                id: 'material_video_0',
                name: 'preview',
                path: 'resources/test.mp4',  // 相对项目根；resolvePreviewPaths 会转为绝对路径
                type: '',
            },
        ],
        effects: [],
        transitions: [],
    },
    tracks: [
        {
            id: 'track_video_0',
            type: 'video',
            muted: false,
            visible: true,
            segments: [
                {
                    id: 'segment_0',
                    material_id: 'material_video_0',
                    visible: true,
                    muted: false,
                    extra_material_refs: [],    // 运行时填入 transition material id
                    source_timerange: { start: 0, duration: 2000 },
                    target_timerange: { start: 0, duration: 2000 },
                },
                {
                    id: 'segment_1',
                    material_id: 'material_video_0',
                    visible: true,
                    muted: false,
                    extra_material_refs: [],    // 运行时填入 effect material id
                    source_timerange: { start: 0, duration: 2000 },
                    target_timerange: { start: 2000, duration: 2000 },
                },
            ],
        },
    ],
};

/**
 * 将 materials 里的相对路径转为 Native 可读的绝对路径。
 * 不修改导出的常量，返回深拷贝。
 *
 * @param {object} protocol - PREVIEW_PROTOCOL 或带相对路径的同结构对象
 * @param {string} projectRoot - 仓库根目录（含 resources/、build/；通常为 app 的上一级）
 */
function resolvePreviewPaths(protocol, projectRoot) {
    const root = projectRoot || path.join(__dirname, '..', '..');
    const copy = JSON.parse(JSON.stringify(protocol));

    const resolvePath = (p) =>
        (!p || path.isAbsolute(p)) ? p : path.resolve(root, p);

    copy.materials.videos = (copy.materials.videos || []).map((v) => ({
        ...v,
        path: resolvePath(v.path),
    }));
    copy.materials.effects = (copy.materials.effects || []).map((e) => ({
        ...e,
        path: resolvePath(e.path),
    }));
    copy.materials.transitions = (copy.materials.transitions || []).map((t) => ({
        ...t,
        path: resolvePath(t.path),
    }));

    return copy;
}

module.exports = { PREVIEW_PROTOCOL, resolvePreviewPaths };
