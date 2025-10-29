import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'byteSuffix'
})
export class ByteSuffixPipe implements PipeTransform {

  private static _this = new ByteSuffixPipe();

  public static transform(value: number): string {
    return this._this.transform(value);
  }

  public transform(value: number): string {

    if (value == null || value < 0) {
      return '0';
    }

    const suffixes = [' B', ' kB', ' MB', ' GB', ' TB', ' PB', ' EB'];

    let power = Math.floor(Math.log10(value) / 3);
    if (power < 0) {
      power = 0;
    }
    const scaledValue = value / Math.pow(1000, power);
    const suffix = suffixes[power];

    if (scaledValue < 10) {
      return scaledValue.toFixed(2) + suffix;
    } else if (scaledValue < 100) {
      return scaledValue.toFixed(1) + suffix;
    }

    return scaledValue.toFixed(0) + suffix;
  }


}
