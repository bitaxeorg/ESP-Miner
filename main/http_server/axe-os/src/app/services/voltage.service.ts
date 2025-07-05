import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';

// Voltage monitoring interfaces
export interface AsicVoltage {
  id: number;
  voltage: number;
  valid: boolean;
}

export interface ChainVoltage {
  chain_id: number;
  total_voltage: number;
  average_voltage: number;
  min_voltage: number;
  max_voltage: number;
  failed_asics: number;
  suggested_frequency: number;
  asics: AsicVoltage[];
}

export interface VoltageMonitorStatus {
  enabled: boolean;
  hardware_present: boolean;
  scan_interval_ms: number;
  chains?: ChainVoltage[];
}

@Injectable({
  providedIn: 'root'
})
export class VoltageService {
  constructor(private http: HttpClient) { }

  getVoltageStatus(): Observable<VoltageMonitorStatus> {
    return this.http.get<VoltageMonitorStatus>('/api/voltage');
  }
}
