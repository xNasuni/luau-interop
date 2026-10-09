import {createServer} from 'node:http';
import {readFile, mkdtemp, mkdir, copyFile, rm, writeFile} from 'node:fs/promises';
import {resolve, dirname, join, relative} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {parseArgs} from 'node:util';
import {chromium} from 'playwright';

const root = dirname(fileURLToPath(import.meta.url));
const {values} = parseArgs({options: {
    backend: {type: 'string'}, 'build-dir': {type: 'string'}, report: {type: 'string'},
    suite: {type: 'string', default: 'nul'}, case: {type: 'string'},
}});
if (!['JSPI', 'Asyncify'].includes(values.backend) || !values['build-dir'] || !['nul', 'lifetime'].includes(values.suite))
    throw Error('Usage: npm test -- --backend JSPI|Asyncify --build-dir ../../build-web [--report results.json]');
const backend = `Luau.Web.${values.backend}.js`;
const runtime = await mkdtemp(join(root, '.runtime-'));
const report = {backend: values.backend, suite: values.suite, results: [], cleanupErrors: []};
let browser, server;
try {
    await mkdir(join(runtime, 'lib'));
    await copyFile(join(root, 'node_modules/luau-web/src/index.js'), join(runtime, 'index.js'));
    const binary = await readFile(resolve(values['build-dir'], backend));
    report.backendSha256 = createHash('sha256').update(binary).digest('hex');
    await writeFile(join(runtime, 'lib', backend), binary);
    server = createServer(async (request, response) => {
        try {
            const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
            if (pathname === '/') { response.end('<!doctype html><title>Web interop regression</title>'); return; }
            const path = resolve(root, '.' + pathname);
            if (relative(root, path).startsWith('..')) throw Error('outside test root');
            response.setHeader('Content-Type', 'text/javascript');
            response.end(await readFile(path));
        } catch { response.writeHead(404); response.end(); }
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    browser = await chromium.launch({headless: true});
    report.browser = browser.version();
    const page = await browser.newPage();
    report.console = [];
    page.on('console', message => report.console.push(message.text()));
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    report.surface = values.backend === 'JSPI' ? 'worker' : 'window';
    report.results = await Promise.race([page.evaluate(async ({backend, runtime, suite, only}) => {
        if (backend === 'Asyncify') {
            const {runTests} = await import('/worker.mjs');
            return runTests({backend, runtime, suite, only});
        }
        return new Promise((resolve, reject) => {
            const worker = new Worker('/worker.mjs', {type: 'module'});
            const timer = setTimeout(() => { worker.terminate(); reject(Error('worker deadline (60000ms)')); }, 60000);
            worker.onerror = event => { clearTimeout(timer); worker.terminate(); reject(Error(event.message)); };
            worker.onmessage = ({data}) => { clearTimeout(timer); worker.terminate(); resolve(data); };
            worker.postMessage({backend, runtime, suite, only});
        });
    }, {backend: values.backend, runtime: '/' + relative(root, runtime), suite: values.suite, only: values.case}), new Promise((_, reject) => {
        const timer = setTimeout(() => reject(Error('browser deadline (65000ms)')), 65000);
        timer.unref();
    })]);
    report.expectedCases = values.suite === 'lifetime' ? ['recreate-100', 'two-live-vms', 'closed-wrapper', 'coroutine-owner', 'release-reuse', 'error-recovery', 'callback-gc'] : [...Array.from({length: 9}, (_, i) => `value-${i}`), 'distinct-table-keys', 'object-keys-and-iteration', 'raw-global', 'opaque-identifiers', 'js-error-ingress'];
    if (values.case) report.expectedCases = report.expectedCases.filter(id => id === values.case);
    if (!report.expectedCases.length || JSON.stringify(report.results.map(r => r.id)) !== JSON.stringify(report.expectedCases) || report.results.some(result => !result.pass)) process.exitCode = 1;
} catch (error) { report.error = error.stack; process.exitCode = 1; }
finally {
    for (const cleanup of [() => browser?.close(), () => server && new Promise((resolve, reject) => server.close(e => e ? reject(e) : resolve())), () => rm(runtime, {recursive: true, force: true})]) {
        try { await cleanup(); } catch (e) { report.cleanupErrors.push(String(e)); process.exitCode = 1; }
    }
    console.log(JSON.stringify(report, null, 2));
    if (values.report) await writeFile(resolve(values.report), JSON.stringify(report, null, 2) + '\n');
}
