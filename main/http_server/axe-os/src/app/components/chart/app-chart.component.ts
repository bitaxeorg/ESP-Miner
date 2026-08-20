import { Component, ElementRef, Input, NgZone, OnChanges, OnDestroy, SimpleChanges, ViewChild } from '@angular/core';
import { Chart, registerables } from 'chart.js';

Chart.register(...registerables);

@Component({
  selector: 'app-chart',
  template: `<div class="relative w-full h-full"><canvas #canvas></canvas></div>`,
  styles: [`
    :host {
      display: flex;
      flex-direction: column;
      width: 100%;
      height: 100%;
    }
  `],
  standalone: true
})
export class AppChartComponent implements OnChanges, OnDestroy {
  @Input() data: any;
  @Input() options: any;
  @Input() type: string = 'line';

  @ViewChild('canvas', { static: true }) canvas!: ElementRef<HTMLCanvasElement>;

  public chart: Chart | null = null;

  constructor(private ngZone: NgZone) {}

  ngOnChanges(changes: SimpleChanges) {
    if (changes['data'] || changes['options'] || changes['type']) {
      this.updateChart();
    }
  }

  ngOnDestroy() {
    this.destroyChart();
  }

  private destroyChart() {
    if (this.chart) {
      this.ngZone.runOutsideAngular(() => {
        this.chart?.destroy();
        this.chart = null;
      });
    }
  }

  private updateChart() {
    if (!this.canvas) return;

    this.ngZone.runOutsideAngular(() => {
      if (this.chart) {
        this.chart.data = this.data;
        if (this.options) {
          this.chart.options = this.options;
        }
        this.chart.update();
      } else {
        this.initChart();
      }
    });
  }

  private initChart() {
    this.destroyChart();
    const ctx = this.canvas.nativeElement.getContext('2d');
    if (!ctx) return;

    this.ngZone.runOutsideAngular(() => {
      this.chart = new Chart(ctx, {
        type: this.type as any,
        data: this.data,
        options: this.options
      });
    });
  }

  public refresh() {
    this.updateChart();
  }
}
