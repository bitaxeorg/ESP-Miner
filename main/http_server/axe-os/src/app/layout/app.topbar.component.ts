import { Component, ElementRef, ViewChild } from '@angular/core';
import { ToastrService } from 'ngx-toastr';
import { MenuItem } from 'primeng/api';

import { SystemService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html'
})
export class AppTopBarComponent {

    items!: MenuItem[];

    @ViewChild('menubutton') menuButton!: ElementRef;

    @ViewChild('topbarmenubutton') topbarMenuButton!: ElementRef;

    @ViewChild('topbarmenu') menu!: ElementRef;

    constructor(public layoutService: LayoutService,
        private systemService: SystemService,
        private toastr: ToastrService
    ) { }



    public restart() {
        this.systemService.restart().subscribe(res => {

        });
        this.toastr.success('Success!', 'Bitaxe restarted');
    }

}
