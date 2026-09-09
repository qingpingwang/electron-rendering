const fs = require('fs');
const path = require('path');
const os = require('os');
const { spawnSync } = require('child_process');
const root = path.resolve(__dirname, '..');
const config = process.argv[2] || 'Release';
const sdk = path.join(root, 'third_party/nle-sdk');
function run(command, args) {
    const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' });
    if (result.error) { throw result.error; }
    if (result.status !== 0) { throw new Error(`${command} failed (${result.status ?? result.signal})`); }
}
try {
    if (!['Release', 'Debug'].includes(config)) { throw new Error('Usage: node scripts/build_native.js [Release|Debug]'); }
    if (!fs.existsSync(path.join(sdk, 'CMakeLists.txt'))) {
        throw new Error('源码构建需要私有 nle-sdk：维护者先运行 bash scripts/init_submodule.sh；直接运行应用无需执行 build');
    }
    const build = path.join(root, 'build', config);
    // The SDK script skips already built dependencies. All host build entries use this path.
    run('bash', [path.join(sdk, 'scripts/build_skia.sh')]);
    run('cmake', ['-S', root, '-B', build, '-G', 'Unix Makefiles', `-DCMAKE_BUILD_TYPE=${config}`]);
    run('cmake', ['--build', build, '--config', config, '--target', 'video_player', '-j', String(os.availableParallelism?.() || os.cpus().length)]);
    const plugin = process.platform === 'darwin' ? 'media_codec_apple' : 'media_codec_ffmpeg';
    const pluginFile = process.platform === 'win32' ? `nle_${plugin}_plugin.dll` : `libnle_${plugin}_plugin.${process.platform === 'darwin' ? 'dylib' : 'so'}`;
    const files = [
        ['video_player.node', path.join(build, 'video_player.node')],
        [`plugins/${plugin}/${pluginFile}`, path.join(build, 'plugins', plugin, pluginFile)],
    ];
    if (process.platform === 'darwin' || process.platform === 'win32') {
        for (const name of ['libEGL', 'libGLESv2']) {
            const filename = `${name}.${process.platform === 'darwin' ? 'dylib' : 'dll'}`;
            files.push([process.platform === 'darwin' ? `lib/${filename}` : filename, path.join(sdk, 'third-party/skia/out/Release', filename)]);
        }
    }
    // Validate the full build before touching the published output.
    for (const [, source] of files) {
        if (!fs.statSync(source).isFile()) { throw new Error(`Build output missing: ${source}`); }
    }
    const deploy = path.join(root, 'deploy');
    for (const [relative, source] of files) {
        const target = path.join(deploy, relative);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.copyFileSync(source, target);
    }
    fs.writeFileSync(path.join(deploy, 'runtime.json'), JSON.stringify({ platform: process.platform, arch: process.arch, napi: 8, configuration: config }, null, 2) + '\n');
    run(process.execPath, [path.join(__dirname, 'verify_deploy.js')]);
    console.log(`Native build published to ${deploy}`);
} catch (error) {
    console.error(error.message);
    process.exitCode = 1;
}
