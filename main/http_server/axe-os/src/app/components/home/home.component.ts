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
  public fallbackQuickLink$: Observable<string | undefined>;
  public expectedHashRate$: Observable<number | undefined>;


  public chartOptions: any;
  public dataLabel: number[] = [];
  public dataData: number[] = [];
  public dataDataAverage: number[] = [];
  public chartData?: any;

  public maxPower: number = 50;
  public maxTemp: number = 75;
  public maxFrequency: number = 800;

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
          label: 'Hashrate',
          data: [],
          fill: false,
          backgroundColor: primaryColor,
          borderColor: primaryColor,
          tension: 0,
          pointRadius: 2,
          pointHoverRadius: 5,
          borderWidth: 1
        },
        {
          type: 'line',
          label: 'Average Hashrate',
          data: [],
          fill: false,
          backgroundColor: textColorSecondary,
          borderColor: textColorSecondary,
          tension: 0,
          pointRadius: 0,
          borderWidth: 2,
          borderDash: [5, 5]
        }
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
        },
        tooltip: {
          callbacks: {
            label: function(tooltipItem: any) {
              let label = tooltipItem.dataset.label || '';
              if (label) {
                label += ': ';
              }
              label += HashSuffixPipe.transform(tooltipItem.raw);
              return label;
            }
          }
        },
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

        if (this.dataData.length >= 720) {
          this.dataData.shift();
          this.dataLabel.shift();
        }

        this.chartData.labels = this.dataLabel;
        this.chartData.datasets[0].data = this.dataData;

        // Calculate average hashrate and fill the array with the same value for the average line
        const averageHashrate = this.calculateAverage(this.dataData);
        this.chartData.datasets[1].data = Array(this.dataData.length).fill(averageHashrate);

        this.chartData = {
          ...this.chartData
        };

        this.maxPower = Math.max(50, info.power);
        this.maxTemp = Math.max(75, info.temp);
        this.maxFrequency = Math.max(800, info.frequency);

      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(1));
        info.current = parseFloat((info.current / 1000).toFixed(1));
        info.coreVoltageActual = parseFloat((info.coreVoltageActual / 1000).toFixed(2));
        info.coreVoltage = parseFloat((info.coreVoltage / 1000).toFixed(2));
        info.temp = parseFloat(info.temp.toFixed(1));

        return info;
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.expectedHashRate$ = this.info$.pipe(map(info => {
      return Math.floor(info.frequency * ((info.smallCoreCount * info.asicCount) / 1000))
    }))

    this.quickLink$ = this.info$.pipe(
      map(info => {
        if (info.stratumURL.includes('public-pool.io')) {
          const address = info.stratumUser.split('.')[0]
          return `https://web.public-pool.io/#/app/${address}`;
        } else if (info.stratumURL.includes('ocean.xyz')) {
          const address = info.stratumUser.split('.')[0]
          return `https://ocean.xyz/stats/${address}`;
        } else if (info.stratumURL.includes('solo.d-central.tech')) {
          const address = info.stratumUser.split('.')[0]
          return `https://solo.d-central.tech/#/app/${address}`;
        } else if (/solo[46]?.ckpool.org/.test(info.stratumURL)) {
          const address = info.stratumUser.split('.')[0]
          return `https://solo.ckpool.org/users/${address}`;
        } else {
          return undefined;
        }
      })
    )

    this.fallbackQuickLink$ = this.info$.pipe(
      map(info => {
        if (info.fallbackStratumURL.includes('public-pool.io')) {
          const address = info.fallbackStratumUser.split('.')[0]
          return `https://web.public-pool.io/#/app/${address}`;
        } else if (info.fallbackStratumURL.includes('ocean.xyz')) {
          const address = info.fallbackStratumUser.split('.')[0]
          return `https://ocean.xyz/stats/${address}`;
        } else if (info.fallbackStratumURL.includes('solo.d-central.tech')) {
          const address = info.fallbackStratumUser.split('.')[0]
          return `https://solo.d-central.tech/#/app/${address}`;
        } else if (/solo[46]?.ckpool.org/.test(info.fallbackStratumURL)) {
          const address = info.fallbackStratumUser.split('.')[0]
          return `https://solo.ckpool.org/users/${address}`;
        } else {
          return undefined;
        }
      })
    )

  }

  private calculateAverage(data: number[]): number {
    if (data.length === 0) return 0;
    const sum = data.reduce((sum, value) => sum + value, 0);
    return sum / data.length;
  }
}

