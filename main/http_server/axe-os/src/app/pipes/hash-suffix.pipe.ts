import { Pipe, PipeTransform } from '@angular/core';

export interface HashSuffixArgs {
  tickmark?: boolean;
  power?: number;
  hideUnit?: boolean;
}

@Pipe({
    name: 'hashSuffix'
})
export class HashSuffixPipe implements PipeTransform {

  public static readonly SUFFIXES = [' H/s', ' Kh/s', ' Mh/s', ' Gh/s', ' Th/s', ' Ph/s', ' Eh/s'];

  private static _this = new HashSuffixPipe();

  public static getPower(value: number): number {
    if (value == null || value <= 0 || isNaN(value)) {
      return 0;
    }
    const valueHs = value * 1_000_000_000;
    const power = Math.floor(Math.log10(valueHs) / 3);
    return Math.max(0, Math.min(power, HashSuffixPipe.SUFFIXES.length - 1));
  }

  public static getSuffix(power: number): string {
    return HashSuffixPipe.SUFFIXES[power]?.trim() || 'H/s';
  }

  public static transform(value: number, args?: HashSuffixArgs): string {
    return this._this.transform(value, args);
  }

  public transform(value: number, args?: HashSuffixArgs): string {

    if (value == null || value <= 0 || isNaN(value)) {
      return args?.hideUnit ? '0' : '0 H/s';
    }

    // Normalize GH/s to H/s
    value = value * 1_000_000_000;

    let power = args?.power !== undefined
      ? args.power
      : Math.floor(Math.log10(value) / 3);

    if (power < 0) {
      power = 0;
    }
    if (power >= HashSuffixPipe.SUFFIXES.length) {
      power = HashSuffixPipe.SUFFIXES.length - 1;
    }

    const scaledValue = value / Math.pow(1000, power);
    const suffix = args?.hideUnit ? '' : HashSuffixPipe.SUFFIXES[power];

    if (args?.tickmark) {
      return scaledValue.toLocaleString(undefined, { useGrouping: false }) + suffix;
    }

    if (scaledValue < 10) {
      return scaledValue.toFixed(2) + suffix;
    } else if (scaledValue < 100) {
      return scaledValue.toFixed(1) + suffix;
    }

    return scaledValue.toFixed(0) + suffix;
  }
}

