import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'hashSuffix'
})
export class HashSuffixPipe implements PipeTransform {

  private static _this = new HashSuffixPipe();

  public static transform(value: number, args?: any): string {
    return this._this.transform(value, args);
  }

  public transform(value: number, args?: any): string {

    if (value == null || value < 0) {
      return '0';
    }

    const suffixes = [' H/s', ' Kh/s', ' Mh/s', ' Gh/s', ' Th/s', ' Ph/s', ' Eh/s'];

    let power = Math.floor(Math.log10(value) / 3);
    if (power < 0) {
      power = 0;
    }
    const scaledValue = value / Math.pow(1000, power);
    const suffix = suffixes[power];

    if (args?.tickmark) {
      return scaledValue + suffix;      
    }

    if (scaledValue < 10) {
      return scaledValue.toFixed(2) + suffix;
    } else if (scaledValue < 100) {
      return scaledValue.toFixed(1) + suffix;
    }

    return scaledValue.toFixed(0) + suffix;
  }
}
