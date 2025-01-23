import { HttpClient, HttpEvent } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { delay, Observable, of } from 'rxjs';
import { eASICModel } from 'src/models/enum/eASICModel';
import { ISystemInfo } from 'src/models/ISystemInfo';

import { environment } from '../../environments/environment';

@Injectable({
  providedIn: 'root'
})
export class SystemService {

  constructor(
    private httpClient: HttpClient
  ) { }

  public getInfo(uri: string = ''): Observable<ISystemInfo> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/v2/system/info`) as Observable<ISystemInfo>;
    } else {
      return of(
        {
          power: 11.670000076293945,
          voltage: 5208.75,
          current: 2237.5,
          temp: 60,
          vrTemp: 45,
          maxPower: 25,
          nominalVoltage: 5,
          hashrate: 475,
          bestDiff: "0",
          bestSessionDiff: "0",
          freeHeap: 200504,
          coreVoltage: 1200,
          coreVoltageActual: 1200,
          hostname: "Bitaxe",
          macAddr: "2C:54:91:88:C9:E3",
          ssid: "default",
          wifiPass: "password",
          wifiStatus: "Connected!",
          apEnabled: 0,
          sharesAccepted: 1,
          sharesRejected: 0,
          sharesRejectedReasons: [],
          uptimeSeconds: 38,
          asicCount: 1,
          smallCoreCount: 672,
          asicModel: eASICModel.BM1366,
          stratumURL: "public-pool.io",
          stratumPort: 21496,
          fallbackStratumURL: "test.public-pool.io",
          fallbackStratumPort: 21497,
          stratumUser: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d.bitaxe-U1",
          fallbackStratumUser: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d.bitaxe-U1",
          isUsingFallbackStratum: true,
          frequency: 485,
          version: "2.0",
          idfVersion: "v5.1.2",
          boardVersion: "204",
          flipScreen: 1,
          invertScreen: 0,
          displayTimeout: 0,
          autoFanSpeed: 1,
          fanSpeed: 100,
          tempTarget: 60,
          fanRPM: 0,

          boardtemp1: 30,
          boardtemp2: 40,
          overheatMode: 0
        }
      ).pipe(delay(1000));
    }
  }

  public restart(uri: string = '') {
    return this.httpClient.post(`${uri}/api/v2/system/restart`, {}, {responseType: 'text'});
  }

  public updateSystem(uri: string = '', update: any) {
    return this.httpClient.patch(`${uri}/api/v2/system`, update);
  }


  private otaUpdate(file: File | Blob, url: string) {
    return new Observable<HttpEvent<string>>((subscriber) => {
      const reader = new FileReader();

      reader.onload = (event: any) => {
        const fileContent = event.target.result;

        return this.httpClient.post(url, fileContent, {
          reportProgress: true,
          observe: 'events',
          responseType: 'text', // Specify the response type
          headers: {
            'Content-Type': 'application/octet-stream', // Set the content type
          },
        }).subscribe({
          next: (event) => {
            subscriber.next(event);
          },
          error: (err) => {
            subscriber.error(err)
          },
          complete: () => {
            subscriber.complete();
          }
        });
      };
      reader.readAsArrayBuffer(file);
    });
  }

  public performOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/v2/system/ota`);
  }
  public performWWWOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/v2/system/otawww`);
  }


  public getAsicSettings(uri: string = ''): Observable<{
    ASICModel: eASICModel;
    defaultFrequency: number;
    frequencyOptions: number[];
    defaultVoltage: number;
    voltageOptions: number[];
  }> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/v2/system/asic`) as Observable<{
        ASICModel: eASICModel;
        defaultFrequency: number;
        frequencyOptions: number[];
        defaultVoltage: number;
        voltageOptions: number[];
      }>;
    } else {
      // Mock data for development
      return of({
        ASICModel: eASICModel.BM1366,
        defaultFrequency: 485,
        frequencyOptions: [400, 425, 450, 475, 485, 500, 525, 550, 575],
        defaultVoltage: 1200,
        voltageOptions: [1100, 1150, 1200, 1250, 1300]
      }).pipe(delay(1000));
    }
  }

  public getSwarmInfo(uri: string = ''): Observable<{ ip: string }[]> {
    return this.httpClient.get(`${uri}/api/v2/swarm/info`) as Observable<{ ip: string }[]>;
  }

  public updateSwarm(uri: string = '', swarmConfig: any) {
    return this.httpClient.patch(`${uri}/api/v2/swarm`, swarmConfig);
  }
}
