import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';

export interface ProgressBarMarker {
  value: number;       // Percentage (0 to 100)
  label: string;       // Text label for the marker
  visible?: boolean;   // Optional visibility condition
}

@Component({
  selector: 'app-progressbar',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="relative w-full">
      <!-- Progress Bar Track -->
      <div class="w-full bg-progressbar rounded-sm overflow-hidden" [ngClass]="heightClass">
        <div class="bg-progressbar-value h-full transition-[width] duration-300" [style.width.%]="progressValue"></div>
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

  get progressValue(): number {
    if (!this.value || isNaN(this.value) || this.value < 0) {
      return 0;
    }
    return Math.min(100, this.value);
  }
}
