import { AfterViewInit, Component, ElementRef, OnDestroy, ViewChild } from '@angular/core';

interface ConfettiDivElement extends HTMLDivElement {
  removeTimeout?: number;
}

@Component({
  selector: 'app-confetti',
  templateUrl: './confetti.component.html',
  styleUrls: ['./confetti.component.scss']
})
export class ConfettiComponent implements AfterViewInit, OnDestroy {
  @ViewChild('container', { static: true }) containerRef!: ElementRef<HTMLDivElement>;

  private confettiColors = ['#fce18a', '#ff726d', '#b48def', '#f4306d'];
  private confettiAnimations = ['slow', 'medium', 'fast'];
  private confettiIntervalId?: number;

  ngAfterViewInit(): void {
    this.renderConfetti();
  }

  ngOnDestroy(): void {
    if (this.confettiIntervalId) {
      clearInterval(this.confettiIntervalId);
    }
  }

  private renderConfetti(): void {
    const el = this.containerRef.nativeElement;
    const containerEl = document.createElement('div');

    containerEl.classList.add('confetti-container');
    el.appendChild(containerEl);

    this.confettiIntervalId = window.setInterval(() => {
      const confettiEl = document.createElement('div') as ConfettiDivElement;

      const confettiSize = (Math.floor(Math.random() * 3) + 7) + 'px';
      const confettiBackground = this.confettiColors[Math.floor(Math.random() * this.confettiColors.length)];
      const confettiLeft = (Math.floor(Math.random() * el.offsetWidth)) + 'px';
      const confettiAnimation = this.confettiAnimations[Math.floor(Math.random() * this.confettiAnimations.length)];

      confettiEl.classList.add('confetti', `confetti--animation-${confettiAnimation}`);
      confettiEl.style.left = confettiLeft;
      confettiEl.style.width = confettiSize;
      confettiEl.style.height = confettiSize;
      confettiEl.style.backgroundColor = confettiBackground;

      confettiEl.removeTimeout = window.setTimeout(() => {
        if (confettiEl.parentNode) {
          confettiEl.parentNode.removeChild(confettiEl);
        }
      }, 3000);

      containerEl.appendChild(confettiEl);
    }, 25);
  }
}
