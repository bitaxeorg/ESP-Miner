import { Component, ElementRef, Input, ViewChild, OnInit, OnDestroy } from '@angular/core';
import { Observable, shareReplay, Subject, takeUntil } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LayoutService } from './service/app.layout.service';
import { SensitiveData } from 'src/app/services/sensitive-data.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated';
import { MenuItem } from 'primeng/api';
import { I18nService } from 'src/app/i18n/i18n.service';

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
    private i18n: I18nService,
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
      next: () => this.toastr.success(this.i18n.t('messages.device_restarted')),
      error: (err) => this.toastr.error(this.i18n.t('errors.restart_failed', { error: err?.message ?? '' }))
    });
  }
}
