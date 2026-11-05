import { Component } from '@angular/core';
import { ThemeConfigComponent } from './theme-config.component';
import { TranslatePipe } from 'src/app/i18n/translate.pipe';

@Component({
    selector: 'app-design',
    templateUrl: './design.component.html',
    imports: [ThemeConfigComponent, TranslatePipe]
})
export class DesignComponent {
  constructor() { }
}
