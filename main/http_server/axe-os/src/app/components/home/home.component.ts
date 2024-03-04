import { Component } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, switchMap, tap } from 'rxjs';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { SystemService } from 'src/app/services/system.service';
import { eASICModel } from 'src/models/enum/eASICModel';
import { ISystemInfo } from 'src/models/ISystemInfo';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent {


  public info$: Observable<ISystemInfo>;

  public quickLink$: Observable<string | undefined>;
  public expectedHashRate$: Observable<number | undefined>;


  public chartOptions: any;
  public dataLabel: number[] = [];
  public dataData: number[] = [];
  public chartData?: any;

  constructor(
    private systemService: SystemService
  ) {


    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');

    this.chartData = {
      labels: [],
      datasets: [
        {
          type: 'line',
          label: '10 Minute',
          data: [],
          fill: false,
          backgroundColor: primaryColor,
          borderColor: primaryColor,
          tension: .4,
          pointRadius: 1,
          borderWidth: 1
        },

      ]
    };

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          labels: {
            color: textColor
          }
        }
      },
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'hour', // Set the unit to 'minute'
          },
          ticks: {
            color: textColorSecondary
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false,
            display: true
          }
        },
        y: {
          ticks: {
            color: textColorSecondary,
            callback: (value: number) => HashSuffixPipe.transform(value)
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          }
        }
      }
    };


    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      tap(info => {

        this.dataData.push(info.hashRate * 1000000000);
        this.dataLabel.push(new Date().getTime());

        if (this.dataData.length >= 1000) {
          this.dataData.shift();
          this.dataLabel.shift();
        }

        this.chartData.labels = this.dataLabel;
        this.chartData.datasets[0].data = this.dataData;



        this.chartData = {
          ...this.chartData
        };

      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(1));
        info.current = parseFloat((info.current / 1000).toFixed(1));
        info.coreVoltageActual = parseFloat((info.coreVoltageActual / 1000).toFixed(2));
        info.coreVoltage = parseFloat((info.coreVoltage / 1000).toFixed(2));



        return info;
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.expectedHashRate$ = this.info$.pipe(map(info => {
      if (info.ASICModel === eASICModel.BM1366) {
        const version = parseInt(info.boardVersion);
        if (version >= 400 && version < 500) {
          return (info.frequency * ((894 * 6) / 1000))
        } else {
          return (info.frequency * (894 / 1000))
        }
      } else if (info.ASICModel === eASICModel.BM1397) {
        return (info.frequency * (672 / 1000))
      } else if (info.ASICModel === eASICModel.BM1368) {
        return (info.frequency * (1276 / 1000))
      }

      return undefined;

    }))

    this.quickLink$ = this.info$.pipe(
      map(info => {
        if (info.stratumURL.includes('public-pool.io')) {
          const address = info.stratumUser.split('.')[0]
          return `https://web.public-pool.io/#/app/${address}`;
        } else {
          return undefined;
        }
      })
    )



  }




}

