import { 
  Directive, 
  ElementRef, 
  Input, 
  HostListener, 
  OnDestroy, 
  Component, 
  ComponentRef, 
  ViewContainerRef, 
  EmbeddedViewRef 
} from '@angular/core';

@Component({
  selector: 'app-tooltip-content',
  standalone: true,
  template: `{{ text }}`,
  host: {
    'class': 'fixed z-[99999] bg-[#2b2b2b] text-[#ffffff] app-tooltip-text px-2.5 py-1.5 rounded shadow-xl pointer-events-none border border-neutral-700 max-w-[250px] whitespace-normal text-center'
  }
})
export class TooltipContentComponent {
  @Input() text = '';
}

@Directive({
  selector: '[appTooltip]',
  standalone: true
})
export class TooltipDirective implements OnDestroy {
  @Input('appTooltip') tooltipText = '';
  @Input() tooltipPosition: 'top' | 'bottom' | 'left' | 'right' = 'top';

  private componentRef: ComponentRef<TooltipContentComponent> | null = null;

  constructor(
    private el: ElementRef, 
    private viewContainerRef: ViewContainerRef
  ) {}

  @HostListener('mouseenter')
  @HostListener('focusin')
  onMouseEnter() {
    if (!this.tooltipText) return;
    this.showTooltip();
  }

  @HostListener('mouseleave')
  @HostListener('focusout')
  @HostListener('click')
  onMouseLeave() {
    this.hideTooltip();
  }

  ngOnDestroy() {
    this.hideTooltip();
  }

  private showTooltip() {
    this.hideTooltip();

    this.componentRef = this.viewContainerRef.createComponent(TooltipContentComponent);
    this.componentRef.instance.text = this.tooltipText;
    
    // Run change detection so template is rendered and DOM node has size
    this.componentRef.changeDetectorRef.detectChanges();

    const domElem = (this.componentRef.hostView as EmbeddedViewRef<any>).rootNodes[0] as HTMLElement;
    document.body.appendChild(domElem);
    
    this.positionTooltip();
  }

  private hideTooltip() {
    if (this.componentRef) {
      this.componentRef.destroy();
      this.componentRef = null;
    }
  }

  private positionTooltip() {
    if (!this.componentRef) return;

    const domElem = (this.componentRef.hostView as EmbeddedViewRef<any>).rootNodes[0] as HTMLElement;
    const hostRect = this.el.nativeElement.getBoundingClientRect();
    const tooltipRect = domElem.getBoundingClientRect();

    let top = 0;
    let left = 0;
    const spacing = 6;

    switch (this.tooltipPosition) {
      case 'top':
        top = hostRect.top - tooltipRect.height - spacing;
        left = hostRect.left + (hostRect.width - tooltipRect.width) / 2;
        break;
      case 'bottom':
        top = hostRect.bottom + spacing;
        left = hostRect.left + (hostRect.width - tooltipRect.width) / 2;
        break;
      case 'left':
        top = hostRect.top + (hostRect.height - tooltipRect.height) / 2;
        left = hostRect.left - tooltipRect.width - spacing;
        break;
      case 'right':
        top = hostRect.top + (hostRect.height - tooltipRect.height) / 2;
        left = hostRect.right + spacing;
        break;
    }

    // Viewport bounds checking
    left = Math.max(spacing, Math.min(left, window.innerWidth - tooltipRect.width - spacing));
    top = Math.max(spacing, Math.min(top, window.innerHeight - tooltipRect.height - spacing));

    domElem.style.top = `${top}px`;
    domElem.style.left = `${left}px`;
  }
}
