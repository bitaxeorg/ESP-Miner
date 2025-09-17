import { Component, ElementRef, Input, ViewChild } from '@angular/core';
import { Observable, shareReplay } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { SystemService } from 'src/app/services/system.service';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { LayoutService } from './service/app.layout.service';
import { MenuItem } from 'primeng/api';

@Component({
  selector: 'app-topbar',
  templateUrl: './app.topbar.component.html'
})
export class AppTopBarComponent {

  public info$!: Observable<ISystemInfo>;

  items!: MenuItem[];

  @Input() isAPMode: boolean = false;

  @ViewChild('menubutton') menuButton!: ElementRef;

  constructor(
    public layoutService: LayoutService,
    private systemService: SystemService,
    private toastr: ToastrService,
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}))
  }

  public restart() {
    this.systemService.restart().subscribe(() => {});
    this.toastr.success('Device restarted');
  }

  public isDesktop() {
    return window.matchMedia("(min-width: 991px)").matches;
  }
}
