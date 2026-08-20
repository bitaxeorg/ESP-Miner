import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'heatmapLightness',
  pure: true,
  standalone: true
})
export class HeatmapLightnessPipe implements PipeTransform {
  transform(domainHashrate: number, expectedHashrate: number, asicsAmount: number, asicDomainsAmount: number): string {
    const expected = expectedHashrate || 1;
    const ratio = Math.max(0, Math.min(2, (domainHashrate / expected) * asicsAmount) * asicDomainsAmount);
    const deviation = isNaN(ratio) ? 1 : Math.abs(ratio - 1);  // 0 = perfect, 1 = 100% off
    const t = 1 - Math.pow(1 - deviation, 1.5); // Exponent controls graduality

    const direction = ratio > 1 ? 1 : -1;
    const amount = direction * t * 0.4;
    const lightness = 0.5 + amount;

    return lightness.toFixed(3);
  }
}
