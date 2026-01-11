import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'sats',
  pure: true
})
export class SatsPipe implements PipeTransform {
  private static _this = new SatsPipe();

  public static transform(value: number, args?: any): string {
    return this._this.transform(value, args);
  }

  transform(value: number, args?: any): string {
    if (!value) return '0 BTC';
    return (value / 100_000_000).toFixed(8) + ' BTC';
  }
}
