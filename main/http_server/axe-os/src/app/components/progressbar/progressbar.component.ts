import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';

export interface ProgressBarMarker {
  value: number;       // Percentage (0 to 100)
  label: string;       // Text label for the marker
  visible?: boolean;   // Optional visibility condition
}

/**
 * How the filled portion is coloured.
 *
 * 'brand' keeps the accent colour and is the default, so a bar that has not
 * opted in looks exactly as it did before. 'auto' derives the colour from how
 * close the value is to its maximum, which is only meaningful for bars where
 * approaching the maximum is a problem — temperature, say, but not clock
 * frequency, where running at the top of the range is the goal.
 */
export type ProgressBarTone = 'brand' | 'neutral' | 'auto' | 'ok' | 'warn' | 'critical';

@Component({
  selector: 'app-progressbar',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="relative w-full">
      <!-- Progress Bar Track -->
      <div class="w-full bg-progressbar rounded-full overflow-hidden" [ngClass]="heightClass">
        <div
          class="h-full rounded-full transition-[width,background-color] duration-300"
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

  /**
   * Thresholds for 'auto'. Deliberately high: a Bitaxe sits around 80% of its
   * 75 C ceiling in normal operation, so warning at anything lower would mean
   * the dashboard is amber whenever the miner is simply working. 85% and 95%
   * of the ceiling land near where the firmware actually starts throttling.
   */
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
      case 'ok': return 'var(--color-status-ok)';
      case 'warn': return 'var(--color-status-warn)';
      case 'critical': return 'var(--color-status-critical)';
      case 'neutral': return 'var(--color-meter-neutral)';
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
    return 'ok';
  }
}
