import { Component, ElementRef, Input, ViewChild, OnInit, OnDestroy } from '@angular/core';
import { Observable, Subject, takeUntil } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { LayoutService } from './service/app.layout.service';
import { SensitiveData } from 'src/app/services/sensitive-data.service';
import { DashboardEditService } from 'src/app/services/dashboard-edit.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated/models';
import { I18nService } from 'src/app/i18n/i18n.service';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html',
    standalone: false
})
export class AppTopBarComponent implements OnInit, OnDestroy {
  private destroy$ = new Subject<void>();

  public info$: Observable<ISystemInfo>;
  public sensitiveDataHidden: boolean = false;
  public isMiningPaused: boolean = false;
  public isWidgetPanelOpen = false;

  @Input() isAPMode: boolean = false;

  @ViewChild('menubutton') menuButton!: ElementRef;

  constructor(
    public layoutService: LayoutService,
    private systemService: SystemApiService,
    private liveDataService: LiveDataService,
    private toastr: ToastrService,
    private sensitiveData: SensitiveData,
    public dashboardEdit: DashboardEditService,
    private i18n: I18nService,
  ) {
    this.info$ = this.liveDataService.info$;
  }

  ngOnInit() {
    this.sensitiveData.hidden
      .pipe(takeUntil(this.destroy$))
      .subscribe((hidden: boolean) => {
        this.sensitiveDataHidden = hidden;
      });

    this.info$.pipe(takeUntil(this.destroy$)).subscribe((info: ISystemInfo) => {
      if ((info as any).miningPaused !== undefined) {
        this.isMiningPaused = (info as any).miningPaused;
      }
    });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  public toggleSensitiveData() {
    this.sensitiveData.toggle();
  }

  public toggleMiningPaused() {
    const action = this.isMiningPaused
      ? this.systemService.resumeMining()
      : this.systemService.pauseMining();
    const newPausedState = !this.isMiningPaused;
    action.subscribe({
      next: (response) => {
        this.isMiningPaused = newPausedState;
        this.toastr.success(response.message);
      },
      error: () => this.toastr.error(this.i18n.t('errors.mining_state_failed'))
    });
  }

  public restart() {
    if (confirm(this.i18n.t('confirm.restart_device'))) {
      this.systemService.restart().subscribe({
        next: () => this.toastr.success(this.i18n.t('messages.device_restarted')),
        error: (err) => this.toastr.error(this.i18n.t('errors.restart_failed', { error: err?.message ?? '' }))
      });
    }
  }
}
