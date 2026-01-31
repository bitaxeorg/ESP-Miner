import { Component, ElementRef, Input, ViewChild, OnInit, OnDestroy } from '@angular/core';
import { Observable, shareReplay, Subject, takeUntil } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LayoutService } from './service/app.layout.service';
import { SensitiveData } from 'src/app/services/sensitive-data.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated';
import { MenuItem } from 'primeng/api';

@Component({
  selector: 'app-topbar',
  templateUrl: './app.topbar.component.html'
})
export class AppTopBarComponent implements OnInit, OnDestroy {
  private destroy$ = new Subject<void>();

  public info$!: Observable<ISystemInfo>;
  public sensitiveDataHidden: boolean = false;
  public items!: MenuItem[];

  @Input() isAPMode: boolean = false;

  @ViewChild('menubutton') menuButton!: ElementRef;

  constructor(
    public layoutService: LayoutService,
    private systemService: SystemApiService,
    private toastr: ToastrService,
    private sensitiveData: SensitiveData,
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}))
  }

  ngOnInit() {
    this.sensitiveData.hidden
      .pipe(takeUntil(this.destroy$))
      .subscribe((hidden: boolean) => {
        this.sensitiveDataHidden = hidden;
      });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  public toggleSensitiveData() {
    this.sensitiveData.toggle();
  }

  public restart() {
    this.systemService.restart().subscribe({
      next: () => this.toastr.success('Device restarted'),
      error: () => this.toastr.error('Restart failed')
    });
  }
}
