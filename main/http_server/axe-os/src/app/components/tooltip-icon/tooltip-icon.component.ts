import { Component, Input, HostListener } from '@angular/core';

@Component({
  selector: 'tooltip-icon',
  templateUrl: './tooltip-icon.component.html',
  styleUrls: ['./tooltip-icon.component.scss'],
})
export class TooltipIconComponent {
  @Input() tooltip: string = '';
  @Input() size: string = 'xs';

  showMobileTooltip = false;
  tooltipIconClass = `pi pi-question-circle text-${this.size} pl-1 pr-2 tooltip-icon`;
  isMobile = ('ontouchstart' in window) || (navigator.maxTouchPoints > 0);
}
