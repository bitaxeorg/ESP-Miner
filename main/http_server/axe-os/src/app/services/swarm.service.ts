import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { SystemService } from '../generated/api/system.service';
import { Configuration } from '../generated/configuration';
import { SystemInfo } from '../generated/model/system-info';
import { Observable } from 'rxjs/internal/Observable';
import { Settings } from '../generated';

@Injectable({
  providedIn: 'root'
})
export class SwarmService {

  constructor(private http: HttpClient) { }

  public forIp(ip: string): SystemService {
    const config = new Configuration({
      basePath: `http://${ip}`,
    });

    return new SystemService(this.http, config.basePath || '', config);
  }

  public getSystemInfo(ip: string): Observable<SystemInfo> {
    return this.forIp(ip).getSystemInfo();
  }

  public restartSystem(ip: string): Observable<string> {
    return this.forIp(ip).restartSystem();
  }

  public updateSystemSettings(ip: string, settings: Settings): Observable<void> {
    return this.forIp(ip).updateSystemSettings(settings);
  }
}
