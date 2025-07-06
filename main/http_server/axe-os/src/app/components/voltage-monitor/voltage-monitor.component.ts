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
  // Add for collapsible view
  expanded?: boolean;
}

type ViewMode = 'single-column' | 'multi-column' | 'collapsible' | 'tree';

@Component({
  selector: 'app-voltage-monitor',
  templateUrl: './voltage-monitor.component.html',
  styleUrls: ['./voltage-monitor.component.scss']
})
export class VoltageMonitorComponent implements OnInit, OnDestroy {
	private treeData: any[] = [];

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

	viewOptions = [
    { label: 'Single', value: 'single-column', icon: 'pi pi-bars' },
    { label: 'Multi', value: 'multi-column', icon: 'pi pi-th-large' },
    { label: 'Collapsible', value: 'collapsible', icon: 'pi pi-list' },
    { label: 'Tree', value: 'tree', icon: 'pi pi-sitemap' }
  ];

  private destroy$ = new Subject<void>();
  private refreshInterval = 5000; // 5 seconds
  
  // View controls
  currentView: ViewMode = 'single-column';
  allExpanded = false;

  constructor(
    private voltageService: VoltageService,
    private systemService: SystemService
  ) {}

  ngOnInit(): void {
    // Load saved view preference
    const savedView = localStorage.getItem('voltage-monitor-view');
    if (savedView) {
      this.currentView = savedView as ViewMode;
    }
    
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

  setView(view: ViewMode): void {
    this.currentView = view;
    localStorage.setItem('voltage-monitor-view', view);
    // Clear tree cache when switching views
    if (view !== 'tree') {
      this.treeData = [];
    }
  }

  toggleChain(chainId: number): void {
    if (this.combinedData && this.combinedData.chains) {
      const chain = this.combinedData.chains.find(c => c.chain_id === chainId);
      if (chain) {
        chain.expanded = !chain.expanded;
      }
    }
  }

  toggleAllChains(): void {
    if (this.combinedData && this.combinedData.chains) {
      this.allExpanded = !this.allExpanded;
      this.combinedData.chains.forEach(chain => {
        chain.expanded = this.allExpanded;
      });
    }
  }

  onNodeExpand(event: any): void {
    console.log('Node expand attempt:', event);
  }

  onNodeCollapse(event: any): void {
    console.log('Node collapse attempt:', event);
  }

  onNodeSelect(event: any): void {
    console.log('Node selected:', event);
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

      // Preserve expanded state if it exists
      const existingChain = this.combinedData?.chains?.find(c => c.chain_id === chain.chain_id);
      
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
        failed_asics: failedAsics,
        expanded: existingChain?.expanded ?? false
      };
    });

    this.combinedData = {
      enabled: this.voltageStatus.enabled,
      hardware_present: this.voltageStatus.hardware_present,
      scan_interval_ms: this.voltageStatus.scan_interval_ms,
      chains
    };
  }
getTreeData(): any[] {
  // Only rebuild if we don't have cached data
  if (this.treeData.length === 0 && this.combinedData && this.combinedData.chains) {
    console.log('Building tree data...');
    this.treeData = [{
      label: 'All Chains',
      expanded: true,
      expandedIcon: 'pi pi-folder-open',
      collapsedIcon: 'pi pi-folder',
      children: this.combinedData.chains.map(chain => ({
        label: `Chain ${chain.chain_id}`,
        expanded: false,
        expandedIcon: 'pi pi-folder-open',
        collapsedIcon: 'pi pi-folder',
        children: [
          {
            label: 'Statistics',
            icon: 'pi pi-chart-bar',
            expanded: false,
            children: [
              {
                label: `Average Voltage: ${chain.average_voltage.toFixed(2)}V`,
                icon: 'pi pi-bolt',
                leaf: true
              },
              {
                label: `Total Hashrate: ${(chain.total_hashrate / 1000).toFixed(2)} TH/s`,
                icon: 'pi pi-server',
                leaf: true
              },
              {
                label: `Healthy ASICs: ${chain.healthy_asics}`,
                icon: 'pi pi-check-circle',
                styleClass: 'text-green-600',
                leaf: true
              },
              {
                label: `Degraded ASICs: ${chain.degraded_asics}`,
                icon: 'pi pi-exclamation-circle',
                styleClass: 'text-orange-600',
                leaf: true
              },
              {
                label: `Failed ASICs: ${chain.failed_asics}`,
                icon: 'pi pi-times-circle',
                styleClass: 'text-red-600',
                leaf: true
              }
            ]
          },
          {
            label: `ASICs (${chain.asics.length})`,
            icon: 'pi pi-microchip',
            expanded: false,
            children: chain.asics.map(asic => ({
              label: `ASIC ${asic.id}`,
              expanded: false,
              icon: asic.status === 'healthy' ? 'pi pi-check' : 
                    asic.status === 'degraded' ? 'pi pi-exclamation-triangle' : 
                    'pi pi-times',
              styleClass: asic.status === 'healthy' ? 'text-green-600' : 
                         asic.status === 'degraded' ? 'text-orange-600' : 
                         'text-red-600',
              children: [
                {
                  label: `Voltage: ${asic.voltage.toFixed(3)}V`,
                  icon: 'pi pi-bolt',
                  leaf: true
                },
                {
                  label: `Frequency: ${asic.frequency} MHz`,
                  icon: 'pi pi-chart-line',
                  leaf: true
                },
                {
                  label: `Hashrate: ${asic.hashrate.toFixed(1)} GH/s`,
                  icon: 'pi pi-server',
                  leaf: true
                },
                {
                  label: `Status: ${asic.status}`,
                  styleClass: asic.status === 'healthy' ? 'text-green-600' : 
                             asic.status === 'degraded' ? 'text-orange-600' : 
                             'text-red-600',
                  leaf: true
                }
              ]
            }))
          }
        ]
      }))
    }];
  }
  return this.treeData;
}
}
