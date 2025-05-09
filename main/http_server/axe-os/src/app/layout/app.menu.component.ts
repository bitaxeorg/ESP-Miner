import { Component, OnInit } from '@angular/core';
import { ToastrService } from 'ngx-toastr';

import { SystemService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';

@Component({
    selector: 'app-menu',
    templateUrl: './app.menu.component.html'
})
export class AppMenuComponent implements OnInit {

    model: any[] = [];

    constructor(public layoutService: LayoutService,
        private systemService: SystemService,
        private toastr: ToastrService
    ) { }

    ngOnInit() {
        this.model = [
            {
                label: 'Menu',
                items: [
                    { label: 'Dashboard', icon: 'pi pi-fw pi-home', routerLink: ['/'] },
                    { label: 'Swarm', icon: 'pi pi-fw pi-share-alt', routerLink: ['swarm'] },
                    { label: 'Network', icon: 'pi pi-fw pi-wifi', routerLink: ['network'] },
                    { label: 'Pool Settings', icon: 'pi pi-fw pi-server', routerLink: ['pool'] },
                    { label: 'Customization', icon: 'pi pi-fw pi-palette', routerLink: ['design'] },
                    { label: 'Settings', icon: 'pi pi-fw pi-cog', routerLink: ['settings'] },
                    { label: 'Logs', icon: 'pi pi-fw pi-list', routerLink: ['logs'] },
                    { label: 'Whitepaper', icon: 'pi pi-fw pi-bitcoin', command: () => window.open('/bitcoin.pdf', '_blank') },
                ]
            }
        ];
    }

    public restart() {
        this.systemService.restart().subscribe(res => {
        });
        this.toastr.success('Success!', 'Bitaxe restarted');
    }


    callStartMining() {
        this.toastr.info('Requesting to start mining...', 'Mining Start Requested');
        this.systemService.startMining().subscribe({
            next: () => {
                this.toastr.success('Successfully started mining!', 'Mining Started');
            },
            error: (err) => {
                this.toastr.error('Failed to start mining.', 'Error');
                console.error('Error starting mining:', err);
            }
        });
    }

    callStopMining() {
        this.toastr.info('Requesting to stop mining...', 'Mining Stop Requested');
        this.systemService.stopMining().subscribe({
            next: () => {
                this.toastr.success('Successfully stopped mining!', 'Mining Stopped');
            },
            error: (err) => {
                this.toastr.error('Failed to stop mining.', 'Error');
                console.error('Error stopping mining:', err);
            }
        });
    }
}
