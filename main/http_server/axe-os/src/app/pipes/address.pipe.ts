import { Pipe, PipeTransform } from '@angular/core';

export interface IAddressPipeArgs {
  length?: number;
  html?: boolean;
}

@Pipe({
  name: 'address',
  pure: true
})
export class AddressPipe implements PipeTransform {
  private static _this = new AddressPipe();

  public static transform(value: string, args?: IAddressPipeArgs): string {
    return this._this.transform(value, args);
  }

  public static hasAddress(value: string): boolean {
    if (!value) return false;
    const addressRegex = /\b(bc1[a-zA-HJ-NP-Z0-9]{39,87}|tb1[a-zA-HJ-NP-Z0-9]{39,87}|bcrt1[a-zA-HJ-NP-Z0-9]{39,87}|[13mn2][a-km-zA-HJ-NP-Z1-9]{25,34})\b/;
    return addressRegex.test(value);
  }

  public static formatAddress(address: string, maxLength: number = 22): string {
    if (!address) return address;

    const segments: string[] = [];
    for (let i = 0; i < address.length; i += 4) {
      segments.push(address.slice(i, i + 4));
    }

    let formatted = segments.join(' ');
    if (formatted.length <= maxLength) return formatted;

    const mid = Math.ceil(segments.length / 2);
    let left = segments.slice(0, mid);
    let right = segments.slice(mid);
    do {
      if (left.length > right.length) left.pop(); else right.shift();
      formatted = left.join(' ') + '...' + right.join(' ');
    } while (formatted.length > maxLength && (left.length > 1 || right.length > 1));

    return formatted;
  }

  transform(value: string, args?: IAddressPipeArgs): string {
    if (!value) return value;

    const maxLength = args?.length ?? 22;
    const useHtml = args?.html ?? true;

    // Detect Bitcoin address tokens (Bech32/Bech32m and Base58Check)
    const addressRegex = /\b(bc1[a-zA-HJ-NP-Z0-9]{39,87}|tb1[a-zA-HJ-NP-Z0-9]{39,87}|bcrt1[a-zA-HJ-NP-Z0-9]{39,87}|[13mn2][a-km-zA-HJ-NP-Z1-9]{25,34})\b/g;

    if (!addressRegex.test(value)) {
      return value;
    }

    addressRegex.lastIndex = 0;
    return value.replace(addressRegex, (match) => {
      const formatted = AddressPipe.formatAddress(match, maxLength);
      return useHtml ? `<span class="font-mono">${formatted}</span>` : formatted;
    });
  }
}
