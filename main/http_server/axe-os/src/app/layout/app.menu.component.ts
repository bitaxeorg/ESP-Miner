import { Component, OnInit } from '@angular/core';

import { LayoutService } from './service/app.layout.service';

@Component({
    selector: 'app-menu',
    templateUrl: './app.menu.component.html'
})
export class AppMenuComponent implements OnInit {

    model: any[] = [];

    constructor(public layoutService: LayoutService) { }

    ngOnInit() {
        this.model = [
            {
                label: 'Menu',
                items: [
                    { label: 'Dashboard', icon: 'pi pi-fw pi-home', routerLink: ['/'] },
                    { label: 'Swarm', icon: 'pi pi-fw pi-share-alt', routerLink: ['swarm'] },
                    { label: 'Settings', icon: 'pi pi-fw pi-cog', routerLink: ['settings'] },
                    { label: 'Logs', icon: 'pi pi-fw pi-list', routerLink: ['logs'] },

                ]
            }

        ];
    }
}
