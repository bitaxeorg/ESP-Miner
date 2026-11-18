import { Component } from '@angular/core';
import { ThemeConfigComponent } from './theme-config.component';

@Component({
    selector: 'app-design',
    templateUrl: './design.component.html',
    imports: [ThemeConfigComponent]
})
export class DesignComponent {
  constructor() { }
}
