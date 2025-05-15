import { Component, ElementRef, Input, ViewChild } from '@angular/core';
import { MenuItem } from 'primeng/api';

import { LayoutService } from './service/app.layout.service';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html',
    standalone: false
})
export class AppTopBarComponent {

    items!: MenuItem[];

    @Input() isAPMode: boolean = false;

    @ViewChild('menubutton') menuButton!: ElementRef;

    @ViewChild('topbarmenubutton') topbarMenuButton!: ElementRef;

    @ViewChild('topbarmenu') menu!: ElementRef;

    constructor(public layoutService: LayoutService,
    ) { }


}
