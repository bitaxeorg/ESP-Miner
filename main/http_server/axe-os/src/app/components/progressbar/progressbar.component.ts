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
        <div class="bg-progressbar-value h-full transition-all duration-300" [style.width.%]="value"></div>
      </div>

      <!-- Optional Markers -->
      @for (marker of markers; track $index) {
        @if (marker.visible !== false) {
          <div class="progressbar-marker" [style.left]="'round(' + marker.value + '%, 1px)'">
            <small class="progressbar-marker-label">
              {{ marker.label }}
            </small>
          </div>
        }
      }
    </div>
  `,
  styles: [`
    :host {
      display: block;
      width: 100%;
    }
  `]
})
export class ProgressbarComponent {
  @Input() value: number = 0;              // Current progress value (0 to 100)
  @Input() markers: ProgressBarMarker[] = []; // Optional marker lines
  @Input() heightClass: string = 'h-[6px]';  // Custom height class (e.g. h-6 for updates)
}
