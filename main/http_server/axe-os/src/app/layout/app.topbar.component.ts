import { Component, ElementRef, Input, ViewChild, OnInit, OnDestroy, ChangeDetectorRef } from '@angular/core';
import { Observable, Subject, takeUntil } from 'rxjs';
import { ToastrService, ActiveToast } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { LayoutService } from './service/app.layout.service';
import { SensitiveData } from 'src/app/services/sensitive-data.service';
import { DashboardEditService } from 'src/app/services/dashboard-edit.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated/models';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html',
    standalone: false
})
export class AppTopBarComponent implements OnInit, OnDestroy {
  private destroy$ = new Subject<void>();
  private otaToastRef: ActiveToast<any> | null = null;
  private wasOtaUpdating = false;

  public info$: Observable<ISystemInfo>;
  public sensitiveDataHidden: boolean = false;
  public isMiningPaused: boolean = false;
  public isWidgetPanelOpen = false;
  private lastOtaStatus: string = '';

  @Input() isAPMode: boolean = false;

  @ViewChild('menubutton') menuButton!: ElementRef;

  constructor(
    public layoutService: LayoutService,
    private systemService: SystemApiService,
    private liveDataService: LiveDataService,
    private toastr: ToastrService,
    private sensitiveData: SensitiveData,
    public dashboardEdit: DashboardEditService,
    private cdr: ChangeDetectorRef,
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

      if (info.isFirmwareUpdate === 1) {
        this.wasOtaUpdating = true;
        const percent = info.firmwareUpdatePercent ?? 0;
        const status = info.firmwareUpdateStatus || 'Updating...';
        this.lastOtaStatus = status;
        const title = `Firmware Update`;
        const message = status;

        if (!this.otaToastRef) {
          this.otaToastRef = this.toastr.info(message, title, {
            disableTimeOut: true,
            tapToDismiss: false,
            closeButton: false,
            progressBar: true,
          });
        } else {
          if (this.otaToastRef.toastRef && this.otaToastRef.toastRef.componentInstance) {
            const inst = this.otaToastRef.toastRef.componentInstance;
            inst.title = title;
            inst.message = message;
            if (inst.toastPackage) {
              inst.toastPackage.title = title;
              inst.toastPackage.message = message;
            }
          }
          if (this.otaToastRef.portal && this.otaToastRef.portal.location) {
            const el = this.otaToastRef.portal.location.nativeElement as HTMLElement;
            if (el) {
              const titleEl = el.querySelector('.toast-title');
              if (titleEl) titleEl.textContent = title;

              const msgEl = el.querySelector('.toast-message');
              if (msgEl) msgEl.textContent = message;

              const progressEl = el.querySelector('.toast-progress') as HTMLElement;
              if (progressEl) progressEl.style.width = `${percent}%`;
            }
          }
          if (this.otaToastRef.portal && this.otaToastRef.portal.changeDetectorRef) {
            this.otaToastRef.portal.changeDetectorRef.markForCheck();
            this.otaToastRef.portal.changeDetectorRef.detectChanges();
          }
          this.cdr.detectChanges();
        }
      } else if (this.otaToastRef) {
        this.toastr.clear(this.otaToastRef.toastId);
        this.otaToastRef = null;
        if (this.wasOtaUpdating) {
          if (this.lastOtaStatus && (this.lastOtaStatus.includes('Error') || this.lastOtaStatus.includes('Failed'))) {
            this.toastr.error(`Firmware update failed: ${this.lastOtaStatus}`, 'Update Failed', { timeOut: 8000 });
          } else {
            this.toastr.success('Firmware update completed!');
          }
          this.wasOtaUpdating = false;
        }
      }
    });

    this.liveDataService.connected$.pipe(takeUntil(this.destroy$)).subscribe((connected: boolean) => {
      if (!connected && this.otaToastRef) {
        const title = 'Firmware Update';
        const message = 'Device is rebooting...';
        if (this.otaToastRef.toastRef && this.otaToastRef.toastRef.componentInstance) {
          const inst = this.otaToastRef.toastRef.componentInstance;
          inst.title = title;
          inst.message = message;
          if (inst.toastPackage) {
            inst.toastPackage.title = title;
            inst.toastPackage.message = message;
          }
        }
        if (this.otaToastRef.portal && this.otaToastRef.portal.location) {
          const el = this.otaToastRef.portal.location.nativeElement as HTMLElement;
          if (el) {
            const titleEl = el.querySelector('.toast-title');
            if (titleEl) titleEl.textContent = title;
            const msgEl = el.querySelector('.toast-message');
            if (msgEl) msgEl.textContent = message;
            const progressEl = el.querySelector('.toast-progress') as HTMLElement;
            if (progressEl) progressEl.style.width = '100%';
          }
        }
        if (this.otaToastRef.portal && this.otaToastRef.portal.changeDetectorRef) {
          this.otaToastRef.portal.changeDetectorRef.markForCheck();
          this.otaToastRef.portal.changeDetectorRef.detectChanges();
        }
        this.cdr.detectChanges();
      }
    });
  }

  ngOnDestroy() {
    if (this.otaToastRef) {
      this.toastr.clear(this.otaToastRef.toastId);
      this.otaToastRef = null;
    }

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
      error: () => this.toastr.error('Failed to change mining state')
    });
  }

  public restart() {
    this.systemService.restart().subscribe({
      next: () => this.toastr.success('Device restarted'),
      error: () => this.toastr.error('Restart failed')
    });
  }
}
