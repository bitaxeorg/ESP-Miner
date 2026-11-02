import { Component } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { LayoutService } from './layout/service/app.layout.service';
import { SnowflakesComponent } from './components/snowflakes/snowflakes.component';
import { DialogListComponent } from './services/dialog.service';

@Component({
    selector: 'app-root',
    templateUrl: './app.component.html',
    styleUrls: ['./app.component.scss'],
    imports: [RouterOutlet, SnowflakesComponent, DialogListComponent]
})
export class AppComponent {
  constructor(public layoutService: LayoutService) { }
}
