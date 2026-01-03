import { Component, Input, OnChanges, SimpleChanges } from '@angular/core';

@Component({
  selector: 'tooltip-text-icon',
  templateUrl: './tooltip-text-icon.component.html',
})
export class TooltipTextIconComponent implements OnChanges {
  @Input() tooltip: string | null = '';
  @Input() icon: string = 'pi-question-circle';
  @Input() text: string | null = '';
  @Input() split: boolean = true;

  preLastWords: string = '';
  lastWord: string = '';

  ngOnChanges(changes: SimpleChanges): void {
    const safeText = (this.text ?? '').trim();

    const updateWords = (text: string) => {
      const words = text.split(/\s+/);
      if (words.length > 1) {
        this.preLastWords = words.slice(0, words.length - 1).join(' ');
        this.lastWord = words[words.length - 1];
      } else {
        this.preLastWords = '';
        this.lastWord = text;
      }
    };

    if ('text' in changes && safeText) {
      updateWords(safeText);
    } else if ('tooltip' in changes && this.tooltip && safeText) {
      updateWords(safeText);
    } else if (!safeText) {
      this.preLastWords = '';
      this.lastWord = '';
    }
  }
}
