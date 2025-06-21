import { HttpClient, HttpEvent } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { delay, Observable, of } from 'rxjs';
import { eASICModel } from 'src/models/enum/eASICModel';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { ISystemStatistics } from 'src/models/ISystemStatistics';

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
    }

    // Mock data for development
    return of(
      {
        power: 11.670000076293945,
        voltage: 5208.75,
        current: 2237.5,
        temp: 60,
        vrTemp: 45,
        maxPower: 25,
        nominalVoltage: 5,
        hashRate: 475,
        expectedHashrate: 420,
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
        wifiRSSI: -32,
        apEnabled: 0,
        sharesAccepted: 1,
        sharesRejected: 0,
        sharesRejectedReasons: [],
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
        idfVersion: "v5.1.2",
        boardVersion: "204",
        display: "SSD1306 (128x32)",
        rotation: 0,
        invertscreen: 0,
        displayTimeout: -1,
        autofanspeed: 1,
        fanspeed: 100,
        temptarget: 60,
        statsFrequency: 30,
        fanrpm: 0,

        boardtemp1: 30,
        boardtemp2: 40,
        overheat_mode: 0
      }
    ).pipe(delay(1000));
  }

  public getStatistics(uri: string = ''): Observable<ISystemStatistics> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/system/statistics/dashboard`) as Observable<ISystemStatistics>;
    }

    // Mock data for development
    return of({
      currentTimestamp: 61125,
      statistics: [
        [0,-1,14.45068359375,13131],
        [413.4903744405481,58.5,14.86083984375,18126],
        [410.7764830376959,59.625,15.03173828125,23125],
        [440.100549473198,60.125,15.1171875,28125],
        [430.5816012914026,60.75,15.1171875,33125],
        [452.5464981767163,61.5,15.1513671875,38125],
        [414.9564271189586,61.875,15.185546875,43125],
        [498.7294609150379,62.125,15.27099609375,48125],
        [411.1671601439723,62.5,15.30517578125,53125],
        [491.327834852684,63,15.33935546875,58125]
      ]
    }).pipe(delay(1000));
  }

  public restart(uri: string = '') {
    return this.httpClient.post(`${uri}/api/system/restart`, {}, {responseType: 'text'});
  }

  public updateSystem(uri: string = '', update: any) {
    if (environment.production) {
      return this.httpClient.patch(`${uri}/api/system`, update);
    } else {
      return of(true);
    }
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
    return this.otaUpdate(file, `/api/system/OTA`);
  }
  public performWWWOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/system/OTAWWW`);
  }


  public getAsicSettings(uri: string = ''): Observable<{
    ASICModel: eASICModel;
    defaultFrequency: number;
    frequencyOptions: number[];
    defaultVoltage: number;
    voltageOptions: number[];
  }> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/system/asic`) as Observable<{
        ASICModel: eASICModel;
        defaultFrequency: number;
        frequencyOptions: number[];
        defaultVoltage: number;
        voltageOptions: number[];
      }>;
    }

    // Mock data for development
    return of({
      ASICModel: eASICModel.BM1366,
      defaultFrequency: 485,
      frequencyOptions: [400, 425, 450, 475, 485, 500, 525, 550, 575],
      defaultVoltage: 1200,
      voltageOptions: [1100, 1150, 1200, 1250, 1300]
    }).pipe(delay(1000));
  }

  public getSwarmInfo(uri: string = ''): Observable<{ ip: string }[]> {
    return this.httpClient.get(`${uri}/api/swarm/info`) as Observable<{ ip: string }[]>;
  }

  public updateSwarm(uri: string = '', swarmConfig: any) {
    return this.httpClient.patch(`${uri}/api/swarm`, swarmConfig);
  }
}
