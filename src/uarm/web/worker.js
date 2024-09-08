importScripts('../src/uarm_web.js', './setimmediate/setimmediate.js', './crc.js');

(function () {
    const PCM_BUFFER_SIZE = (44100 / 60) * 10;
    const INITIAL_PAGE_POOL_PAGES = 256;
    const PAGE_POOL_GROWTH_FACTOR = 1.5;

    const messageQueue = [];
    let dispatchInProgress = false;

    let emulator;

    let framePool = [];
    let pcmPool = [];

    class DirtyPageTracker {
        constructor({
            pageSize,
            pageCount,
            getDataPtr,
            getDataSize,
            getDirtyPagesPtr,
            getDirtyPagesSize,
            module,
            name,
            crcCheck,
        }) {
            this.pageSize = pageSize;
            this.pageCount = pageCount;

            this.getDataPtr = getDataPtr;
            this.getDataSize = getDataSize;
            this.getDirtyPagesPtr = getDirtyPagesPtr;
            this.getDirtyPagesSize = getDirtyPagesSize;
            this.module = module;

            this.name = name;
            this.crcCheck = crcCheck;

            this.scheduledPages = new Uint32Array(pageCount);
            this.pagePool = new Uint8Array(INITIAL_PAGE_POOL_PAGES * pageSize);
            this.scheduledPageCount = 0;
        }

        getData() {
            const size = this.getDataSize();
            const ptr = this.getDataPtr();

            return this.module.HEAPU8.subarray(ptr, ptr + size);
        }

        getDirtyPages() {
            const size32 = this.getDirtyPagesSize() >>> 2;
            const ptr32 = this.getDirtyPagesPtr() >>> 2;

            return this.module.HEAPU32.subarray(ptr32, ptr32 + size32);
        }

        takeSnapshot() {
            if (this.scheduledPageCount > 0) {
                console.warn(`${this.name} pending, skipping...`);
            }

            const data = this.getData();
            const dirtyPages = this.getDirtyPages();

            let iPage = 0;
            let page = 0;
            for (let i = 0; i < dirtyPages.length; i++) {
                let pageSet32 = dirtyPages[i];

                if (pageSet32 === 0) {
                    page += 32;
                    continue;
                }

                for (let j = 0; j < 4; j++) {
                    let pageSet8 = pageSet32 & 0xff;
                    pageSet32 >>>= 8;

                    if (pageSet8 === 0) {
                        page += 8;
                        continue;
                    }

                    for (let mask = 1; mask < 0x100; mask <<= 1) {
                        if (!pageSet8 & mask) {
                            page++;
                            continue;
                        }

                        this.scheduledPages[iPage] = page;
                        const pagePoolOffset = iPage * this.pageSize;
                        const dataOffset = page * this.pageSize;

                        if (pagePoolOffset >= this.pagePool.length) this.growPagePool();

                        this.pagePool
                            .subarray(pagePoolOffset, pagePoolOffset + this.pageSize)
                            .set(data.subarray(dataOffset, dataOffset + this.pageSize));

                        iPage++;
                        page++;
                    }
                }
            }

            this.scheduledPageCount = iPage;

            if (this.scheduledPageCount > 0) {
                dirtyPages.fill(0);

                return {
                    scheduledPageCount: this.scheduledPageCount,
                    scheduledPages: this.scheduledPages.buffer,
                    pagePool: this.pagePool.buffer,
                    crc: this.crcCheck ? crc32(data) : undefined,
                };
            } else {
                return undefined;
            }
        }

        getTransferables() {
            return this.scheduledPageCount > 0 ? [this.scheduledPages.buffer, this.pagePool.buffer] : [];
        }

        onSnapshotDone(success, snapshot) {
            if (this.scheduledPages === 0) return;

            const scheduledPageCount = this.scheduledPageCount;

            this.scheduledPages = new Uint32Array(snapshot.scheduledPages);
            this.pagePool = new Uint8Array(snapshot.pagePool);
            this.scheduledPageCount = 0;

            if (success) return;

            console.log(`${this.name} failed`);

            const dirtyPages = this.getDirtyPages();

            for (let i = 0; i < scheduledPageCount; i++) {
                const page = this.scheduledPages[i];

                dirtyPages[page >>> 5] |= 1 << (page & 0x1f);
            }
        }

        growPagePool() {
            const newPagePool = new Uint8Array(
                Math.min(
                    Math.ceil((this.pagePool.length * PAGE_POOL_GROWTH_FACTOR) / this.pageSize) * this.pageSize,
                    this.pageCount * this.pageSize
                )
            );

            console.log(`growing ${this.name} page pool to ${newPagePool.length} bytes`);

            newPagePool.set(this.pagePool);
            this.pagePool = newPagePool;
        }
    }

    class Emulator {
        constructor(module, maxLoad, cyclesPerSecondLimit, crcCheck, { onSpeedDisplay, onFrame, log, postSnapshot }) {
            this.module = module;
            this.onSpeedDisplay = onSpeedDisplay;
            this.onFrame = onFrame;
            this.log = log;
            this.postSnapshot = postSnapshot;

            this.cycle = module.cwrap('cycle', undefined, ['number']);
            this.getFrame = module.cwrap('getFrame', 'number', []);
            this.resetFrame = module.cwrap('resetFrame', undefined, []);
            this.currentIps = module.cwrap('currentIps', 'number', []);
            this.currentIpsMax = module.cwrap('currentIpsMax', 'number', []);
            this.setMaxLoad = module.cwrap('setMaxLoad', undefined, ['number']);
            this.setCyclesPerSecondLimit = module.cwrap('setCyclesPerSecondLimit', undefined, ['number']);
            this.getTimesliceSizeUsec = module.cwrap('getTimesliceSizeUsec', 'number', []);
            this.getTimestampUsec = module.cwrap('getTimestampUsec', 'number', []);
            this.penDown = module.cwrap('penDown', undefined, ['number', 'number']);
            this.penUp = module.cwrap('penUp', undefined, []);
            this.keyDown = module.cwrap('keyDown', undefined, ['number']);
            this.keyUp = module.cwrap('keyUp', undefined, ['number']);
            this.pendingSamples = module.cwrap('pendingSamples', 'number', []);
            this.popQueuedSamples = module.cwrap('popQueuedSamples', 'number', []);
            this.setPcmOutputEnabled = module.cwrap('setPcmOutputEnabled', undefined, ['number']);
            this.setPcmSuspended = module.cwrap('setPcmSuspended', undefined, ['number']);
            this.getNandDataSize = module.cwrap('getNandDataSize', 'number');

            this.amIDead = false;
            this.pcmEnabled = false;
            this.pcmPort = undefined;
            this.snapshotPending = false;

            this.setMaxLoad(maxLoad);
            this.setCyclesPerSecondLimit(cyclesPerSecondLimit);

            const nandSize = this.getNandDataSize();
            this.nandTracker = new DirtyPageTracker({
                pageSize: 4224,
                pageCount: (nandSize / 4224) | 0,
                name: 'NAND snapshot',
                crcCheck,
                getDataPtr: module.cwrap('getNandData', 'number'),
                getDataSize: this.getNandDataSize,
                getDirtyPagesPtr: module.cwrap('getNandDirtyPages', 'number'),
                getDirtyPagesSize: module.cwrap('getNandDirtyPagesSize', 'number'),
                module,
            });
        }

        static async create(nor, nand, sd, maxLoad, cyclesPerSecondLimit, crcCheck, env) {
            const { log, binary } = env;
            let module;

            module = await createModule({
                noInitialRun: true,
                print: log,
                printErr: log,
                locateFile: () => `../src/${binary}`,
            });

            const malloc = module.cwrap('malloc', 'number', ['number']);

            let norPtr = malloc(nor.length);
            let nandPtr = malloc(nand.length);
            let sdPtr = sd ? malloc(sd.length) : 0;

            module.HEAPU8.subarray(norPtr, norPtr + nor.length).set(nor);
            module.HEAPU8.subarray(nandPtr, nandPtr + nand.length).set(nand);
            if (sd) module.HEAPU8.subarray(sdPtr, sdPtr + sd.length).set(sd);

            module.callMain([]);

            module.ccall(
                'webMain',
                undefined,
                ['number', 'number', 'number', 'number', 'number', 'number'],
                [norPtr, nor.length, nandPtr, nand.length, sdPtr, sd ? sd.length : 0]
            );

            return new Emulator(module, maxLoad, cyclesPerSecondLimit, crcCheck, env);
        }

        stop() {
            if (this.amIDead) return;

            if (this.immediateHandle) clearImmediate(this.immediateHandle);
            if (this.timeoutHandle) clearTimeout(this.timeoutHandle);

            this.timeoutHandle = this.immediateHandle = undefined;

            this.log('emulator stopped');
        }

        start() {
            if (this.amIDead) return;

            if (this.timeoutHandle || this.immediateHandle) return;
            this.lastSnapshot = Number(this.getTimestampUsec());

            const schedule = () => {
                const now64 = this.getTimestampUsec();
                const now = Number(now64);

                try {
                    this.cycle(now64);
                } catch (e) {
                    this.amIDead = true;
                    console.error(e);
                    this.log(e.message);

                    return;
                }

                this.render();
                this.processAudio();

                const timesliceRemainning =
                    (this.getTimesliceSizeUsec() - Number(this.getTimestampUsec()) + now) / 1000;
                this.timeoutHandle = this.immediateHandle = undefined;

                if (timesliceRemainning < 5) this.immediateHandle = setImmediate(schedule);
                else this.timeoutHandle = setTimeout(schedule, timesliceRemainning);

                if (now - this.lastSnapshot > 1000000) {
                    this.triggerSnapshot();
                    this.updateSpeedDisplay();

                    this.lastSnapshot = now;
                }
            };

            this.log('emulator running');
            schedule();
        }

        triggerSnapshot() {
            if (this.snapshotPending) return;

            const snapshotNand = this.nandTracker.takeSnapshot();
            if (!snapshotNand) return;

            this.postSnapshot({ nand: snapshotNand }, this.nandTracker.getTransferables());
            this.snapshotPending = true;
        }

        snapshotDone(success, snapshot) {
            this.nandTracker.onSnapshotDone(success, snapshot.nand);

            this.snapshotPending = false;
        }

        render() {
            if (!this.module) return;

            const framePtr = this.getFrame() >>> 2;
            if (!framePtr) return;

            const frame = this.module.HEAPU32.subarray(framePtr, framePtr + 320 * 320);

            if (framePool.length === 0) {
                this.onFrame(frame.slice().buffer);
            } else {
                const frameCopy = new Uint32Array(framePool.pop());
                frameCopy.set(frame);

                this.onFrame(frameCopy.buffer);
            }

            this.resetFrame();
        }

        getPcmBuffer() {
            if (pcmPool.length > 0) return new Uint32Array(pcmPool.pop());

            return new Uint32Array(PCM_BUFFER_SIZE);
        }

        processAudio() {
            if (!this.module || !this.pcmPort || !this.pcmEnabled) return;

            const pendingSamples = this.pendingSamples();
            if (pendingSamples === 0) return;

            const samplesPtr = this.popQueuedSamples() >>> 2;
            const samples = this.getPcmBuffer();

            samples.set(
                this.module.HEAPU32.subarray(samplesPtr, samplesPtr + Math.min(pendingSamples, samples.length))
            );

            this.pcmPort.postMessage({ type: 'sample-data', count: pendingSamples, buffer: samples.buffer }, [
                samples.buffer,
            ]);
        }

        updateSpeedDisplay() {
            const currentIps = this.currentIps();
            const currentIpsMax = Number(this.currentIpsMax());

            this.onSpeedDisplay(
                `current ${(currentIps / 1e6).toFixed(2)} MIPS, limit ${(currentIpsMax / 1e6).toFixed(2)} MIPS -> ${(
                    (currentIps / currentIpsMax) *
                    100
                ).toFixed(2)}%`
            );
        }

        setupPcm(port) {
            this.pcmPort = port;
            this.setPcmOutputEnabled(true);
            this.pcmEnabled = true;

            this.pcmPort.onmessage = (evt) => this.handlePcmMessage(evt.data);
        }

        handlePcmMessage(message) {
            switch (message.type) {
                case 'suspend-pcm':
                    this.setPcmSuspended(true);
                    break;

                case 'resume-pcm':
                    this.setPcmSuspended(false);
                    break;

                case 'return-buffer':
                    pcmPool.push(message.buffer);
                    break;

                default:
                    console.error(`worker: invalid PCM message ${message.type}`);
                    break;
            }
        }
    }

    function mapButton(name) {
        switch (name) {
            case 'cal':
                return 1;

            case 'phone':
                return 2;

            case 'todo':
                return 3;

            case 'notes':
                return 4;

            case 'up':
                return 5;

            case 'down':
                return 6;

            default:
                return -1;
        }
    }

    function postReady() {
        postMessage({ type: 'ready' });
    }

    function postFrame(data) {
        postMessage({ type: 'frame', data }, [data]);
    }

    function postSpeed(text) {
        postMessage({ type: 'speed', text });
    }

    function postLog(message) {
        postMessage({ type: 'log', message });
    }

    function postError(reason) {
        postMessage({ type: 'error', reason });
    }

    function postInitialized() {
        postMessage({ type: 'initialized' });
    }

    function postSnapshot(snapshot, transferables) {
        postMessage(
            {
                type: 'snapshot',
                snapshot,
            },
            transferables
        );
    }

    async function handleMessage(message) {
        let assertEmulator = (context) => {
            if (!emulator) {
                throw new Error(`${context}: emulator not running`);
            }
        };

        switch (message.type) {
            case 'initialize':
                emulator = await Emulator.create(
                    message.nor,
                    message.nand,
                    message.sd,
                    message.maxLoad,
                    message.cyclesPerSecondLimit,
                    message.crcCheck,
                    {
                        onFrame: postFrame,
                        onSpeedDisplay: postSpeed,
                        postSnapshot,
                        log: postLog,
                        binary: message.binary,
                    }
                );

                postInitialized();

                break;

            case 'start':
                assertEmulator('start');
                emulator.start();
                break;

            case 'stop':
                assertEmulator('stop');
                emulator.stop();
                break;

            case 'setMaxLoad':
                assertEmulator('setMaxLoad');
                emulator.setMaxLoad(message.maxLoad);
                break;

            case 'setCyclesPerSecondLimit':
                assertEmulator('cyclesPerSecondLimit');
                emulator.setCyclesPerSecondLimit(message.cyclesPerSecondLimit);
                break;

            case 'penDown':
                assertEmulator('penDown');
                emulator.penDown(message.x, message.y);
                break;

            case 'penUp':
                assertEmulator('penUp');
                emulator.penUp();
                break;

            case 'buttonDown': {
                const button = mapButton(message.button);
                if (button < 0) {
                    console.error(`ignoring unknown button ${message.button}`);
                    return;
                }

                assertEmulator('buttonDown');

                emulator.keyDown(button);
                break;
            }

            case 'buttonUp': {
                const button = mapButton(message.button);
                if (button < 0) {
                    console.error(`ignoring unknown button ${message.button}`);
                    return;
                }

                assertEmulator('buttonUp');

                emulator.keyUp(button);
                break;
            }

            case 'returnFrame':
                framePool.push(message.frame);
                break;

            case 'setupPcm':
                assertEmulator('setupPcm');

                emulator.setupPcm(message.port);
                break;

            case 'disablePcm':
                assertEmulator('disablePcm');

                emulator.setPcmOutputEnabled(false);
                break;

            case 'enablePcm':
                assertEmulator('enablePcm');

                emulator.setPcmOutputEnabled(true);
                break;

            case 'snapshotDone':
                assertEmulator('snapshotDone');

                emulator.snapshotDone(message.success, message.snapshot);
                break;

            default:
                console.error('unknown message from main thread', message);
        }
    }

    async function dispatchMessages() {
        if (dispatchInProgress || messageQueue.length === 0) return;
        dispatchInProgress = true;

        while (messageQueue.length > 0) {
            try {
                await handleMessage(messageQueue.shift());
            } catch (e) {
                postError(e);
            }
        }

        dispatchInProgress = false;
    }

    async function main() {
        postReady();

        onmessage = (e) => {
            messageQueue.push(e.data);
            dispatchMessages();
        };
    }

    main().catch((e) => console.error(e));
})();
