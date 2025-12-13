import { TestBed } from '@angular/core/testing';
import { AppComponent } from './app.component';
import { SnowflakesComponent } from './components/snowflakes/snowflakes.component';
import { RouterModule } from '@angular/router';

describe('AppComponent', () => {
  beforeEach(() => TestBed.configureTestingModule({
    imports: [RouterModule],
    declarations: [AppComponent, SnowflakesComponent]
  }));

  it('should create the app', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const app = fixture.componentInstance;
    expect(app).toBeTruthy();
  });
});
