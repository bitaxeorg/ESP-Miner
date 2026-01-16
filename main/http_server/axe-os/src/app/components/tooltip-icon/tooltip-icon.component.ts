import { Component, Input, HostListener } from '@angular/core';

@Component({
  selector: 'tooltip-icon',
  templateUrl: './tooltip-icon.component.html',
  styleUrls: ['./tooltip-icon.component.scss'],
})
export class TooltipIconComponent {
  @Input() tooltip: string = '';
  @Input() size: string = 'xs';
  @Input() icon: string = '';

  showMobileTooltip = false;
  isMobile = ('ontouchstart' in window) || (navigator.maxTouchPoints > 0);

  get tooltipIconClass(): string {
    return `pi ${this.icon} text-${this.size} pl-1 pr-2 tooltip-icon`;
  }
}
