import { Component, Input } from '@angular/core';

@Component({
    selector: 'wifi-icon',
    templateUrl: './wifi-icon.component.html',
    styleUrls: ['./wifi-icon.component.scss'],
    standalone: false
})
export class WifiIconComponent {
  @Input() rssi: number = 0;

  get bars(): number {
    if (this.rssi > -50) return 4;
    if (this.rssi > -60) return 3;
    if (this.rssi > -70) return 2;
    if (this.rssi > -128) return 1;
    return 0;
  }
}
