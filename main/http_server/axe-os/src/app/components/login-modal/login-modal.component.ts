import { Component, OnInit, OnDestroy } from '@angular/core';
import { Subscription } from 'rxjs';
import { AuthService } from '../../services/auth.service';

@Component({
  selector: 'app-login-modal',
  templateUrl: './login-modal.component.html',
  standalone: false
})
export class LoginModalComponent implements OnInit, OnDestroy {
  public isVisible = false;
  public host = '';
  public error = '';
  public username = 'admin';
  public password = '';

  private sub?: Subscription;

  constructor(private authService: AuthService) {}

  ngOnInit(): void {
    this.sub = this.authService.promptState$.subscribe((state) => {
      if (state) {
        this.host = state.host;
        this.error = state.error || '';
        this.isVisible = true;
        this.password = ''; // clear password on new prompt
      } else {
        this.isVisible = false;
      }
    });
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  public onCancel(): void {
    this.authService.submitPrompt(null);
  }

  public onSubmit(): void {
    if (this.password) {
      this.authService.submitPrompt({
        username: this.username,
        password: this.password
      });
    }
  }
}
