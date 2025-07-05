import { Component, OnInit, OnDestroy } from '@angular/core';
import { forkJoin, Subject, timer } from 'rxjs';
import { takeUntil } from 'rxjs/operators';
import { VoltageService, VoltageMonitorStatus, AsicVoltage } from '../../services/voltage.service';
import { SystemService } from '../../services/system.service';
import { ISystemInfo } from '../../../models/ISystemInfo';

interface CombinedAsicData {
  id: number;
  voltage: number;
  frequency: number;
  hashrate: number;
  status: 'healthy' | 'degraded' | 'fault';
  valid: boolean;
}

interface CombinedChainData {
  chain_id: number;
  asics: CombinedAsicData[];
  total_voltage: number;
  average_voltage: number;
  min_voltage: number;
  max_voltage: number;
  average_frequency: number;
  total_hashrate: number;
  healthy_asics: number;
  degraded_asics: number;
  failed_asics: number;
}

@Component({
  selector: 'app-voltage-monitor',
  templateUrl: './voltage-monitor.component.html',
  styleUrls: ['./voltage-monitor.component.scss']
})
export class VoltageMonitorComponent implements OnInit, OnDestroy {
  voltageStatus: VoltageMonitorStatus | null = null;
  systemInfo: ISystemInfo | null = null;
  combinedData: { 
    enabled: boolean;
    hardware_present: boolean;
    scan_interval_ms: number;
    chains: CombinedChainData[] 
  } | null = null;
  isLoading = true;
  error: string | null = null;
  private destroy$ = new Subject<void>();
  private refreshInterval = 5000; // 5 seconds

  constructor(
    private voltageService: VoltageService,
    private systemService: SystemService
  ) {}

  ngOnInit(): void {
    this.loadData();
    
    // Auto-refresh every 5 seconds
    timer(this.refreshInterval, this.refreshInterval)
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => this.loadData());
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private loadData(): void {
    forkJoin({
      voltage: this.voltageService.getVoltageStatus(),
      system: this.systemService.getInfo()
    }).subscribe({
      next: (data) => {
        this.voltageStatus = data.voltage;
        this.systemInfo = data.system;
        this.combineData();
        this.isLoading = false;
        this.error = null;
      },
      error: (err) => {
        this.error = 'Failed to load monitoring data';
        this.isLoading = false;
        console.error('Error loading data:', err);
      }
    });
  }

  private combineData(): void {
    if (!this.voltageStatus || !this.systemInfo) {
      return;
    }

    // Calculate per-ASIC values from system totals
    const totalAsics = this.voltageStatus.chains?.reduce((sum, chain) => sum + chain.asics.length, 0) || 1;
    const perAsicHashrate = (this.systemInfo.hashRate || 0) / totalAsics;
    const systemFrequency = this.systemInfo.frequency || 0;

    const chains: CombinedChainData[] = (this.voltageStatus.chains || []).map(chain => {
      const combinedAsics: CombinedAsicData[] = chain.asics.map((asic: AsicVoltage) => {
        // Determine status based on voltage thresholds
        let status: 'healthy' | 'degraded' | 'fault' = 'healthy';
        
        if (!asic.valid || asic.voltage < 0.8) {
          status = 'fault';
        } else if (asic.voltage < 1.1 || asic.voltage > 1.4) {
          status = 'degraded';
        }

        // Use suggested frequency if available, otherwise use system frequency
        const asicFrequency = chain.suggested_frequency || systemFrequency;
        
        // Estimate hash rate based on voltage (higher voltage typically means better performance)
        const voltageRatio = asic.valid ? asic.voltage / 1.2 : 0; // Assuming 1.2V is nominal
        const estimatedHashrate = perAsicHashrate * voltageRatio;

        return {
          id: asic.id,
          voltage: asic.voltage,
          frequency: asicFrequency,
          hashrate: estimatedHashrate,
          status,
          valid: asic.valid
        };
      });

      // Calculate statistics
      const healthyAsics = combinedAsics.filter(a => a.status === 'healthy').length;
      const degradedAsics = combinedAsics.filter(a => a.status === 'degraded').length;
      const failedAsics = combinedAsics.filter(a => a.status === 'fault').length;
      const avgFrequency = chain.suggested_frequency || systemFrequency;
      const totalHashrate = combinedAsics.reduce((sum, a) => sum + a.hashrate, 0);

      return {
        chain_id: chain.chain_id,
        asics: combinedAsics,
        total_voltage: chain.total_voltage,
        average_voltage: chain.average_voltage,
        min_voltage: chain.min_voltage,
        max_voltage: chain.max_voltage,
        average_frequency: avgFrequency,
        total_hashrate: totalHashrate,
        healthy_asics: healthyAsics,
        degraded_asics: degradedAsics,
        failed_asics: failedAsics
      };
    });

    this.combinedData = {
      enabled: this.voltageStatus.enabled,
      hardware_present: this.voltageStatus.hardware_present,
      scan_interval_ms: this.voltageStatus.scan_interval_ms,
      chains
    };
  }
}
