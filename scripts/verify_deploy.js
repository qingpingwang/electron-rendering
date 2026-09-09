const fs = require('fs');
const path = require('path');
const root = path.resolve(__dirname, '../deploy');
try {
    const info = JSON.parse(fs.readFileSync(path.join(root, 'runtime.json'), 'utf8'));
    if (info.platform !== process.platform || info.arch !== process.arch) {
        throw new Error(`deploy 为 ${info.platform}/${info.arch}，当前系统为 ${process.platform}/${process.arch}；请使用对应平台的预编译产物`);
    }
    const files = ['video_player.node'];
    if (process.platform === 'darwin') {
        files.push('lib/libEGL.dylib', 'lib/libGLESv2.dylib', 'plugins/media_codec_apple/libnle_media_codec_apple_plugin.dylib');
    } else {
        files.push(`plugins/media_codec_ffmpeg/${process.platform === 'win32' ? 'nle_media_codec_ffmpeg_plugin.dll' : 'libnle_media_codec_ffmpeg_plugin.so'}`);
    }
    for (const name of files) {
        const file = path.join(root, name);
        const fd = fs.openSync(file, 'r');
        try {
            const header = Buffer.alloc(128);
            const bytes = fs.readSync(fd, header, 0, header.length, 0);
            if (!bytes || header.toString('utf8', 0, bytes).startsWith('version https://git-lfs.github.com/spec/v1')) {
                throw new Error(`${name} 尚未下载，请执行 git lfs pull`);
            }
        } finally { fs.closeSync(fd); }
    }
} catch (error) {
    console.error(`预编译产物检查失败：${error.message}`);
    process.exitCode = 1;
}
