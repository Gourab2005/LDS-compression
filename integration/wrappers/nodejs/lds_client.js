/**
 * LDS (Log Data Stream) - Node.js Streaming Client
 * =================================================
 * Demonstrates high-throughput online log stream compression
 * from Node.js applications by spawning the native LDS C binary.
 */

const { spawn } = require('child_process');
const path = require('path');
const readline = require('readline');

function findEnginePath() {
    const candidates = [
        path.join(__dirname, '..', '..', '..', 'bin', 'lds_encoder_engine.exe'),
        path.join(__dirname, '..', '..', '..', 'bin', 'lds_encoder_engine'),
        path.join(__dirname, '..', '..', '..', 'docs', 'downloads', 'lds_encoder_engine.exe'),
        path.join(__dirname, '..', '..', '..', 'docs', 'downloads', 'lds_encoder_engine')
    ];
    for (const c of candidates) {
        if (require('fs').existsSync(c)) return c;
    }
    throw new Error('Could not find lds_encoder_engine executable');
}

const enginePath = findEnginePath();
console.log(`[*] Spawning LDS Native Engine: ${enginePath}`);

const encoder = spawn(enginePath, [], { stdio: ['pipe', 'pipe', 'pipe'] });
const rl = readline.createInterface({ input: encoder.stdout, terminal: false });

const sampleLogs = [
    '2026-10-05 02:01:47.520 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=41.7m speed=45.2km/h status=OK',
    '2026-10-05 02:01:47.620 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.9m speed=45.1km/h status=OK',
    '2026-10-05 02:01:47.720 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.1m speed=44.9km/h status=OK',
    '2026-10-05 02:01:47.820 WARN ADAS [RADAR] sensor=FRONT objects=6 dist=26.3m speed=-0.1km/h status=ALERT',
    '2026-10-05 02:01:47.920 INFO BMS [BATTERY] pack=HV01 voltage=386.20V min_cell=3.094V max_cell=4.108V'
];

let logIndex = 0;

rl.on('line', (line) => {
    try {
        const msg = JSON.parse(line);
        if (msg.status === 'READY') {
            console.log(`[*] Engine Ready: ${msg.engine} (RAM Budget: ~${msg.ram_budget_kb} KB)`);
            sendNext();
        } else if (msg.status === 'OK') {
            console.log(`Seq #${String(msg.seq).padStart(2, '0')} | Raw: ${String(msg.raw_bytes).padStart(3, ' ')} B -> Comp: ${String(msg.comp_bytes).padStart(2, ' ')} B | Ratio: ${msg.ratio.toFixed(2)}x | Saved: ${msg.savings.toFixed(1)}%`);
            sendNext();
        }
    } catch (e) {
        console.error('Failed to parse line:', line);
    }
});

function sendNext() {
    if (logIndex < sampleLogs.length) {
        encoder.stdin.write(sampleLogs[logIndex++] + '\n');
    } else {
        encoder.stdin.write('QUIT\n');
        console.log('[*] Finished streaming all logs.');
    }
}
