import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
    name: 'diffSuffix',
    pure: true
})
export class DiffSuffixPipe implements PipeTransform {

  private static _this = new DiffSuffixPipe();

  public static transform(value: number, digits: number = 2): string {
    return this._this.transform(value, digits);
  }

  public transform(value: number, digits: number = 2): string {
    if (value == null || value < 0) {
      return '0';
    }

    const suffixes = ['', 'K', 'M', 'G', 'T', 'P', 'E'];

    const power = Math.max(0, Math.floor(Math.log10(value) / 3));
    const scaledValue = value / Math.pow(1000, power);
    const suffix = suffixes[power] ?? '';
    const space = suffix ? ' ' : '';

    if (power > 0) {
      return scaledValue.toFixed(digits) + space + suffix;
    } else {
      return scaledValue.toFixed(0) + space + suffix;
    }
  }
}
