import { Component } from '@angular/core';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { TooltipDirective } from './tooltip.directive';

@Component({
  template: `<span [appTooltip]="tooltipText" tooltipPosition="bottom">Test Work</span>`,
  imports: [TooltipDirective]
})
class TestHostComponent {
  tooltipText = '100 total hashes';
}

describe('TooltipDirective', () => {
  let fixture: ComponentFixture<TestHostComponent>;
  let hostComponent: TestHostComponent;
  let element: HTMLElement;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [TestHostComponent, TooltipDirective]
    }).compileComponents();

    fixture = TestBed.createComponent(TestHostComponent);
    hostComponent = fixture.componentInstance;
    fixture.detectChanges();
    element = fixture.nativeElement.querySelector('span');
  });

  afterEach(() => {
    const activeTooltips = document.querySelectorAll('.app-tooltip-text');
    activeTooltips.forEach(el => el.remove());
  });

  it('should update tooltip content when appTooltip binding changes while active', () => {
    // Trigger hover to display tooltip
    element.dispatchEvent(new MouseEvent('mouseenter'));
    fixture.detectChanges();

    let tooltipEl = document.querySelector('.app-tooltip-text') as HTMLElement;
    expect(tooltipEl).toBeTruthy();
    expect(tooltipEl.textContent).toBe('100 total hashes');

    // Update bound tooltip text as new data comes in
    hostComponent.tooltipText = '500 total hashes';
    fixture.detectChanges();

    tooltipEl = document.querySelector('.app-tooltip-text') as HTMLElement;
    expect(tooltipEl).toBeTruthy();
    expect(tooltipEl.textContent).toBe('500 total hashes');
  });
});
