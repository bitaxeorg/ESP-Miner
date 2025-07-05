import { Component, OnInit, OnDestroy } from '@angular/core';
import { Subject, takeUntil, interval } from 'rxjs';
import { VoltageService, VoltageMonitorStatus } from '../../services/voltage.service';

@Component({
  selector: 'app-voltage-monitor',
  templateUrl: './voltage-monitor.component.html',
  styleUrls: ['./voltage-monitor.component.scss']
})
export class VoltageMonitorComponent implements OnInit, OnDestroy {
  voltageStatus: VoltageMonitorStatus | null = null;
  isLoading = true;
  error: string | null = null;
  
  private destroy$ = new Subject<void>();

  constructor(private voltageService: VoltageService) {}

  ngOnInit(): void {
    this.loadVoltageStatus();
    
    // Poll every 5 seconds
    interval(5000)
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => this.loadVoltageStatus());
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private loadVoltageStatus(): void {
    this.voltageService.getVoltageStatus()
      .pipe(takeUntil(this.destroy$))
      .subscribe({
        next: (status) => {
          this.voltageStatus = status;
          this.isLoading = false;
          this.error = null;
        },
        error: (err) => {
          this.error = 'Failed to load voltage data';
          this.isLoading = false;
          console.error('Voltage monitoring error:', err);
        }
      });
  }
}
