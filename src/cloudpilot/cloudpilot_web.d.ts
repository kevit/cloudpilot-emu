import { DbBackup, ZipfileWalker, SessionImage, SkinLoader } from './web/binding/binding.d';
import 'emscripten';

import { Cloudpilot, RomInfo, VoidPtr } from './web/binding/binding';

export * from './web/binding/binding';

export interface Module extends EmscriptenModule {
    addFunction: typeof addFunction;
    getPointer(ptr: VoidPtr): number;
    UTF8ToString(charPtr: number): string;

    Cloudpilot: { new (): Cloudpilot };
    RomInfo: { new (buffer: VoidPtr, size: number): RomInfo };
    ZipfileWalker: {
        new (bufferSize: number, buffer: VoidPtr): ZipfileWalker;
    };
    SessionImage: { new (): SessionImage };
    SkinLoader: { new (name: string): SkinLoader };

    destroy(cloudpilot: Cloudpilot): void;
    destroy(romInfo: RomInfo): void;
    destroy(dbBackup: DbBackup): void;
    destroy(zipfileWalker: ZipfileWalker): void;
    destroy(sessionImage: SessionImage): void;
    destroy(skinLoader: SkinLoader): void;
}

declare const createModule: EmscriptenModuleFactory<Module>;

export default createModule;
