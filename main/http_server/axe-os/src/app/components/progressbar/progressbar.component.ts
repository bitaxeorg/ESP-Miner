import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';

export interface ProgressBarMarker {
  value: number;       // Percentage (0 to 100)
  label: string;       // Text label for the marker
  visible?: boolean;   // Optional visibility condition
}

/**
 * Fill colour for a meter.
 *   'brand'    - the theme colour, and the default
 *   'auto'     - brand until the value nears its maximum, then warn/critical
 *   'warn'     - caller decides
 *   'critical' - caller decides
 */
export type ProgressBarTone = 'brand' | 'auto' | 'warn' | 'critical';

@Component({
  selector: 'app-progressbar',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="relative w-full">
      <!-- Progress Bar Track -->
      <div class="w-full bg-progressbar rounded-sm overflow-hidden" [ngClass]="heightClass">
        <div
          class="bg-progressbar-value h-full transition-[width,background-color] duration-300"
          [style.width.%]="progressValue"
          [style.background]="fillColor"></div>
      </div>

      <!-- Optional Markers -->
      @for (marker of markers; track $index) {
        @if (marker.visible !== false && marker.value >= 0) {
          <div class="progressbar-marker" [style.left]="'round(' + marker.value + '%, 1px)'">
            <small class="progressbar-marker-label">
              {{ marker.label }}
            </small>
          </div>
        }
      }
    </div>
  `,
  styleUrl: './progressbar.component.scss'
})
export class ProgressbarComponent {
  @Input() value: number = 0;              // Current progress value (0 to 100)
  @Input() markers: ProgressBarMarker[] = []; // Optional marker lines
  @Input() heightClass: string = 'h-[6px]';  // Custom height class (e.g. h-6 for updates)
  @Input() tone: ProgressBarTone = 'brand';

  /* A working Bitaxe idles near 80% of its temperature ceiling, so 'auto'
     stays on the theme colour until well above that. */
  private static readonly WARN_AT = 85;
  private static readonly CRITICAL_AT = 95;

  get progressValue(): number {
    if (!this.value || isNaN(this.value) || this.value < 0) {
      return 0;
    }
    return Math.min(100, this.value);
  }

  get fillColor(): string {
    switch (this.resolvedTone) {
      case 'warn': return 'var(--color-status-warn)';
      case 'critical': return 'var(--color-status-critical)';
      default: return 'var(--color-primary)';
    }
  }

  private get resolvedTone(): ProgressBarTone {
    if (this.tone !== 'auto') {
      return this.tone;
    }
    if (this.progressValue >= ProgressbarComponent.CRITICAL_AT) {
      return 'critical';
    }
    if (this.progressValue >= ProgressbarComponent.WARN_AT) {
      return 'warn';
    }
    return 'brand';
  }
}
