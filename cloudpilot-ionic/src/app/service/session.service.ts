import { DeviceId } from './../model/DeviceId';
import { EmulationService } from './emulation.service';
import { Injectable } from '@angular/core';
import { LoadingController } from '@ionic/angular';
import { Session } from '../model/Session';
import { SessionImage } from './file.service';
import { StorageService } from './storage.service';

@Injectable({
    providedIn: 'root',
})
export class SessionService {
    constructor(
        private emulationService: EmulationService,
        private storageService: StorageService,
        private loadingController: LoadingController
    ) {
        this.updateSessionsFromStorage();
    }

    async addSessionFromImage(image: SessionImage, name: string) {
        const session: Session = {
            id: -1,
            name,
            device: image.deviceId as DeviceId,
            ram: image.memory.length / 1024 / 1024,
            rom: '',
        };

        const loader = await this.loadingController.create({ message: 'Importing...' });
        await loader.present();

        await this.storageService.addSession(session, image.rom, image.memory, image.savestate);

        await this.updateSessionsFromStorage();
        await loader.dismiss();
    }

    async addSessionFromRom(rom: Uint8Array, name: string, device: DeviceId) {
        const session: Session = {
            id: -1,
            name,
            device,
            ram: (await this.emulationService.cloudpilot).minRamForDevice(device) / 1024 / 1024,
            rom: '',
        };

        const loader = await this.loadingController.create({ message: 'Importing...' });
        await loader.present();

        await this.storageService.addSession(session, rom);

        await this.updateSessionsFromStorage();
        await loader.dismiss();
    }

    getSessions(): Array<Session> {
        return this.sessions;
    }

    async deleteSession(session: Session): Promise<void> {
        await this.storageService.deleteSession(session);

        this.updateSessionsFromStorage();
    }

    async updateSession(session: Session): Promise<void> {
        await this.storageService.updateSession(session);

        this.updateSessionsFromStorage();
    }

    private async updateSessionsFromStorage(): Promise<void> {
        this.sessions = (await this.storageService.getAllSessions()).sort((x, y) => x.name.localeCompare(y.name));
    }

    private sessions: Array<Session> = [];
}