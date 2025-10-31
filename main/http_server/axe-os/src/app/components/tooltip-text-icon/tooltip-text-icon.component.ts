import { Component, Input, OnChanges, SimpleChanges } from '@angular/core';

@Component({
  selector: 'tooltip-text-icon',
  templateUrl: './tooltip-text-icon.component.html',
})
export class TooltipTextIconComponent implements OnChanges {
  @Input() tooltip: string | null = '';
  @Input() text: string | null = '';
  @Input() split: boolean = true;

  preLastWords: string = '';
  lastWord: string = '';

  ngOnChanges(changes: SimpleChanges): void {
    const safeText = (this.text ?? '').trim();

    if ('text' in changes && safeText) {
      const words = safeText.split(/\s+/);

      if (words.length > 1) {
        this.preLastWords = words.slice(0, words.length - 1).join(' ');
        this.lastWord = words[words.length - 1];
      } else {
        this.preLastWords = '';
        this.lastWord = safeText;
      }
    } else {
      this.preLastWords = '';
      this.lastWord = '';
    }
  }
}
