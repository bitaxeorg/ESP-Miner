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
      return this.httpClient.get(`${uri}/api/system/info`) as Observable<ISystemInfo>;
    } else {
      return of(
        {
          power: 11.670000076293945,
          voltage: 5208.75,
          current: 2237.5,
          temp: 60,
          vrTemp: 45,
          hashRate: 475,
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
          sharesAccepted: 1,
          sharesRejected: 0,
          uptimeSeconds: 38,
          asicCount: 1,
          smallCoreCount: 672,
          ASICModel: eASICModel.BM1366,
          stratumURL: "public-pool.io",
          stratumPort: 21496,
          fallbackStratumURL: "test.public-pool.io",
          fallbackStratumPort: 21497,
          stratumUser: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d.bitaxe-U1",
          fallbackStratumUser: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d.bitaxe-U1",
          isUsingFallbackStratum: true,
          frequency: 485,
          version: "2.0",
          boardVersion: "204",
          flipscreen: 1,
          invertscreen: 0,
          invertfanpolarity: 1,
          autofanspeed: 1,
          fanspeed: 100,
          fanrpm: 0,

          boardtemp1: 30,
          boardtemp2: 40,
          overheat_mode: 0
        }
      ).pipe(delay(1000));
    }
  }

  public restart(uri: string = '') {
    return this.httpClient.post(`${uri}/api/system/restart`, {});
  }

  public updateSystem(uri: string = '', update: any) {
    return this.httpClient.patch(`${uri}/api/system`, update);
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
          next: (e) => {

          },
          error: (err) => {
            subscriber.error(err)
          },
          complete: () => {
            subscriber.next()
            subscriber.complete();
          }
        });
      };
      reader.readAsArrayBuffer(file);
    });
  }

  public performOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/system/OTA`);
  }
  public performWWWOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/system/OTAWWW`);
  }


  public getSwarmInfo(uri: string = ''): Observable<{ ip: string }[]> {
    return this.httpClient.get(`${uri}/api/swarm/info`) as Observable<{ ip: string }[]>;
  }

  public updateSwarm(uri: string = '', swarmConfig: any) {
    return this.httpClient.patch(`${uri}/api/swarm`, swarmConfig);
  }
}
