import { AfterViewInit, Component, ElementRef, OnDestroy, OnInit, ViewChild } from '@angular/core';

@Component({
  selector: 'app-snowflakes',
  templateUrl: './snowflakes.component.html',
  styleUrls: ['./snowflakes.component.scss']
})
export class SnowflakesComponent implements OnInit, AfterViewInit, OnDestroy {
  @ViewChild('container', { static: true }) containerRef!: ElementRef<HTMLDivElement>;

  public showSnowflakes = false;
  private intervalId?: number;

  ngOnInit(): void {
    // Configuration: Set the date range for snowflakes
    // Month is 0-indexed (0 = January, 11 = December)
    const startMonth = 11;
    const startDay = 24;
    const endMonth = 11;
    const endDay = 26;

    this.showSnowflakes = this.isDateInRange(startMonth, startDay, endMonth, endDay);
  }

  private isDateInRange(startMonth: number, startDay: number, endMonth: number, endDay: number): boolean {
    const today = new Date();
    const currentMonth = today.getMonth();
    const currentDay = today.getDate();

    const current = currentMonth * 100 + currentDay;
    const start = startMonth * 100 + startDay;
    const end = endMonth * 100 + endDay;

    if (start <= end) {
      return current >= start && current <= end;
    } else {
      return current >= start || current <= end;
    }
  }

  ngAfterViewInit(): void {
    if (this.showSnowflakes) {
      this.startSnow();
    }
  }

  ngOnDestroy(): void {
    if (this.intervalId) {
      clearInterval(this.intervalId);
    }
  }

  private startSnow(): void {
    const el = this.containerRef.nativeElement;
    const containerEl = document.createElement('div');
    containerEl.classList.add('snowflake-container');
    el.appendChild(containerEl);

    this.intervalId = window.setInterval(() => {
      const snowflake = document.createElement('div');
      snowflake.classList.add('snowflake');

      // Random properties
      const left = Math.random() * 100 + 'vw';
      const animationDuration = Math.random() * 3 + 3 + 's'; // 3-6s
      const opacity = Math.random() * 0.5 + 0.3; // 0.3 - 0.8
      const size = Math.random() * 5 + 3 + 'px'; // 3-8px

      snowflake.style.left = left;
      snowflake.style.animationDuration = animationDuration;
      snowflake.style.opacity = opacity.toString();
      snowflake.style.width = size;
      snowflake.style.height = size;

      containerEl.appendChild(snowflake);

      // Remove after animation
      setTimeout(() => {
        if (snowflake.parentNode) {
          snowflake.parentNode.removeChild(snowflake);
        }
      }, parseFloat(animationDuration) * 1000);
    }, 200);
  }
}
