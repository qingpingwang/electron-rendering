const fs = require('fs/promises');
const path = require('path');
const os = require('os');
const { createHash, randomUUID } = require('crypto');
const { spawn } = require('child_process');
const processes = new Set();
function run(executable, args) {
    return new Promise((resolve, reject) => {
        const child = spawn(executable, args, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
        processes.add(child);
        let stdout = '', stderr = '';
        child.stdout.on('data', data => { stdout = (stdout + data).slice(-65536); });
        child.stderr.on('data', data => { stderr = (stderr + data).slice(-65536); });
        child.on('error', reject);
        child.on('close', code => {
            processes.delete(child);
            code === 0 ? resolve(stdout) : reject(new Error(`${path.basename(executable)} 失败：${stderr}`));
        });
    });
}
function stopAll() {
    stopping = true;
    for (const child of processes) { child.kill('SIGTERM'); }
    for (const job of waiting.splice(0)) { job.reject(new Error('预处理已停止')); }
}

const pending = new Map();
const cpuCount = os.availableParallelism?.() || os.cpus().length;
const concurrency = cpuCount;
const threadsPerJob = cpuCount;
const waiting = [];
let active = 0;
let stopping = false;
function schedule(task) {
    return new Promise((resolve, reject) => {
        waiting.push({ task, resolve, reject });
        drain();
    });
}
function drain() {
    while (!stopping && active < concurrency && waiting.length) {
        const { task, resolve, reject } = waiting.shift();
        active++;
        Promise.resolve().then(task).then(resolve, reject).finally(() => {
            active--;
            drain();
        });
    }
}

async function executable(name) {
    const configured = process.env[`NLE_${name.toUpperCase()}_PATH`];
    const candidates = configured ? [configured] : [...(process.env.PATH || '').split(path.delimiter), '/opt/homebrew/bin', '/usr/local/bin', '/usr/bin'].map(dir => path.join(dir, name));
    for (const file of candidates) {
        try { await fs.access(file, 1); return file; } catch { /* Try the next runtime installation. */ }
    }
    throw new Error(`找不到 ${name}，请安装 FFmpeg 或设置 NLE_${name.toUpperCase()}_PATH`);
}
async function probe(file, ffprobe) {
    return JSON.parse(await run(ffprobe, ['-v', 'error', '-show_entries', 'stream=codec_type,width,height,duration,avg_frame_rate:format=duration', '-of', 'json', file]));
}
async function exists(file) {
    try { await fs.access(file); return true; } catch (error) {
        if (error.code === 'ENOENT') { return false; }
        throw error;
    }
}
async function build(source, outputDir, report) {
    await fs.mkdir(outputDir, { recursive: true });
    const result = {
        source, outputDir,
        thumbnail: path.join(outputDir, '1fps_200.mp4'),
        video: path.join(outputDir, '720p.mp4'),
        audio: path.join(outputDir, 'audio.m4a'),
        skipped: true,
    };
    const ready = await Promise.all([result.thumbnail, result.video, result.audio].map(exists));
    if (ready.every(Boolean)) { return result; }
    const ffmpeg = await executable('ffmpeg');
    const ffprobe = await executable('ffprobe');
    const info = await probe(source, ffprobe);
    if (!info.streams.some(stream => stream.codec_type === 'video')) { throw new Error(`素材没有视频轨道：${source}`); }
    const hasAudio = info.streams.some(stream => stream.codec_type === 'audio');
    if (!hasAudio) { result.audio = null; }
    const missing = {
        thumbnail: !ready[0],
        video: !ready[1],
        audio: hasAudio && !ready[2],
    };
    const stages = Object.keys(missing).filter(key => missing[key]);
    if (!stages.length) { return result; }
    result.skipped = false;
    report({ stage: 'transcode', stages, message: `构建缓存中：${path.basename(source)} · ${stages.map(key => path.basename(result[key])).join('、')}` });
    const threads = String(threadsPerJob);
    const args = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
        '-filter_threads', threads, '-filter_complex_threads', threads, '-threads', threads, '-i', source];
    const graph = [];
    const videoStages = stages.filter(key => key !== 'audio');
    const filters = {
        thumbnail: "fps=1,scale=w='min(200,iw)':h='min(200,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1",
        video: "scale=w='if(gte(iw,ih),min(1280,iw),min(720,iw))':h='if(gte(iw,ih),min(720,ih),min(1280,ih))':force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1",
    };
    if (videoStages.length === 2) {
        graph.push('[0:v:0]split=2[small_in][preview_in]');
    }
    for (const key of videoStages) {
        const input = videoStages.length === 2 ? (key === 'thumbnail' ? '[small_in]' : '[preview_in]') : '[0:v:0]';
        graph.push(`${input}${filters[key]}[${key}]`);
    }
    // Share the decoded audio between the small video and the standalone AAC output.
    const audioStages = hasAudio ? stages.filter(key => key !== 'video') : [];
    if (audioStages.length === 2) {
        graph.push('[0:a:0]asplit=2[small_audio][standalone_audio]');
    }
    if (graph.length) { args.push('-filter_complex', graph.join(';')); }
    const outputs = stages.map(key => ({ key, file: result[key], temp: path.join(outputDir, `${randomUUID()}.tmp${path.extname(result[key])}`) }));
    for (const { key, temp } of outputs) {
        if (key === 'audio') {
            args.push('-map', audioStages.length === 2 ? '[standalone_audio]' : '0:a:0', '-vn', '-c:a', 'aac', '-b:a', '96k', '-ar', '44100', '-ac', '2', '-movflags', '+faststart', temp);
            continue;
        }
        args.push('-map', `[${key}]`);
        if (key === 'thumbnail' && hasAudio) {
            args.push('-map', audioStages.length === 2 ? '[small_audio]' : '0:a:0', '-c:a', 'aac', '-b:a', '96k');
        } else {
            args.push('-an');
        }
        args.push('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '23', '-pix_fmt', 'yuv420p', '-bf', '0',
            '-threads:v', threads, '-movflags', '+faststart', '-g', key === 'thumbnail' ? '1' : '30');
        if (key === 'video') { args.push('-fps_mode', 'passthrough'); }
        args.push(temp);
    }
    try {
        await run(ffmpeg, args);
        for (const { temp, file } of outputs) { await fs.rename(temp, file); }
    } finally {
        await Promise.all(outputs.map(({ temp }) => fs.rm(temp, { force: true })));
    }
    return result;
}
async function prepareOne({ source, outputDir }, report = () => {}) {
    if (stopping) { throw new Error('预处理已停止'); }
    if (!path.isAbsolute(source) || !path.isAbsolute(outputDir)) {
        throw new Error('源文件和输出目录必须是绝对路径');
    }
    const jobKey = `${outputDir}\n${source}`;
    if (!pending.has(jobKey)) {
        const task = schedule(() => build(source, outputDir, report));
        pending.set(jobKey, task);
        task.finally(() => pending.delete(jobKey)).catch(() => {});
    }
    return pending.get(jobKey);
}
async function preprocess(videoPaths, cacheDir, report = () => {}) {
    if (!Array.isArray(videoPaths) || !path.isAbsolute(cacheDir) || videoPaths.some(source => typeof source !== 'string' || !path.isAbsolute(source))) {
        throw new Error('请传入视频绝对路径数组和 cache 绝对目录');
    }
    await fs.mkdir(cacheDir, { recursive: true });
    const sources = [...new Set(videoPaths)];
    let completed = 0;
    const total = sources.length;
    const emit = event => report({ total, completed, ...event });
    emit({ type: 'batch-start', status: 'running' });
    const results = new Map(await Promise.all(sources.map(async (source, index) => {
        const outputDir = path.join(cacheDir, createHash('md5').update(source).digest('hex'));
        const file = { source, index, outputDir };
        emit({ ...file, type: 'file-start', status: 'queued', message: `等待处理：${path.basename(source)}` });
        try {
            const result = await prepareOne({ source, outputDir }, progress => {
                emit({ ...file, ...progress, type: 'file-progress', status: 'running' });
            });
            completed++;
            emit({ ...file, type: 'file-complete', status: result.skipped ? 'skipped' : 'completed', result, message: `缓存已就绪：${path.basename(source)}` });
            return [source, result];
        } catch (error) {
            emit({ ...file, type: 'file-error', status: 'failed', error: error.message, message: `处理失败：${path.basename(source)}` });
            throw error;
        }
    })));
    emit({ type: 'batch-complete', status: 'completed' });
    return videoPaths.map(source => results.get(source));
}
module.exports = { preprocess, stopAll };
