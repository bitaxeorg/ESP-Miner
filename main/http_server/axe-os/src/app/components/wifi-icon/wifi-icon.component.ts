import { Component, Input } from '@angular/core';

@Component({
  selector: 'wifi-icon',
  templateUrl: './wifi-icon.component.html',
  styleUrls: ['./wifi-icon.component.scss'],
})
export class WifiIconComponent {
  @Input() rssi: number = 0;

  constructor() {}
}
