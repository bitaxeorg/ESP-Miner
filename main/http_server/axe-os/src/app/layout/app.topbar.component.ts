import { Component, ElementRef, Input, ViewChild, OnInit, OnDestroy, ChangeDetectorRef } from '@angular/core';
import { Observable, Subject, takeUntil } from 'rxjs';
import { ToastrService, ActiveToast } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { LayoutService } from './service/app.layout.service';
import { SensitiveData } from 'src/app/services/sensitive-data.service';
import { DashboardEditService } from 'src/app/services/dashboard-edit.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated/models';
import { RouterLink } from '@angular/router';
import { AsyncPipe } from '@angular/common';
import { TooltipDirective } from '../directives/tooltip.directive';
import { WifiIconComponent } from '../components/wifi-icon/wifi-icon.component';
import { CheckboxComponent } from '../components/checkbox/checkbox.component';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'app-topbar',
  templateUrl: './app.topbar.component.html',
  imports: [RouterLink, TooltipDirective, WifiIconComponent, CheckboxComponent, FormsModule, AsyncPipe]
})
export class AppTopBarComponent implements OnInit, OnDestroy {
  private destroy$ = new Subject<void>();
  private otaToastRef: ActiveToast<any> | null = null;
  private wasOtaUpdating = false;

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
        const title = 'Firmware Update';

        if (!this.otaToastRef) {
          this.otaToastRef = this.toastr.info(status, title, {
            disableTimeOut: true,
            tapToDismiss: false,
            closeButton: false,
            progressBar: true,
          });
        }
        this.updateOtaToast(title, status, percent);
      } else if (this.otaToastRef) {
        this.toastr.clear(this.otaToastRef.toastId);
        this.otaToastRef = null;
        if (this.wasOtaUpdating) {
          this.wasOtaUpdating = false;
          const status = info.firmwareUpdateStatus;
          if (status && status !== 'Rebooting...') {
            this.toastr.error(status, 'Firmware Update Failed');
          } else {
            this.toastr.success('Firmware update completed!');
          }
        }
      }
    });

    this.liveDataService.connected$.pipe(takeUntil(this.destroy$)).subscribe((connected: boolean) => {
      if (!connected && this.otaToastRef) {
        this.updateOtaToast('Firmware Update', 'Device is rebooting...', 100);
      }
    });
  }

  private updateOtaToast(title: string, message: string, percent: number) {
    if (!this.otaToastRef) {
      return;
    }
    const inst = this.otaToastRef.toastRef?.componentInstance;
    if (inst) {
      inst.title = title;
      inst.message = message;
      if (inst.toastPackage) {
        inst.toastPackage.title = title;
        inst.toastPackage.message = message;
      }
      if (typeof inst.width?.set === 'function') {
        inst.width.set(percent);
      }
    }
    if (this.otaToastRef.portal?.changeDetectorRef) {
      this.otaToastRef.portal.changeDetectorRef.markForCheck();
      this.otaToastRef.portal.changeDetectorRef.detectChanges();
    }
    this.cdr.detectChanges();
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
    if (confirm('Are you sure you want to restart the device?')) {
      this.systemService.restart().subscribe({
        next: () => this.toastr.success('Device restarted'),
        error: () => this.toastr.error('Restart failed')
      });
    }
  }
}
